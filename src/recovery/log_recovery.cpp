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
#include "record/rm_file_handle.h"

/**
 * @description: 从日志数据中解析一条日志记录
 * @param data: 指向日志记录起始位置的指针
 * @return 反序列化后的日志记录对象
 */
std::unique_ptr<LogRecord> RecoveryManager::parse_log_record(const char* data) {
    // 读取日志类型（前4字节）
    LogType log_type = *reinterpret_cast<const LogType*>(data);

    std::unique_ptr<LogRecord> record;
    switch (log_type) {
        case LogType::begin:
            record = std::make_unique<BeginLogRecord>();
            break;
        case LogType::commit:
            record = std::make_unique<CommitLogRecord>();
            break;
        case LogType::ABORT:
            record = std::make_unique<AbortLogRecord>();
            break;
        case LogType::INSERT:
            record = std::make_unique<InsertLogRecord>();
            break;
        case LogType::DELETE:
            record = std::make_unique<DeleteLogRecord>();
            break;
        case LogType::UPDATE:
            record = std::make_unique<UpdateLogRecord>();
            break;
        case LogType::CHECKPOINT:
            record = std::make_unique<CheckpointLogRecord>();
            break;
        default:
            return nullptr;
    }
    record->deserialize(data);
    return record;
}

/**
 * @description: 获取表文件句柄（若sm_manager中未打开，则打开）
 */
RmFileHandle* RecoveryManager::get_table_fh(const std::string& tab_name) {
    auto it = sm_manager_->fhs_.find(tab_name);
    if (it != sm_manager_->fhs_.end()) {
        return it->second.get();
    }
    // 表文件尚未打开，打开它
    auto fh = std::make_unique<RmFileHandle>(disk_manager_, buffer_pool_manager_,
                                              disk_manager_->open_file(tab_name));
    sm_manager_->fhs_[tab_name] = std::move(fh);
    return sm_manager_->fhs_[tab_name].get();
}

/**
 * @description: ARIES analyze阶段：
 *   1. 从头扫描日志文件
 *   2. 构建ATT（活跃事务表）和DPT（脏页表）
 *   3. 若遇到检查点日志，从其记录的ATT/DPT开始
 *   4. 确定redo_lsn = min(DPT中所有LSN)
 */
void RecoveryManager::analyze() {
    // 1. 读取整个日志文件
    int file_size = disk_manager_->get_file_size(LOG_FILE_NAME);
    if (file_size <= 0) {
        // 日志文件不存在或为空，无需恢复
        redo_lsn_ = INVALID_LSN;
        return;
    }

    log_data_.resize(file_size);
    int bytes_read = disk_manager_->read_log(log_data_.data(), file_size, 0);
    if (bytes_read <= 0) {
        redo_lsn_ = INVALID_LSN;
        return;
    }

    // 2. 检查是否有检查点日志（从后往前找最后一个CHECKPOINT）
    int scan_offset = 0;
    int last_checkpoint_offset = -1;
    while (scan_offset < bytes_read) {
        if (scan_offset + LOG_HEADER_SIZE > bytes_read) break;
        LogType log_type = *reinterpret_cast<const LogType*>(log_data_.data() + scan_offset);
        uint32_t log_len = *reinterpret_cast<const uint32_t*>(log_data_.data() + scan_offset + OFFSET_LOG_TOT_LEN);
        if (log_len == 0 || scan_offset + log_len > bytes_read) break;

        lsn_t lsn = *reinterpret_cast<const lsn_t*>(log_data_.data() + scan_offset + OFFSET_LSN);
        lsn_offsets_.emplace_back(lsn, scan_offset);

        if (log_type == LogType::CHECKPOINT) {
            last_checkpoint_offset = scan_offset;
        }
        scan_offset += log_len;
    }

    // 3. 若有检查点，从检查点初始化ATT和DPT
    if (last_checkpoint_offset >= 0) {
        auto ckpt = std::make_unique<CheckpointLogRecord>();
        ckpt->deserialize(log_data_.data() + last_checkpoint_offset);
        checkpoint_lsn_ = ckpt->lsn_;

        // 从检查点恢复ATT
        for (auto& [tid, lsn] : ckpt->att_) {
            att_[tid] = lsn;
        }
        // 从检查点恢复DPT
        for (auto& [pid, lsn] : ckpt->dpt_) {
            dpt_[pid] = lsn;
        }

        // 从检查点之后继续扫描
        scan_offset = last_checkpoint_offset + ckpt->log_tot_len_;
    } else {
        // 无检查点，从头开始
        scan_offset = 0;
    }

    // 4. 从检查点之后（或从头）扫描日志，更新ATT和DPT
    while (scan_offset < bytes_read) {
        if (scan_offset + LOG_HEADER_SIZE > bytes_read) break;

        LogType log_type = *reinterpret_cast<const LogType*>(log_data_.data() + scan_offset);
        uint32_t log_len = *reinterpret_cast<const uint32_t*>(log_data_.data() + scan_offset + OFFSET_LOG_TOT_LEN);
        if (log_len == 0 || scan_offset + log_len > bytes_read) break;

        txn_id_t tid = *reinterpret_cast<const txn_id_t*>(log_data_.data() + scan_offset + OFFSET_LOG_TID);
        lsn_t lsn = *reinterpret_cast<const lsn_t*>(log_data_.data() + scan_offset + OFFSET_LSN);

        // 追踪最大事务ID（用于恢复后设置txn_id和时间戳）
        if (tid != INVALID_TXN_ID && (max_txn_id_ == INVALID_TXN_ID || tid > max_txn_id_)) {
            max_txn_id_ = tid;
        }

        switch (log_type) {
            case LogType::begin:
                // 事务开始：加入ATT
                att_[tid] = lsn;
                break;

            case LogType::commit:
                // 事务提交：从ATT移除
                att_.erase(tid);
                break;

            case LogType::ABORT:
                // 事务中止：从ATT移除，并标记为aborted（REDO时跳过其操作）
                att_.erase(tid);
                aborted_txns_.insert(tid);
                break;

            case LogType::INSERT: {
                // 更新ATT + 更新DPT
                att_[tid] = lsn;
                InsertLogRecord insert_rec;
                insert_rec.deserialize(log_data_.data() + scan_offset);
                // 若表文件已不存在（如被DROP），跳过该日志
                int fd = get_file_fd_safe(insert_rec.table_name_);
                if (fd < 0) break;
                PageId pid{fd, insert_rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) {
                    dpt_[pid] = lsn;  // 首次变脏
                } else {
                    dpt_[pid] = std::min(dpt_[pid], lsn);
                }
                break;
            }

            case LogType::DELETE: {
                att_[tid] = lsn;
                DeleteLogRecord delete_rec;
                delete_rec.deserialize(log_data_.data() + scan_offset);
                int fd = get_file_fd_safe(delete_rec.table_name_);
                if (fd < 0) break;
                PageId pid{fd, delete_rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) {
                    dpt_[pid] = lsn;
                }
                break;
            }

            case LogType::UPDATE: {
                att_[tid] = lsn;
                UpdateLogRecord update_rec;
                update_rec.deserialize(log_data_.data() + scan_offset);
                int fd = get_file_fd_safe(update_rec.table_name_);
                if (fd < 0) break;
                PageId pid{fd, update_rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) {
                    dpt_[pid] = lsn;
                }
                break;
            }

            case LogType::CHECKPOINT:
                // 跳过（已经在初始化时处理）
                break;
        }
        scan_offset += log_len;
    }

    // 5. 确定redo_lsn = DPT中最小LSN
    redo_lsn_ = INVALID_LSN;
    for (auto& [pid, lsn] : dpt_) {
        if (redo_lsn_ == INVALID_LSN || lsn < redo_lsn_) {
            redo_lsn_ = lsn;
        }
    }

    // 6. 打印恢复状态
    printf("[Recovery::Analyze] ATT size: %zu, DPT size: %zu, redo_lsn: %d\n",
           att_.size(), dpt_.size(), redo_lsn_);
}

/**
 * @description: 安全获取文件fd（表文件可能已被删除，例如前面的测试题目中DROP了表）
 * @param tab_name: 表名
 * @return fd值，若文件不存在则返回-1
 */
int RecoveryManager::get_file_fd_safe(const std::string& tab_name) {
    try {
        return disk_manager_->get_file_fd(tab_name);
    } catch (FileNotFoundError& e) {
        // 表文件已被删除（如DROP TABLE），跳过该日志条目
        printf("[Recovery] 跳过不存在的表: %s\n", tab_name.c_str());
        return -1;
    }
}

/**
 * @description: 确保指定表的指定页面在磁盘上存在
 *   crash后部分页面可能仅存在于buffer pool中未刷盘，
 *   在REDO读取这些页面前需先扩展文件
 * @return 若实际扩展了文件则返回true
 */
bool RecoveryManager::ensure_page_exists(const std::string& tab_name, int page_no) {
    int file_size = disk_manager_->get_file_size(tab_name);
    if (file_size < 0) return false;  // 文件不存在（不应该发生）
    int required_size = (page_no + 1) * PAGE_SIZE;
    if (file_size < required_size) {
        // 页面在磁盘上不存在：写入零页扩展文件
        int fd = get_file_fd_safe(tab_name);
        std::vector<char> zero_page(PAGE_SIZE, 0);
        disk_manager_->write_page(fd, page_no, zero_page.data(), PAGE_SIZE);
        printf("[Recovery::Redo] 扩展文件 %s page %d (文件大小 %d → %d)\n",
               tab_name.c_str(), page_no, file_size, required_size);
        return true;
    }
    return false;
}

/**
 * @description: ARIES redo阶段：
 *   从redo_lsn开始，重做所有日志操作（历史重做，不管事务是否提交）
 *   跳过page_lsn >= log_lsn的页面（已持久化）
 */
void RecoveryManager::redo() {
    if (redo_lsn_ == INVALID_LSN) {
        printf("[Recovery::Redo] Nothing to redo (no dirty pages found)\n");
        return;
    }

    // 找到redo_lsn在日志中的起始偏移
    int start_offset = -1;
    for (auto& [lsn, offset] : lsn_offsets_) {
        if (lsn == redo_lsn_) {
            start_offset = offset;
            break;
        }
    }
    if (start_offset < 0) {
        printf("[Recovery::Redo] Cannot find redo_lsn %d in log\n", redo_lsn_);
        return;
    }

    int offset = start_offset;
    int redo_count = 0;
    while (offset < static_cast<int>(log_data_.size())) {
        if (offset + LOG_HEADER_SIZE > static_cast<int>(log_data_.size())) break;

        LogType log_type = *reinterpret_cast<const LogType*>(log_data_.data() + offset);
        uint32_t log_len = *reinterpret_cast<const uint32_t*>(log_data_.data() + offset + OFFSET_LOG_TOT_LEN);
        lsn_t lsn = *reinterpret_cast<const lsn_t*>(log_data_.data() + offset + OFFSET_LSN);

        if (log_len == 0 || offset + log_len > static_cast<int>(log_data_.size())) break;

        // 读取事务ID，跳过已中止事务的操作
        txn_id_t tid = *reinterpret_cast<const txn_id_t*>(log_data_.data() + offset + OFFSET_LOG_TID);
        if (aborted_txns_.count(tid) > 0) {
            offset += log_len;
            continue;  // 跳过已中止事务的操作
        }

        switch (log_type) {
            case LogType::INSERT: {
                InsertLogRecord rec;
                rec.deserialize(log_data_.data() + offset);

                // 检查页面是否在DPT中
                int fd = get_file_fd_safe(rec.table_name_);
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) {
                    break;  // 页面不在DPT中，跳过
                }

                // 确保页面在磁盘上存在（crash后可能未刷盘）
                ensure_page_exists(rec.table_name_, rec.rid_.page_no);

                // 检查页面LSN
                auto fh = get_table_fh(rec.table_name_);
                // 更新文件头num_pages（扩展文件后需同步更新，确保扫描能遍历到所有页面）
                fh->update_num_pages(rec.rid_.page_no + 1);
                auto page_handle = fh->fetch_page_handle(rec.rid_.page_no);
                lsn_t page_lsn = page_handle.page->get_page_lsn();

                if (page_lsn < lsn) {
                    // 需要重做：写入记录
                    char* slot = page_handle.get_slot(rec.rid_.slot_no);
                    memcpy(slot, rec.insert_value_.data, rec.insert_value_.size);
                    Bitmap::set(page_handle.bitmap, rec.rid_.slot_no);
                    page_handle.page_hdr->num_records++;
                    page_handle.page->set_page_lsn(lsn);
                    buffer_pool_manager_->mark_dirty(page_handle.page);
                    redo_count++;
                }
                buffer_pool_manager_->unpin_page(pid, true);
                break;
            }

            case LogType::DELETE: {
                DeleteLogRecord rec;
                rec.deserialize(log_data_.data() + offset);

                int fd = get_file_fd_safe(rec.table_name_);
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) break;

                // 确保页面在磁盘上存在（crash后可能未刷盘）
                ensure_page_exists(rec.table_name_, rec.rid_.page_no);

                auto fh = get_table_fh(rec.table_name_);
                // 更新文件头num_pages（扩展文件后需同步更新，确保扫描能遍历到所有页面）
                fh->update_num_pages(rec.rid_.page_no + 1);
                auto page_handle = fh->fetch_page_handle(rec.rid_.page_no);
                lsn_t page_lsn = page_handle.page->get_page_lsn();

                if (page_lsn < lsn) {
                    // 需要重做：软删除（设置xmax为记录中保存的值）
                    char* slot = page_handle.get_slot(rec.rid_.slot_no);
                    // 旧记录的xmax已经设置，直接写回完整记录
                    memcpy(slot, rec.deleted_record_.data, rec.deleted_record_.size);
                    page_handle.page->set_page_lsn(lsn);
                    buffer_pool_manager_->mark_dirty(page_handle.page);
                    redo_count++;
                }
                buffer_pool_manager_->unpin_page(pid, true);
                break;
            }

            case LogType::UPDATE: {
                UpdateLogRecord rec;
                rec.deserialize(log_data_.data() + offset);

                int fd = get_file_fd_safe(rec.table_name_);
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) break;

                // 确保页面在磁盘上存在（crash后可能未刷盘）
                ensure_page_exists(rec.table_name_, rec.rid_.page_no);

                auto fh = get_table_fh(rec.table_name_);
                // 更新文件头num_pages（扩展文件后需同步更新，确保扫描能遍历到所有页面）
                fh->update_num_pages(rec.rid_.page_no + 1);
                auto page_handle = fh->fetch_page_handle(rec.rid_.page_no);
                lsn_t page_lsn = page_handle.page->get_page_lsn();

                if (page_lsn < lsn) {
                    // 需要重做：写回新记录
                    char* slot = page_handle.get_slot(rec.rid_.slot_no);
                    memcpy(slot, rec.new_record_.data, rec.new_record_.size);
                    page_handle.page->set_page_lsn(lsn);
                    buffer_pool_manager_->mark_dirty(page_handle.page);
                    redo_count++;
                }
                buffer_pool_manager_->unpin_page(pid, true);
                break;
            }

            default:
                break;
        }
        offset += log_len;
    }

    printf("[Recovery::Redo] Redone %d operations\n", redo_count);
}

/**
 * @description: ARIES undo阶段：
 *   回滚所有ATT中的活跃事务（loser transactions）
 *   每次选择ATT中LSN最大的日志记录进行undo，沿prev_lsn链逆向回滚
 */
void RecoveryManager::undo() {
    if (att_.empty()) {
        printf("[Recovery::Undo] No active transactions to undo\n");
        return;
    }

    int undo_count = 0;

    // 构建LSN到日志偏移的映射（用于快速定位）
    std::unordered_map<lsn_t, int> lsn_to_offset;
    for (auto& [lsn, offset] : lsn_offsets_) {
        lsn_to_offset[lsn] = offset;
    }

    while (!att_.empty()) {
        // 1. 找到ATT中最大的LSN
        lsn_t max_lsn = INVALID_LSN;
        txn_id_t victim_tid = INVALID_TXN_ID;
        for (auto& [tid, lsn] : att_) {
            if (lsn > max_lsn) {
                max_lsn = lsn;
                victim_tid = tid;
            }
        }

        if (max_lsn == INVALID_LSN || lsn_to_offset.find(max_lsn) == lsn_to_offset.end()) {
            // 无法定位日志，从ATT中移除
            att_.erase(victim_tid);
            continue;
        }

        // 2. 读取该日志记录
        int offset = lsn_to_offset[max_lsn];
        auto record = parse_log_record(log_data_.data() + offset);
        if (record == nullptr) {
            att_.erase(victim_tid);
            continue;
        }

        // 3. 执行补偿操作（undo）
        switch (record->log_type_) {
            case LogType::INSERT: {
                auto* insert_rec = dynamic_cast<InsertLogRecord*>(record.get());
                if (insert_rec) {
                    // 撤销插入：物理删除记录
                    ensure_page_exists(insert_rec->table_name_, insert_rec->rid_.page_no);
                    auto fh = get_table_fh(insert_rec->table_name_);
                    // 检查记录是否还存在（可能已被其他操作删除）
                    if (fh->is_record(insert_rec->rid_)) {
                        fh->delete_record(insert_rec->rid_, nullptr);
                    }
                    undo_count++;
                }
                break;
            }

            case LogType::DELETE: {
                auto* delete_rec = dynamic_cast<DeleteLogRecord*>(record.get());
                if (delete_rec) {
                    // 撤销删除：恢复原记录（写回旧记录的完整slot，包括旧xmin/xmax）
                    ensure_page_exists(delete_rec->table_name_, delete_rec->rid_.page_no);
                    auto fh = get_table_fh(delete_rec->table_name_);
                    if (fh->is_record(delete_rec->rid_)) {
                        // 记录还存在（软删除场景，bitmap未清除），写回旧数据恢复xmax
                        fh->update_record(delete_rec->rid_, delete_rec->deleted_record_.data, nullptr);
                    } else {
                        // 记录不存在（物理删除场景），重新插入
                        fh->insert_record(delete_rec->rid_, delete_rec->deleted_record_.data);
                    }
                    undo_count++;
                }
                break;
            }

            case LogType::UPDATE: {
                auto* update_rec = dynamic_cast<UpdateLogRecord*>(record.get());
                if (update_rec) {
                    // 撤销更新：写回旧记录
                    ensure_page_exists(update_rec->table_name_, update_rec->rid_.page_no);
                    auto fh = get_table_fh(update_rec->table_name_);
                    if (fh->is_record(update_rec->rid_)) {
                        fh->update_record(update_rec->rid_, update_rec->old_record_.data, nullptr);
                    }
                    undo_count++;
                }
                break;
            }

            case LogType::begin:
                // 回退到BEGIN记录，该事务完成undo
                break;

            default:
                break;
        }

        // 4. 沿着prev_lsn链回退
        lsn_t prev_lsn = record->prev_lsn_;
        if (prev_lsn == INVALID_LSN) {
            // 已到达事务的第一条日志（BEGIN），从ATT中移除
            att_.erase(victim_tid);
        } else {
            // 更新事务的最后LSN为prev_lsn，继续回退
            att_[victim_tid] = prev_lsn;
        }
    }

    printf("[Recovery::Undo] Undone %d operations\n", undo_count);
}
