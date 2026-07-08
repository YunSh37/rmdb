/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

/**
 * @file log_recovery.cpp
 * @brief 面向题目十一的保守故障恢复实现
 *
 * 恢复原则：
 *   1. 表数据页是唯一恢复真相，索引在恢复完成后统一重建；
 *   2. REDO 全量扫描日志，不再依赖容易遗漏的 DPT 作为跳过依据；
 *   3. UNDO 只回滚 crash 时仍未完成的事务；
 *   4. ABORT 日志表示事务已经完成回滚，Analyze 阶段直接从 ATT 移除；
 *   5. 日志尾部损坏或半条日志时安全停止扫描。
 */

#include "log_recovery.h"
#include <vector>
#include <unordered_map>
#include <cstring>
#include "record/rm_file_handle.h"

/* ================================================================
 * 辅助函数
 * ================================================================ */

/** 从日志数据中反序列化一条日志记录 */
std::unique_ptr<LogRecord> RecoveryManager::parse_log_record(const char* data) {
    if (data == nullptr) return nullptr;
    LogType log_type = *reinterpret_cast<const LogType*>(data);
    std::unique_ptr<LogRecord> record;
    switch (log_type) {
        case LogType::begin:             record = std::make_unique<BeginLogRecord>(); break;
        case LogType::commit:            record = std::make_unique<CommitLogRecord>(); break;
        case LogType::ABORT:             record = std::make_unique<AbortLogRecord>(); break;
        case LogType::INSERT:            record = std::make_unique<InsertLogRecord>(); break;
        case LogType::DELETE:            record = std::make_unique<DeleteLogRecord>(); break;
        case LogType::UPDATE:            record = std::make_unique<UpdateLogRecord>(); break;
        case LogType::CHECKPOINT:        record = std::make_unique<CheckpointLogRecord>(); break;
        case LogType::INDEX_PAGE_MODIFY: record = std::make_unique<IndexPageModifyLogRecord>(); break;
        default: return nullptr;
    }
    record->deserialize(data);
    return record;
}

/** 获取表文件句柄（若未打开则打开） */
RmFileHandle* RecoveryManager::get_table_fh(const std::string& tab_name) {
    auto it = sm_manager_->fhs_.find(tab_name);
    if (it != sm_manager_->fhs_.end()) {
        return it->second.get();
    }
    auto fh = std::make_unique<RmFileHandle>(
        disk_manager_, buffer_pool_manager_,
        disk_manager_->open_file(tab_name));
    sm_manager_->fhs_[tab_name] = std::move(fh);
    return sm_manager_->fhs_[tab_name].get();
}

/** 安全获取文件 fd（表文件可能已被删除，返回 -1 表示不存在） */
int RecoveryManager::get_file_fd_safe(const std::string& tab_name) {
    try {
        return disk_manager_->get_file_fd(tab_name);
    } catch (FileNotFoundError& e) {
        printf("[Recovery] 跳过不存在的文件: %s\n", tab_name.c_str());
        return -1;
    }
}

// ensure_page_exists 已迁移到 RmFileHandle 成员方法，确保新页面被正确初始化
// （next_free_page_no=RM_NO_PAGE, bitmap 已初始化），避免零页导致的
// 后续页面满时误将文件头页(0)当作空闲数据页。

/* ================================================================
 * 阶段一：Analyze — 线性扫描日志，构建 ATT 和 LSN 索引
 * ================================================================ */
void RecoveryManager::analyze() {
    att_.clear();
    dpt_.clear();
    lsn_offsets_.clear();
    redo_lsn_ = INVALID_LSN;
    checkpoint_lsn_ = INVALID_LSN;
    max_txn_id_ = INVALID_TXN_ID;
    max_lsn_ = INVALID_LSN;

    int file_size = disk_manager_->get_file_size(LOG_FILE_NAME);
    if (file_size <= 0) {
        printf("[Recovery::Analyze] 日志文件为空或不存在，跳过恢复\n");
        return;
    }

    log_data_.assign(file_size, 0);
    int bytes_read = disk_manager_->read_log(log_data_.data(), file_size, 0);
    if (bytes_read <= 0) {
        printf("[Recovery::Analyze] 日志文件读取失败，跳过恢复\n");
        return;
    }

    int offset = 0;
    int total_records = 0;
    while (offset < bytes_read) {
        if (offset + LOG_HEADER_SIZE > bytes_read) break;

        LogType log_type = *reinterpret_cast<const LogType*>(log_data_.data() + offset);
        uint32_t log_len = *reinterpret_cast<const uint32_t*>(log_data_.data() + offset + OFFSET_LOG_TOT_LEN);
        lsn_t lsn = *reinterpret_cast<const lsn_t*>(log_data_.data() + offset + OFFSET_LSN);
        txn_id_t tid = *reinterpret_cast<const txn_id_t*>(log_data_.data() + offset + OFFSET_LOG_TID);

        if (log_len == 0 || log_len > static_cast<uint32_t>(bytes_read) ||
            offset + static_cast<int>(log_len) > bytes_read) {
            printf("[Recovery::Analyze] 偏移 %d 处日志不完整或长度非法，停止扫描\n", offset);
            break;
        }

        lsn_offsets_.emplace_back(lsn, offset);
        if (redo_lsn_ == INVALID_LSN) redo_lsn_ = lsn;
        if (max_lsn_ == INVALID_LSN || lsn > max_lsn_) max_lsn_ = lsn;
        if (tid != INVALID_TXN_ID && (max_txn_id_ == INVALID_TXN_ID || tid > max_txn_id_)) {
            max_txn_id_ = tid;
        }

        switch (log_type) {
            case LogType::begin:
                att_[tid] = lsn;
                break;
            case LogType::INSERT:
            case LogType::DELETE:
            case LogType::UPDATE:
                att_[tid] = lsn;
                break;
            case LogType::commit:
                att_.erase(tid);
                break;
            case LogType::ABORT:
                // ABORT 日志只在正常回滚完成并刷盘后写入，看到它即可认为事务已结束。
                att_.erase(tid);
                break;
            case LogType::CHECKPOINT:
                checkpoint_lsn_ = lsn;
                break;
            case LogType::INDEX_PAGE_MODIFY:
                // 索引页日志不参与最终正确性，恢复后会全量重建索引。
                if (tid != INVALID_TXN_ID && att_.count(tid)) att_[tid] = lsn;
                break;
            default:
                break;
        }

        offset += static_cast<int>(log_len);
        total_records++;
    }

    printf("[Recovery::Analyze] 记录=%d条 ATT=%zu redo_lsn=%d max_txn=%d max_lsn=%d\n",
           total_records, att_.size(), redo_lsn_, max_txn_id_, max_lsn_);
}

/* ================================================================
 * 阶段二：Redo — 全量扫描逻辑日志并按 pageLSN 幂等重放
 * ================================================================ */
void RecoveryManager::redo() {
    if (redo_lsn_ == INVALID_LSN || log_data_.empty()) {
        printf("[Recovery::Redo] 无需重做\n");
        return;
    }

    int redo_count = 0;
    int total_count = 0;
    int bytes_total = static_cast<int>(log_data_.size());

    for (auto& [lsn, offset] : lsn_offsets_) {
        if (offset + LOG_HEADER_SIZE > bytes_total) continue;
        auto record = parse_log_record(log_data_.data() + offset);
        if (!record) continue;
        total_count++;

        switch (record->log_type_) {
            case LogType::INSERT: {
                auto* r = dynamic_cast<InsertLogRecord*>(record.get());
                if (!r) break;
                try {
                    int fd = get_file_fd_safe(r->table_name_);
                    if (fd < 0) break;
                    auto fh = get_table_fh(r->table_name_);
                    if (!fh->ensure_page_exists(r->rid_.page_no)) break;
                    fh->update_num_pages(r->rid_.page_no + 1);
                    auto ph = fh->fetch_page_handle(r->rid_.page_no);
                    if (ph.page->get_page_lsn() < r->lsn_) {
                        bool existed = Bitmap::is_set(ph.bitmap, r->rid_.slot_no);
                        memcpy(ph.get_slot(r->rid_.slot_no), r->insert_value_.data, r->insert_value_.size);
                        if (!existed) {
                            Bitmap::set(ph.bitmap, r->rid_.slot_no);
                            ph.page_hdr->num_records++;
                        }
                        ph.page->set_page_lsn(r->lsn_);
                        buffer_pool_manager_->mark_dirty(ph.page);
                        redo_count++;
                    }
                    buffer_pool_manager_->unpin_page(ph.page->get_page_id(), true);
                } catch (std::exception& e) {
                    printf("[Recovery::Redo] INSERT 重做异常: %s\n", e.what());
                }
                break;
            }
            case LogType::DELETE: {
                auto* r = dynamic_cast<DeleteLogRecord*>(record.get());
                if (!r) break;
                try {
                    int fd = get_file_fd_safe(r->table_name_);
                    if (fd < 0) break;
                    auto fh = get_table_fh(r->table_name_);
                    if (!fh->ensure_page_exists(r->rid_.page_no)) break;
                    fh->update_num_pages(r->rid_.page_no + 1);
                    auto ph = fh->fetch_page_handle(r->rid_.page_no);
                    if (ph.page->get_page_lsn() < r->lsn_) {
                        bool existed = Bitmap::is_set(ph.bitmap, r->rid_.slot_no);
                        memcpy(ph.get_slot(r->rid_.slot_no), r->deleted_record_.data, r->deleted_record_.size);
                        if (!existed) {
                            Bitmap::set(ph.bitmap, r->rid_.slot_no);
                            ph.page_hdr->num_records++;
                        }
                        int user_size = fh->get_user_record_size();
                        auto* hdr = reinterpret_cast<MvccHeader*>(ph.get_slot(r->rid_.slot_no) + user_size);
                        hdr->xmax_ = r->xmax_;
                        ph.page->set_page_lsn(r->lsn_);
                        buffer_pool_manager_->mark_dirty(ph.page);
                        redo_count++;
                    }
                    buffer_pool_manager_->unpin_page(ph.page->get_page_id(), true);
                } catch (std::exception& e) {
                    printf("[Recovery::Redo] DELETE 重做异常: %s\n", e.what());
                }
                break;
            }
            case LogType::UPDATE: {
                auto* r = dynamic_cast<UpdateLogRecord*>(record.get());
                if (!r) break;
                try {
                    int fd = get_file_fd_safe(r->table_name_);
                    if (fd < 0) break;
                    auto fh = get_table_fh(r->table_name_);
                    if (!fh->ensure_page_exists(r->rid_.page_no)) break;
                    fh->update_num_pages(r->rid_.page_no + 1);
                    auto ph = fh->fetch_page_handle(r->rid_.page_no);
                    if (ph.page->get_page_lsn() < r->lsn_) {
                        bool existed = Bitmap::is_set(ph.bitmap, r->rid_.slot_no);
                        memcpy(ph.get_slot(r->rid_.slot_no), r->new_record_.data, r->new_record_.size);
                        if (!existed) {
                            Bitmap::set(ph.bitmap, r->rid_.slot_no);
                            ph.page_hdr->num_records++;
                        }
                        ph.page->set_page_lsn(r->lsn_);
                        buffer_pool_manager_->mark_dirty(ph.page);
                        redo_count++;
                    }
                    buffer_pool_manager_->unpin_page(ph.page->get_page_id(), true);
                } catch (std::exception& e) {
                    printf("[Recovery::Redo] UPDATE 重做异常: %s\n", e.what());
                }
                break;
            }
            default:
                // BEGIN/COMMIT/ABORT/CHECKPOINT/INDEX_PAGE_MODIFY 不在 REDO 中修改表数据。
                break;
        }
    }

    printf("[Recovery::Redo] 扫描 %d 条记录，应用了 %d 条 REDO\n", total_count, redo_count);
}

/* ================================================================
 * 阶段三：Undo — 回滚 Analyze 后仍在 ATT 中的 loser transaction
 * ================================================================ */
void RecoveryManager::undo() {
    if (att_.empty()) {
        printf("[Recovery::Undo] ATT 为空，无需回滚\n");
        return;
    }

    std::unordered_map<lsn_t, int> lsn_to_offset;
    for (auto& [lsn, offset] : lsn_offsets_) {
        lsn_to_offset[lsn] = offset;
    }

    int undo_count = 0;
    while (!att_.empty()) {
        lsn_t max_lsn = INVALID_LSN;
        txn_id_t victim_tid = INVALID_TXN_ID;
        for (auto& [tid, lsn] : att_) {
            if (max_lsn == INVALID_LSN || lsn > max_lsn) {
                max_lsn = lsn;
                victim_tid = tid;
            }
        }

        if (max_lsn == INVALID_LSN || lsn_to_offset.find(max_lsn) == lsn_to_offset.end()) {
            att_.erase(victim_tid);
            continue;
        }

        auto record = parse_log_record(log_data_.data() + lsn_to_offset[max_lsn]);
        if (!record) {
            att_.erase(victim_tid);
            continue;
        }

        switch (record->log_type_) {
            case LogType::INSERT: {
                auto* r = dynamic_cast<InsertLogRecord*>(record.get());
                if (!r) break;
                try {
                    int fd = get_file_fd_safe(r->table_name_);
                    if (fd < 0) break;
                    auto fh = get_table_fh(r->table_name_);
                    if (!fh->ensure_page_exists(r->rid_.page_no)) break;
                    if (fh->is_record(r->rid_)) {
                        fh->delete_record(r->rid_, nullptr);
                        auto ph = fh->fetch_page_handle(r->rid_.page_no);
                        ph.page->set_page_lsn(r->lsn_);
                        buffer_pool_manager_->unpin_page(ph.page->get_page_id(), true);
                        undo_count++;
                    }
                } catch (std::exception& e) {
                    printf("[Recovery::Undo] INSERT 回滚异常: %s\n", e.what());
                }
                break;
            }
            case LogType::DELETE: {
                auto* r = dynamic_cast<DeleteLogRecord*>(record.get());
                if (!r) break;
                try {
                    int fd = get_file_fd_safe(r->table_name_);
                    if (fd < 0) break;
                    auto fh = get_table_fh(r->table_name_);
                    if (!fh->ensure_page_exists(r->rid_.page_no)) break;
                    fh->update_num_pages(r->rid_.page_no + 1);
                    if (fh->is_record(r->rid_)) {
                        fh->update_record(r->rid_, r->deleted_record_.data, nullptr);
                    } else {
                        fh->insert_record(r->rid_, r->deleted_record_.data);
                    }
                    auto ph = fh->fetch_page_handle(r->rid_.page_no);
                    ph.page->set_page_lsn(r->lsn_);
                    buffer_pool_manager_->unpin_page(ph.page->get_page_id(), true);
                    undo_count++;
                } catch (std::exception& e) {
                    printf("[Recovery::Undo] DELETE 回滚异常: %s\n", e.what());
                }
                break;
            }
            case LogType::UPDATE: {
                auto* r = dynamic_cast<UpdateLogRecord*>(record.get());
                if (!r) break;
                try {
                    int fd = get_file_fd_safe(r->table_name_);
                    if (fd < 0) break;
                    auto fh = get_table_fh(r->table_name_);
                    if (!fh->ensure_page_exists(r->rid_.page_no)) break;
                    fh->update_num_pages(r->rid_.page_no + 1);
                    if (fh->is_record(r->rid_)) {
                        fh->update_record(r->rid_, r->old_record_.data, nullptr);
                    } else {
                        fh->insert_record(r->rid_, r->old_record_.data);
                    }
                    auto ph = fh->fetch_page_handle(r->rid_.page_no);
                    ph.page->set_page_lsn(r->lsn_);
                    buffer_pool_manager_->unpin_page(ph.page->get_page_id(), true);
                    undo_count++;
                } catch (std::exception& e) {
                    printf("[Recovery::Undo] UPDATE 回滚异常: %s\n", e.what());
                }
                break;
            }
            default:
                break;
        }

        lsn_t prev_lsn = record->prev_lsn_;
        if (prev_lsn == INVALID_LSN) {
            att_.erase(victim_tid);
        } else {
            att_[victim_tid] = prev_lsn;
        }
    }

    printf("[Recovery::Undo] 应用了 %d 条 UNDO\n", undo_count);
}
