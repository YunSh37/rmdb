/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <assert.h>

#include <memory>
#include <vector>

#include "bitmap.h"
#include "common/context.h"
#include "rm_defs.h"

class RmManager;

/* 对表数据文件中的页面进行封装 */
struct RmPageHandle {
    const RmFileHdr *file_hdr;  // 当前页面所在文件的文件头指针
    Page *page;                 // 页面的实际数据，包括页面存储的数据、元信息等
    RmPageHdr *page_hdr;        // page->data的第一部分，存储页面元信息，指针指向首地址，长度为sizeof(RmPageHdr)
    char *bitmap;               // page->data的第二部分，存储页面的bitmap，指针指向首地址，长度为file_hdr->bitmap_size
    char *slots;                // page->data的第三部分，存储表的记录，指针指向首地址，每个slot的长度为file_hdr->record_size

    RmPageHandle(const RmFileHdr *fhdr_, Page *page_) : file_hdr(fhdr_), page(page_) {
        page_hdr = reinterpret_cast<RmPageHdr *>(page->get_data() + page->OFFSET_PAGE_HDR);
        bitmap = page->get_data() + sizeof(RmPageHdr) + page->OFFSET_PAGE_HDR;
        slots = bitmap + file_hdr->bitmap_size;
    }

    // 返回指定slot_no的slot存储收地址
    char* get_slot(int slot_no) const {
        return slots + slot_no * file_hdr->record_size;  // slots的首地址 + slot个数 * 每个slot的大小(每个record的大小)
    }
};

/* 每个RmFileHandle对应一个表的数据文件，里面有多个page，每个page的数据封装在RmPageHandle中 */
class RmFileHandle {      
    friend class RmScan;    
    friend class RmManager;

   private:
    DiskManager *disk_manager_;
    BufferPoolManager *buffer_pool_manager_;
    int fd_;        // 打开文件后产生的文件句柄
    RmFileHdr file_hdr_;    // 文件头，维护当前表文件的元数据

   public:
    RmFileHandle(DiskManager *disk_manager, BufferPoolManager *buffer_pool_manager, int fd)
        : disk_manager_(disk_manager), buffer_pool_manager_(buffer_pool_manager), fd_(fd) {
        // 注意：这里从磁盘中读出文件描述符为fd的文件的file_hdr，读到内存中
        // 这里实际就是初始化file_hdr，只不过是从磁盘中读出进行初始化
        // init file_hdr_
        disk_manager_->read_page(fd, RM_FILE_HDR_PAGE, (char *)&file_hdr_, sizeof(file_hdr_));
        // disk_manager管理的fd对应的文件中，设置从file_hdr_.num_pages开始分配page_no
        disk_manager_->set_fd2pageno(fd, file_hdr_.num_pages);
    }

    RmFileHdr get_file_hdr() { return file_hdr_; }
    int GetFd() { return fd_; }

    /** 更新文件头的num_pages并立即写回磁盘（用于恢复时扩展文件后同步元数据） */
    void update_num_pages(int num_pages) {
        if (num_pages > file_hdr_.num_pages) {
            file_hdr_.num_pages = num_pages;
            disk_manager_->write_page(fd_, RM_FILE_HDR_PAGE, (char*)&file_hdr_, sizeof(file_hdr_));
            disk_manager_->set_fd2pageno(fd_, num_pages);
        }
    }

    /** 强制将文件头写回磁盘（用于检查点确保元数据持久化） */
    void sync_file_header() {
        disk_manager_->write_page(fd_, RM_FILE_HDR_PAGE, (char*)&file_hdr_, sizeof(file_hdr_));
    }

    /** 获取用户数据大小（不含MVCC头） */
    int get_user_record_size() const { return file_hdr_.record_size - MVCC_HEADER_SIZE; }

    /**
     * @brief 确保指定数据页在磁盘上存在（供恢复 redo 使用）
     * 若文件不够大，用正确初始化的空页填充，避免零页导致的 next_free_page_no=0
     * （其值应为 RM_NO_PAGE=-1），防止后续页面满时误将文件头页(0)当作空闲数据页。
     */
    bool ensure_page_exists(int page_no) {
        int file_size = disk_manager_->get_file_size(disk_manager_->get_file_name(fd_));
        if (file_size < 0) return false;
        int required_size = (page_no + 1) * PAGE_SIZE;
        if (file_size < required_size) {
            // 在内存中构建正确初始化的空页。
            // 页面布局: [LSN(4字节)] [RmPageHdr(8字节)] [bitmap] [slots]
            std::vector<char> page_buf(PAGE_SIZE, 0);
            // RmPageHdr 位于 LSN 之后（偏移 OFFSET_PAGE_HDR=4 字节）
            auto* phdr = reinterpret_cast<RmPageHdr*>(page_buf.data() + Page::OFFSET_PAGE_HDR);
            phdr->next_free_page_no = RM_NO_PAGE;   // -1，区别于文件头页0
            phdr->num_records = 0;
            // bitmap 位于 RmPageHdr 之后
            Bitmap::init(page_buf.data() + Page::OFFSET_PAGE_HDR + sizeof(RmPageHdr),
                         file_hdr_.bitmap_size);
            disk_manager_->write_page(fd_, page_no, page_buf.data(), PAGE_SIZE);
            disk_manager_->sync_file(fd_);
            printf("[Recovery] 扩展文件 到第%d页（已初始化页面头）\n", page_no);
        }
        return true;
    }

    /* 判断指定位置上是否已经存在一条记录，通过Bitmap来判断 */
    bool is_record(const Rid &rid) const {
        RmPageHandle page_handle = fetch_page_handle(rid.page_no);
        bool result = Bitmap::is_set(page_handle.bitmap, rid.slot_no);
        buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), false);  // 修复页面泄漏
        return result;
    }

    std::unique_ptr<RmRecord> get_record(const Rid &rid, Context *context) const;

    Rid insert_record(char *buf, Context *context);

    /** 插入记录并在页面修改前写入INSERT日志（确保WAL顺序：先写日志再写数据） */
    std::pair<Rid, lsn_t> insert_record_with_log(char *buf, Context *context, const std::string &tab_name);

    void insert_record(const Rid &rid, char *buf);

    void delete_record(const Rid &rid, Context *context);

    void update_record(const Rid &rid, char *buf, Context *context);

    /* ============== MVCC 相关方法 ============== */

    /** 读取记录的MVCC头部 */
    MvccHeader get_mvcc_header(const Rid &rid) const;

    /** 设置记录的MVCC头部 */
    void set_mvcc_header(const Rid &rid, const MvccHeader &header);

    /** MVCC可见性判断：记录是否对指定事务可见 */
    bool is_visible(const Rid &rid, timestamp_t txn_ts) const;

    /** 软删除：设置xmax而不清除bitmap（保留给旧版本读者） */
    void soft_delete_record(const Rid &rid, timestamp_t xmax, Context *context);

    RmPageHandle create_new_page_handle();

    RmPageHandle fetch_page_handle(int page_no) const;

   private:
    RmPageHandle create_page_handle();

    void release_page_handle(RmPageHandle &page_handle);
};