/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_file_handle.h"

/**
 * @description: 获取当前表中记录号为rid的记录
 * @param {Rid&} rid 记录号，指定记录的位置
 * @param {Context*} context
 * @return {unique_ptr<RmRecord>} rid对应的记录对象指针
 */
std::unique_ptr<RmRecord> RmFileHandle::get_record(const Rid& rid, Context* context) const {
    // 1. 获取指定记录所在的 page handle
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    // 2. 检查记录是否存在
    if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }
    // 3. 复制完整 slot 数据（用户数据 + MVCC头部）到 RmRecord
    //    用户数据通过列偏移访问，MVCC头部存储在record末尾8字节
    auto record = std::make_unique<RmRecord>(file_hdr_.record_size);
    char* slot = page_handle.get_slot(rid.slot_no);
    memcpy(record->data, slot, file_hdr_.record_size);
    // 4. unpin 页面
    buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), false);
    return record;
}

/**
 * @description: 在当前表中插入一条记录，不指定插入位置
 * @param {char*} buf 要插入的记录的数据
 * @param {Context*} context
 * @return {Rid} 插入的记录的记录号（位置）
 */
Rid RmFileHandle::insert_record(char* buf, Context* context) {
    // 1. 获取当前有空闲空间的 page handle
    RmPageHandle page_handle = create_page_handle();
    // 2. 在 page 中找到空闲 slot 位置
    int slot_no = Bitmap::first_bit(false, page_handle.bitmap, file_hdr_.num_records_per_page);
    // 3. 将 buf 完整复制到空闲 slot 位置（buf 包含用户数据 + MVCC头部，共 record_size 字节）
    char* slot = page_handle.get_slot(slot_no);
    memcpy(slot, buf, file_hdr_.record_size);
    // 4. 更新 bitmap 和 page_hdr
    Bitmap::set(page_handle.bitmap, slot_no);
    page_handle.page_hdr->num_records++;
    Rid rid = {.page_no = page_handle.page->get_page_id().page_no, .slot_no = slot_no};
    // 5. 检查页面是否已满，若已满则更新 first_free_page_no
    if (page_handle.page_hdr->num_records == file_hdr_.num_records_per_page) {
        // 页面已满，从空闲链表中移除
        file_hdr_.first_free_page_no = page_handle.page_hdr->next_free_page_no;
        // 将 page_hdr 写回（因为 num_records 变化需要持久化）
        buffer_pool_manager_->mark_dirty(page_handle.page);
    } else {
        buffer_pool_manager_->mark_dirty(page_handle.page);
    }
    // 6. unpin 页面
    buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), true);
    return rid;
}

/**
 * @description: 插入记录并在页面修改前写入INSERT日志（确保WAL顺序：先写日志再写数据）
 * @param {char*} buf 要插入的完整记录
 * @param {Context*} context 执行上下文
 * @param {string&} tab_name 表名，用于构造日志
 * @return {pair<Rid, lsn_t>} 插入位置和INSERT日志LSN
 */
std::pair<Rid, lsn_t> RmFileHandle::insert_record_with_log(char* buf, Context* context, const std::string &tab_name) {
    // 1. 获取当前有空闲空间的 page handle。先分配位置，真正写入slot前必须先写WAL。
    RmPageHandle page_handle = create_page_handle();
    int slot_no = Bitmap::first_bit(false, page_handle.bitmap, file_hdr_.num_records_per_page);
    Rid rid = {.page_no = page_handle.page->get_page_id().page_no, .slot_no = slot_no};

    // 2. 修改页面前先写 INSERT 日志（WAL原则：日志先于数据）。
    //    日志先进入缓冲区，由 COMMIT 时统一刷盘（与 DELETE/UPDATE 行为一致）。
    lsn_t lsn = INVALID_LSN;
    if (context != nullptr && context->log_mgr_ != nullptr && context->txn_ != nullptr) {
        RmRecord record(file_hdr_.record_size);
        memcpy(record.data, buf, file_hdr_.record_size);
        InsertLogRecord log_rec(context->txn_->get_transaction_id(), record, rid, tab_name);
        log_rec.prev_lsn_ = context->txn_->get_prev_lsn();
        lsn = context->log_mgr_->add_log_to_buffer(&log_rec);
        context->txn_->set_prev_lsn(lsn);
    }

    // 3. 写入记录并设置页面LSN（日志已落盘，数据页修改是安全的）
    char* slot = page_handle.get_slot(slot_no);
    memcpy(slot, buf, file_hdr_.record_size);
    Bitmap::set(page_handle.bitmap, slot_no);
    page_handle.page_hdr->num_records++;
    if (lsn != INVALID_LSN) {
        page_handle.page->set_page_lsn(lsn);
    }

    if (page_handle.page_hdr->num_records == file_hdr_.num_records_per_page) {
        file_hdr_.first_free_page_no = page_handle.page_hdr->next_free_page_no;
    }
    buffer_pool_manager_->mark_dirty(page_handle.page);
    buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), true);
    return {rid, lsn};
}

/**
 * @description: 在当前表中的指定位置插入一条记录
 * @param {Rid&} rid 要插入记录的位置
 * @param {char*} buf 要插入记录的数据
 */
void RmFileHandle::insert_record(const Rid& rid, char* buf) {
    // 直接写入指定位置（用于恢复等场景），buf 包含完整slot（用户数据+MVCC头）
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    char* slot = page_handle.get_slot(rid.slot_no);
    memcpy(slot, buf, file_hdr_.record_size);
    Bitmap::set(page_handle.bitmap, rid.slot_no);
    page_handle.page_hdr->num_records++;
    buffer_pool_manager_->mark_dirty(page_handle.page);
    buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), true);
}

/**
 * @description: 删除记录文件中记录号为rid的记录
 * @param {Rid&} rid 要删除的记录的记录号（位置）
 * @param {Context*} context
 */
void RmFileHandle::delete_record(const Rid& rid, Context* context) {
    // 1. 获取指定记录所在的 page handle
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    // 2. 检查记录存在
    if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
        buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), false);
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }
    // 3. 更新 bitmap
    Bitmap::reset(page_handle.bitmap, rid.slot_no);
    page_handle.page_hdr->num_records--;
    buffer_pool_manager_->mark_dirty(page_handle.page);
    // 4. 若页面从满变为非满，更新空闲链表
    if (page_handle.page_hdr->num_records == file_hdr_.num_records_per_page - 1) {
        release_page_handle(page_handle);
    }
    buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), true);
}


/**
 * @description: 更新记录文件中记录号为rid的记录
 * @param {Rid&} rid 要更新的记录的记录号（位置）
 * @param {char*} buf 新记录的数据
 * @param {Context*} context
 */
void RmFileHandle::update_record(const Rid& rid, char* buf, Context* context) {
    // 1. 获取指定记录所在的 page handle
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    // 2. 检查记录存在
    if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
        buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), false);
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }
    // 3. 覆盖完整 slot 数据（包含用户数据+MVCC头部，共 record_size 字节）
    char* slot = page_handle.get_slot(rid.slot_no);
    memcpy(slot, buf, file_hdr_.record_size);
    buffer_pool_manager_->mark_dirty(page_handle.page);
    buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), true);
}

/**
 * 以下函数为辅助函数，仅提供参考，可以选择完成如下函数，也可以删除如下函数，在单元测试中不涉及如下函数接口的直接调用
*/
/**
 * @description: 获取指定页面的页面句柄
 * @param {int} page_no 页面号
 * @return {RmPageHandle} 指定页面的句柄
 */
RmPageHandle RmFileHandle::fetch_page_handle(int page_no) const {
    // 使用缓冲池获取指定页面
    PageId page_id = {.fd = fd_, .page_no = page_no};
    Page* page = buffer_pool_manager_->fetch_page(page_id);
    if (page == nullptr) {
        throw PageNotExistError(std::to_string(fd_), page_no);
    }
    return RmPageHandle(&file_hdr_, page);
}

/**
 * @description: 创建一个新的page handle
 * @return {RmPageHandle} 新的PageHandle
 */
RmPageHandle RmFileHandle::create_new_page_handle() {
    // 1. 使用缓冲池创建一个新 page
    PageId page_id = {.fd = fd_, .page_no = INVALID_PAGE_ID};
    Page* page = buffer_pool_manager_->new_page(&page_id);
    if (page == nullptr) {
        throw InternalError("Cannot create new page: buffer pool is full");
    }
    // 2. 初始化 RmPageHdr
    RmPageHandle page_handle(&file_hdr_, page);
    page_handle.page_hdr->next_free_page_no = RM_NO_PAGE;
    page_handle.page_hdr->num_records = 0;
    Bitmap::init(page_handle.bitmap, file_hdr_.bitmap_size);
    // 3. 更新 file_hdr_
    file_hdr_.num_pages++;
    file_hdr_.first_free_page_no = page_id.page_no;
    buffer_pool_manager_->mark_dirty(page);
    return page_handle;
}

/**
 * @brief 创建或获取一个空闲的page handle
 *
 * @return RmPageHandle 返回生成的空闲page handle
 * @note pin the page, remember to unpin it outside!
 */
RmPageHandle RmFileHandle::create_page_handle() {
    // 1. 判断 file_hdr_ 中是否还有空闲页
    if (file_hdr_.first_free_page_no == RM_NO_PAGE) {
        // 1.1 没有空闲页：创建新 page
        return create_new_page_handle();
    }
    // 1.2 有空闲页：直接获取第一个空闲页
    RmPageHandle page_handle = fetch_page_handle(file_hdr_.first_free_page_no);
    // 将 page unpin，由调用者负责 pin 管理（调用者会重新 fetch 使用）
    // 由于 fetch_page_handle 已 pin，这里直接返回
    return page_handle;
}

/**
 * @description: 当一个页面从没有空闲空间的状态变为有空闲空间状态时，更新文件头和页头中空闲页面相关的元数据
 */
void RmFileHandle::release_page_handle(RmPageHandle&page_handle) {
    // 当 page 从已满变成未满时，将其插入空闲链表头部
    int page_no = page_handle.page->get_page_id().page_no;
    page_handle.page_hdr->next_free_page_no = file_hdr_.first_free_page_no;
    file_hdr_.first_free_page_no = page_no;
}

/* ============ MVCC 相关方法 ============ */

/**
 * @description: 读取记录的MVCC头部（存储在slot末尾）
 * @param {Rid&} rid 记录号
 * @return {MvccHeader} MVCC头部
 */
MvccHeader RmFileHandle::get_mvcc_header(const Rid& rid) const {
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    int user_size = file_hdr_.record_size - MVCC_HEADER_SIZE;
    char* slot = page_handle.get_slot(rid.slot_no);
    MvccHeader hdr = *reinterpret_cast<MvccHeader*>(slot + user_size);
    buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), false);
    return hdr;
}

/**
 * @description: 设置记录的MVCC头部
 * @param {Rid&} rid 记录号
 * @param {MvccHeader&} header 要设置的MVCC头部
 */
void RmFileHandle::set_mvcc_header(const Rid& rid, const MvccHeader& header) {
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    int user_size = file_hdr_.record_size - MVCC_HEADER_SIZE;
    char* slot = page_handle.get_slot(rid.slot_no);
    *reinterpret_cast<MvccHeader*>(slot + user_size) = header;
    buffer_pool_manager_->mark_dirty(page_handle.page);
    buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), true);
}

/**
 * @description: MVCC可见性判断
 * 记录对事务可见当且仅当：
 *   1. xmin <= txn_ts（记录在事务开始前已创建）
 *   2. xmax > txn_ts 或 xmax == INT32_MAX（记录未被删除，或删除发生在事务开始之后）
 * @param {Rid&} rid 记录号
 * @param {timestamp_t} txn_ts 事务的时间戳
 * @return {bool} 是否可见
 */
bool RmFileHandle::is_visible(const Rid& rid, timestamp_t txn_ts) const {
    MvccHeader hdr = get_mvcc_header(rid);
    if (hdr.xmin_ > txn_ts) {
        return false;  // 记录在事务开始后创建，不可见
    }
    if (hdr.xmax_ != INT32_MAX && hdr.xmax_ <= txn_ts) {
        return false;  // 记录在事务开始前已被删除，不可见
    }
    return true;
}

/**
 * @description: 软删除记录（设置xmax，保留bitmap和物理存储）
 * 用于MVCC：旧版本读者仍可通过xmax看到此记录
 * @param {Rid&} rid 要软删除的记录号
 * @param {timestamp_t} xmax 删除时间戳
 * @param {Context*} context
 */
void RmFileHandle::soft_delete_record(const Rid& rid, timestamp_t xmax, Context* context) {
    // 1. 获取页面句柄
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);
    // 2. 检查记录存在
    if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
        buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), false);
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }
    // 3. 设置MVCC头部的xmax（软删除标记）
    int user_size = file_hdr_.record_size - MVCC_HEADER_SIZE;
    char* slot = page_handle.get_slot(rid.slot_no);
    MvccHeader* hdr = reinterpret_cast<MvccHeader*>(slot + user_size);
    hdr->xmax_ = xmax;
    // 4. 不修改bitmap（保留物理存储给旧版本读者）
    buffer_pool_manager_->mark_dirty(page_handle.page);
    buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), true);
}