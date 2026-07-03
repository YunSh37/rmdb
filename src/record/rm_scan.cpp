/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_scan.h"
#include "rm_file_handle.h"

/**
 * @brief 初始化file_handle和rid
 * @param file_handle
 */
RmScan::RmScan(const RmFileHandle *file_handle) : file_handle_(file_handle) {
    // 初始化：从第一个记录页开始，slot 设为 -1
    rid_ = {.page_no = RM_FIRST_RECORD_PAGE, .slot_no = -1};
    // 定位到第一条有效记录
    next();
}

/**
 * @brief 找到文件中下一个存放了记录的位置
 */
void RmScan::next() {
    // 从当前位置向后查找下一个有效记录
    while (rid_.page_no < file_handle_->file_hdr_.num_pages) {
        rid_.slot_no++;
        if (rid_.slot_no >= file_handle_->file_hdr_.num_records_per_page) {
            // 当前页已遍历完，跳到下一页第一个 slot
            rid_.page_no++;
            rid_.slot_no = -1;
            continue;
        }
        // 获取当前页检查 bitmap（RmScan 是 RmFileHandle 的友元类，可访问私有成员）
        RmPageHandle ph = file_handle_->fetch_page_handle(rid_.page_no);
        bool is_set = Bitmap::is_set(ph.bitmap, rid_.slot_no);
        file_handle_->buffer_pool_manager_->unpin_page(ph.page->get_page_id(), false);
        if (is_set) {
            return;  // 找到有效记录
        }
    }
    // 已到达文件末尾，rid_ 指向无效位置
}

/**
 * @brief ​ 判断是否到达文件末尾
 */
bool RmScan::is_end() const {
    // 当 page_no 超出文件范围时表示已到末尾
    return rid_.page_no >= file_handle_->file_hdr_.num_pages;
}

/**
 * @brief RmScan内部存放的rid
 */
Rid RmScan::rid() const {
    return rid_;
}