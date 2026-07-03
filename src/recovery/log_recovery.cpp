/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "log_recovery.h"
#include <vector>
#include <unordered_map>
#include "record/rm_file_handle.h"

std::unique_ptr<LogRecord> RecoveryManager::parse_log_record(const char* data) {
    LogType log_type = *reinterpret_cast<const LogType*>(data);
    std::unique_ptr<LogRecord> record;
    switch (log_type) {
        case LogType::begin:    record = std::make_unique<BeginLogRecord>(); break;
        case LogType::commit:   record = std::make_unique<CommitLogRecord>(); break;
        case LogType::ABORT:    record = std::make_unique<AbortLogRecord>(); break;
        case LogType::INSERT:   record = std::make_unique<InsertLogRecord>(); break;
        case LogType::DELETE:   record = std::make_unique<DeleteLogRecord>(); break;
        case LogType::UPDATE:   record = std::make_unique<UpdateLogRecord>(); break;
        case LogType::CHECKPOINT: record = std::make_unique<CheckpointLogRecord>(); break;
        default: return nullptr;
    }
    record->deserialize(data);
    return record;
}

RmFileHandle* RecoveryManager::get_table_fh(const std::string& tab_name) {
    auto it = sm_manager_->fhs_.find(tab_name);
    if (it != sm_manager_->fhs_.end()) return it->second.get();
    auto fh = std::make_unique<RmFileHandle>(disk_manager_, buffer_pool_manager_,
                                              disk_manager_->open_file(tab_name));
    sm_manager_->fhs_[tab_name] = std::move(fh);
    return sm_manager_->fhs_[tab_name].get();
}

void RecoveryManager::analyze() {
    int file_size = disk_manager_->get_file_size(LOG_FILE_NAME);
    if (file_size <= 0) { redo_lsn_ = INVALID_LSN; return; }

    log_data_.resize(file_size);
    int bytes_read = disk_manager_->read_log(log_data_.data(), file_size, 0);
    if (bytes_read <= 0) { redo_lsn_ = INVALID_LSN; return; }

    // ================================================================
    // 阶段一：快速头部扫描
    //   目的：(1) 建立 LSN→偏移 映射供 UNDO 使用
    //         (2) 找到最后一个静态检查点
    //         (3) 跟踪最大事务ID
    //   本阶段不反序列化任何数据记录（仅读固定头部），也不操作 ATT/DPT
    // ================================================================
    int scan_offset = 0;
    int last_checkpoint_offset = -1;
    int total_records = 0;

    while (scan_offset < bytes_read) {
        if (scan_offset + LOG_HEADER_SIZE > bytes_read) break;
        LogType log_type = *reinterpret_cast<const LogType*>(log_data_.data() + scan_offset);
        uint32_t log_len = *reinterpret_cast<const uint32_t*>(log_data_.data() + scan_offset + OFFSET_LOG_TOT_LEN);
        if (log_len == 0 || scan_offset + log_len > bytes_read) break;
        lsn_t lsn = *reinterpret_cast<const lsn_t*>(log_data_.data() + scan_offset + OFFSET_LSN);
        lsn_offsets_.emplace_back(lsn, scan_offset);

        txn_id_t tid = *reinterpret_cast<const txn_id_t*>(log_data_.data() + scan_offset + OFFSET_LOG_TID);
        if (tid != INVALID_TXN_ID && (max_txn_id_ == INVALID_TXN_ID || tid > max_txn_id_))
            max_txn_id_ = tid;

        if (log_type == LogType::CHECKPOINT)
            last_checkpoint_offset = scan_offset;

        scan_offset += log_len;
        total_records++;
    }

    // ================================================================
    // 阶段二：解析检查点并初始化恢复状态
    //   静态检查点的语义保证：创建检查点时已执行全量刷盘（flush_all_pages
    //   + fsync），因此检查点 LSN 之前的所有数据修改都已持久化。
    //
    //   利用检查点数据：
    //   - ATT：检查点记录的活动事务快照 → 作为 UNDO 的初始状态
    //   - DPT：检查点记录的脏页快照 → 作为 REDO 的 DPT 基础
    //   - REDO 起点：由于静态检查点保证全量刷盘，REDO 可从检查点 LSN
    //     之后的第一个操作开始，跳过所有已持久化的操作
    // ================================================================
    lsn_t ckpt_lsn = INVALID_LSN;
    int post_ckpt_offset = 0;  // 检查点之后第一条记录的偏移

    if (last_checkpoint_offset >= 0) {
        auto ckpt = std::make_unique<CheckpointLogRecord>();
        ckpt->deserialize(log_data_.data() + last_checkpoint_offset);
        checkpoint_lsn_ = ckpt->lsn_;
        ckpt_lsn = ckpt->lsn_;

        // 计算检查点之后第一条记录的偏移
        uint32_t ckpt_log_len = *reinterpret_cast<const uint32_t*>(
            log_data_.data() + last_checkpoint_offset + OFFSET_LOG_TOT_LEN);
        post_ckpt_offset = last_checkpoint_offset + static_cast<int>(ckpt_log_len);

        // 从检查点 ATT 初始化活跃事务表
        for (auto& [tid, lsn] : ckpt->att_) {
            att_[tid] = lsn;
            if (tid > max_txn_id_) max_txn_id_ = tid;
        }

        // 从检查点 DPT 初始化脏页表（表名→fd 转换）
        for (auto& [tab_name, page_no, lsn] : ckpt->dpt_entries_) {
            int fd = get_file_fd_safe(tab_name);
            if (fd < 0) continue;
            PageId pid{fd, page_no};
            if (dpt_.find(pid) == dpt_.end() || lsn < dpt_[pid])
                dpt_[pid] = lsn;
        }

        // 静态检查点已全量刷盘 → REDO 从检查点之后开始
        // 找到第一个 LSN > ckpt_lsn 的记录
        redo_lsn_ = INVALID_LSN;
        for (auto& [lsn, offset] : lsn_offsets_) {
            if (lsn > ckpt_lsn) {
                redo_lsn_ = lsn;
                break;
            }
        }
    }

    // ================================================================
    // 阶段三：增量扫描
    //   有检查点 → 从检查点之后扫描，更新 ATT（新事务）和 DPT（新脏页）
    //   无检查点 → 从日志开头全量扫描，建立 ATT 和 DPT
    // ================================================================
    int start_offset = (last_checkpoint_offset >= 0) ? post_ckpt_offset : 0;

    while (start_offset < bytes_read) {
        if (start_offset + LOG_HEADER_SIZE > bytes_read) break;
        LogType log_type = *reinterpret_cast<const LogType*>(log_data_.data() + start_offset);
        uint32_t log_len = *reinterpret_cast<const uint32_t*>(log_data_.data() + start_offset + OFFSET_LOG_TOT_LEN);
        if (log_len == 0 || start_offset + log_len > bytes_read) break;
        lsn_t lsn = *reinterpret_cast<const lsn_t*>(log_data_.data() + start_offset + OFFSET_LSN);
        txn_id_t tid = *reinterpret_cast<const txn_id_t*>(log_data_.data() + start_offset + OFFSET_LOG_TID);

        switch (log_type) {
            case LogType::begin: {
                att_[tid] = lsn;
                break;
            }
            case LogType::commit: {
                att_.erase(tid);
                break;
            }
            case LogType::ABORT: {
                att_.erase(tid);
                aborted_txns_.insert(tid);
                break;
            }
            case LogType::CHECKPOINT:
                break;
            case LogType::INSERT: {
                att_[tid] = lsn;
                InsertLogRecord rec;
                rec.deserialize(log_data_.data() + start_offset);
                int fd = get_file_fd_safe(rec.table_name_);
                if (fd < 0) break;
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) dpt_[pid] = lsn;
                else dpt_[pid] = std::min(dpt_[pid], lsn);
                break;
            }
            case LogType::DELETE: {
                att_[tid] = lsn;
                DeleteLogRecord rec;
                rec.deserialize(log_data_.data() + start_offset);
                int fd = get_file_fd_safe(rec.table_name_);
                if (fd < 0) break;
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) dpt_[pid] = lsn;
                break;
            }
            case LogType::UPDATE: {
                att_[tid] = lsn;
                UpdateLogRecord rec;
                rec.deserialize(log_data_.data() + start_offset);
                int fd = get_file_fd_safe(rec.table_name_);
                if (fd < 0) break;
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) dpt_[pid] = lsn;
                break;
            }
        }
        start_offset += log_len;
    }

    // 无检查点时，从 DPT 最小 LSN 开始 REDO（标准 ARIES 行为）
    if (last_checkpoint_offset < 0) {
        redo_lsn_ = INVALID_LSN;
        for (auto& [pid, lsn] : dpt_) {
            if (redo_lsn_ == INVALID_LSN || lsn < redo_lsn_)
                redo_lsn_ = lsn;
        }
    }

    printf("[Recovery::Analyze] 记录=%d条 ATT=%zu DPT=%zu redo_lsn=%d ckpt=%d\n",
           total_records, att_.size(), dpt_.size(), redo_lsn_, checkpoint_lsn_);
}

int RecoveryManager::get_file_fd_safe(const std::string& tab_name) {
    try { return disk_manager_->get_file_fd(tab_name); }
    catch (FileNotFoundError& e) {
        printf("[Recovery] 跳过不存在的表: %s\n", tab_name.c_str());
        return -1;
    }
}

bool RecoveryManager::ensure_page_exists(const std::string& tab_name, int page_no) {
    int file_size = disk_manager_->get_file_size(tab_name);
    if (file_size < 0) return false;
    int required_size = (page_no + 1) * PAGE_SIZE;
    if (file_size < required_size) {
        int fd = get_file_fd_safe(tab_name);
        std::vector<char> zero_page(PAGE_SIZE, 0);
        disk_manager_->write_page(fd, page_no, zero_page.data(), PAGE_SIZE);
        printf("[Recovery::Redo] 扩展 %s 第%d页\n", tab_name.c_str(), page_no);
        return true;
    }
    return false;
}

void RecoveryManager::redo() {
    if (redo_lsn_ == INVALID_LSN) {
        printf("[Recovery::Redo] 无需重做\n"); return;
    }
    int start_offset = -1;
    for (auto& [lsn, offset] : lsn_offsets_)
        if (lsn == redo_lsn_) { start_offset = offset; break; }
    if (start_offset < 0) {
        printf("[Recovery::Redo] 未找到 redo_lsn=%d\n", redo_lsn_); return;
    }

    int offset = start_offset, redo_count = 0;
    while (offset < static_cast<int>(log_data_.size())) {
        if (offset + LOG_HEADER_SIZE > static_cast<int>(log_data_.size())) break;
        LogType log_type = *reinterpret_cast<const LogType*>(log_data_.data() + offset);
        uint32_t log_len = *reinterpret_cast<const uint32_t*>(log_data_.data() + offset + OFFSET_LOG_TOT_LEN);
        lsn_t lsn = *reinterpret_cast<const lsn_t*>(log_data_.data() + offset + OFFSET_LSN);
        if (log_len == 0 || offset + log_len > static_cast<int>(log_data_.size())) break;
        txn_id_t tid = *reinterpret_cast<const txn_id_t*>(log_data_.data() + offset + OFFSET_LOG_TID);
        if (aborted_txns_.count(tid) > 0) { offset += log_len; continue; }

        switch (log_type) {
            case LogType::INSERT: {
                InsertLogRecord rec; rec.deserialize(log_data_.data() + offset);
                int fd = get_file_fd_safe(rec.table_name_);
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) break;
                ensure_page_exists(rec.table_name_, rec.rid_.page_no);
                auto fh = get_table_fh(rec.table_name_);
                fh->update_num_pages(rec.rid_.page_no + 1);
                auto ph = fh->fetch_page_handle(rec.rid_.page_no);
                if (ph.page->get_page_lsn() < lsn) {
                    memcpy(ph.get_slot(rec.rid_.slot_no), rec.insert_value_.data,
                           rec.insert_value_.size);
                    Bitmap::set(ph.bitmap, rec.rid_.slot_no);
                    ph.page_hdr->num_records++;
                    ph.page->set_page_lsn(lsn);
                    buffer_pool_manager_->mark_dirty(ph.page);
                    redo_count++;
                }
                buffer_pool_manager_->unpin_page(pid, true);
                break;
            }
            case LogType::DELETE: {
                DeleteLogRecord rec; rec.deserialize(log_data_.data() + offset);
                int fd = get_file_fd_safe(rec.table_name_);
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) break;
                ensure_page_exists(rec.table_name_, rec.rid_.page_no);
                auto fh = get_table_fh(rec.table_name_);
                fh->update_num_pages(rec.rid_.page_no + 1);
                auto ph = fh->fetch_page_handle(rec.rid_.page_no);
                if (ph.page->get_page_lsn() < lsn) {
                    memcpy(ph.get_slot(rec.rid_.slot_no), rec.deleted_record_.data,
                           rec.deleted_record_.size);
                    ph.page->set_page_lsn(lsn);
                    buffer_pool_manager_->mark_dirty(ph.page);
                    redo_count++;
                }
                buffer_pool_manager_->unpin_page(pid, true);
                break;
            }
            case LogType::UPDATE: {
                UpdateLogRecord rec; rec.deserialize(log_data_.data() + offset);
                int fd = get_file_fd_safe(rec.table_name_);
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) break;
                ensure_page_exists(rec.table_name_, rec.rid_.page_no);
                auto fh = get_table_fh(rec.table_name_);
                fh->update_num_pages(rec.rid_.page_no + 1);
                auto ph = fh->fetch_page_handle(rec.rid_.page_no);
                if (ph.page->get_page_lsn() < lsn) {
                    memcpy(ph.get_slot(rec.rid_.slot_no), rec.new_record_.data,
                           rec.new_record_.size);
                    ph.page->set_page_lsn(lsn);
                    buffer_pool_manager_->mark_dirty(ph.page);
                    redo_count++;
                }
                buffer_pool_manager_->unpin_page(pid, true);
                break;
            }
            default: break;
        }
        offset += log_len;
    }
    printf("[Recovery::Redo] 应用了 %d 条\n", redo_count);
}

void RecoveryManager::undo() {
    if (att_.empty()) { printf("[Recovery::Undo] 无需回滚\n"); return; }
    int undo_count = 0;
    std::unordered_map<lsn_t, int> lsn_to_offset;
    for (auto& [lsn, offset] : lsn_offsets_) lsn_to_offset[lsn] = offset;

    while (!att_.empty()) {
        lsn_t max_lsn = INVALID_LSN; txn_id_t victim_tid = INVALID_TXN_ID;
        for (auto& [tid, lsn] : att_)
            if (lsn > max_lsn) { max_lsn = lsn; victim_tid = tid; }
        if (max_lsn == INVALID_LSN || lsn_to_offset.find(max_lsn) == lsn_to_offset.end())
            { att_.erase(victim_tid); continue; }

        int offset = lsn_to_offset[max_lsn];
        auto record = parse_log_record(log_data_.data() + offset);
        if (!record) { att_.erase(victim_tid); continue; }

        switch (record->log_type_) {
            case LogType::INSERT: {
                auto* r = dynamic_cast<InsertLogRecord*>(record.get());
                if (r) {
                    ensure_page_exists(r->table_name_, r->rid_.page_no);
                    auto fh = get_table_fh(r->table_name_);
                    if (fh->is_record(r->rid_)) fh->delete_record(r->rid_, nullptr);
                    undo_count++;
                }
                break;
            }
            case LogType::DELETE: {
                auto* r = dynamic_cast<DeleteLogRecord*>(record.get());
                if (r) {
                    ensure_page_exists(r->table_name_, r->rid_.page_no);
                    auto fh = get_table_fh(r->table_name_);
                    if (fh->is_record(r->rid_))
                        fh->update_record(r->rid_, r->deleted_record_.data, nullptr);
                    else fh->insert_record(r->rid_, r->deleted_record_.data);
                    undo_count++;
                }
                break;
            }
            case LogType::UPDATE: {
                auto* r = dynamic_cast<UpdateLogRecord*>(record.get());
                if (r) {
                    ensure_page_exists(r->table_name_, r->rid_.page_no);
                    auto fh = get_table_fh(r->table_name_);
                    if (fh->is_record(r->rid_))
                        fh->update_record(r->rid_, r->old_record_.data, nullptr);
                    undo_count++;
                }
                break;
            }
            default: break;
        }
        lsn_t prev_lsn = record->prev_lsn_;
        if (prev_lsn == INVALID_LSN) att_.erase(victim_tid);
        else att_[victim_tid] = prev_lsn;
    }
    printf("[Recovery::Undo] 应用了 %d 条\n", undo_count);
}
