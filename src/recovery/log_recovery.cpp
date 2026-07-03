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

    // 单遍扫描：LSN→offset + ATT + DPT + 检查点
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

        switch (log_type) {
            case LogType::begin:
                att_[tid] = lsn;
                break;
            case LogType::commit:
                att_.erase(tid);
                break;
            case LogType::ABORT:
                att_.erase(tid);
                aborted_txns_.insert(tid);
                break;
            case LogType::CHECKPOINT:
                last_checkpoint_offset = scan_offset;
                break;
            case LogType::INSERT: {
                att_[tid] = lsn;
                InsertLogRecord rec; rec.deserialize(log_data_.data() + scan_offset);
                int fd = get_file_fd_safe(rec.table_name_);
                if (fd < 0) break;
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) dpt_[pid] = lsn;
                else dpt_[pid] = std::min(dpt_[pid], lsn);
                break;
            }
            case LogType::DELETE: {
                att_[tid] = lsn;
                DeleteLogRecord rec; rec.deserialize(log_data_.data() + scan_offset);
                int fd = get_file_fd_safe(rec.table_name_);
                if (fd < 0) break;
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) dpt_[pid] = lsn;
                break;
            }
            case LogType::UPDATE: {
                att_[tid] = lsn;
                UpdateLogRecord rec; rec.deserialize(log_data_.data() + scan_offset);
                int fd = get_file_fd_safe(rec.table_name_);
                if (fd < 0) break;
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) dpt_[pid] = lsn;
                break;
            }
        }
        scan_offset += log_len;
        total_records++;
    }

    // 检查点信息
    lsn_t checkpoint_min_rec_lsn = INVALID_LSN;
    if (last_checkpoint_offset >= 0) {
        auto ckpt = std::make_unique<CheckpointLogRecord>();
        ckpt->deserialize(log_data_.data() + last_checkpoint_offset);
        checkpoint_lsn_ = ckpt->lsn_;
        for (auto& [tab_name, page_no, lsn] : ckpt->dpt_entries_) {
            if (lsn > 0 && (checkpoint_min_rec_lsn == INVALID_LSN || lsn < checkpoint_min_rec_lsn))
                checkpoint_min_rec_lsn = lsn;
        }
    }

    // 计算全量 DPT 最小 LSN
    lsn_t full_dpt_min = INVALID_LSN;
    for (auto& [pid, lsn] : dpt_)
        if (full_dpt_min == INVALID_LSN || lsn < full_dpt_min) full_dpt_min = lsn;

    // REDO 起点：使用检查点优化，从检查点 DPT 最小 rec_lsn 开始
    // 检查点将所有脏页刷盘，rec_lsn < checkpoint_min_rec_lsn 的操作都已持久化
    if (checkpoint_min_rec_lsn != INVALID_LSN) {
        redo_lsn_ = checkpoint_min_rec_lsn;
        printf("[Recovery::Analyze] REDO优化: 使用检查点 min_rec_lsn=%d (full=%d ckpt=%d)\n",
               redo_lsn_, full_dpt_min, checkpoint_lsn_);
    } else {
        redo_lsn_ = full_dpt_min;
        if (redo_lsn_ == INVALID_LSN && checkpoint_lsn_ != INVALID_LSN)
            redo_lsn_ = checkpoint_lsn_;
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
