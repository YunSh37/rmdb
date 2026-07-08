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
 * @brief ARIES 三阶段故障恢复实现
 *
 * 恢复流程（在 rmdb.cpp 的 main() 中调用）：
 *   1. analyze() — 扫描日志，构建 ATT（活跃事务表）和 DPT（脏页表）
 *   2. redo()    — 从 redo_lsn 开始重放所有已提交修改
 *   3. undo()    — 逆序回滚所有未完成事务
 *
 * ABORT 竞态条件处理：
 *   abort() 先刷盘 undo 数据（flush + fsync），再写 ABORT 日志。
 *   因此 ABORT 日志存在 → undo 已持久化 → 恢复可安全跳过。
 *   若 ABORT 日志丢失（崩溃在刷盘后、写 ABORT 日志前），事务仍在 ATT，
 *   UNDO 阶段会幂等地再次回滚。
 *
 * 静态检查点：
 *   创建时全量刷盘 + fsync，保证检查点 LSN 之前的数据已持久化。
 *   REDO 从检查点 LSN 之后开始，跳过早于检查点的日志。
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
        case LogType::begin:            record = std::make_unique<BeginLogRecord>(); break;
        case LogType::commit:           record = std::make_unique<CommitLogRecord>(); break;
        case LogType::ABORT:            record = std::make_unique<AbortLogRecord>(); break;
        case LogType::INSERT:           record = std::make_unique<InsertLogRecord>(); break;
        case LogType::DELETE:           record = std::make_unique<DeleteLogRecord>(); break;
        case LogType::UPDATE:           record = std::make_unique<UpdateLogRecord>(); break;
        case LogType::CHECKPOINT:       record = std::make_unique<CheckpointLogRecord>(); break;
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
    // 表文件可能尚未打开，尝试打开
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

/** 确保指定页面在磁盘上存在（若不存在则写入零页扩展文件）
 *  @return 操作是否成功 */
bool RecoveryManager::ensure_page_exists(const std::string& tab_name, int page_no) {
    int file_size = disk_manager_->get_file_size(tab_name);
    if (file_size < 0) return false;
    int required_size = (page_no + 1) * PAGE_SIZE;
    if (file_size < required_size) {
        int fd = get_file_fd_safe(tab_name);
        if (fd < 0) return false;
        std::vector<char> zero_page(PAGE_SIZE, 0);
        disk_manager_->write_page(fd, page_no, zero_page.data(), PAGE_SIZE);
        printf("[Recovery::Redo] 扩展文件 %s 第%d页\n", tab_name.c_str(), page_no);
    }
    return true;
}

/* ================================================================
 * 阶段一：Analyze — 分析日志，构建恢复状态
 *
 * 产出：
 *   - att_ (Active Transaction Table): 未完成事务及其最后一条日志的 LSN
 *   - dpt_ (Dirty Page Table): 可能需要 REDO 的脏页及其首次变脏的 LSN
 *   - redo_lsn_: REDO 起始 LSN
 *   - checkpoint_lsn_: 若存在检查点则记录其 LSN
 *   - max_txn_id_: 日志中出现的最大事务 ID
 * ================================================================ */
void RecoveryManager::analyze() {
    // 读取整个日志文件到内存
    int file_size = disk_manager_->get_file_size(LOG_FILE_NAME);
    if (file_size <= 0) {
        printf("[Recovery::Analyze] 日志文件为空或不存在，跳过恢复\n");
        redo_lsn_ = INVALID_LSN;
        return;
    }

    log_data_.resize(file_size);
    int bytes_read = disk_manager_->read_log(log_data_.data(), file_size, 0);
    if (bytes_read <= 0) {
        printf("[Recovery::Analyze] 日志文件读取失败，跳过恢复\n");
        redo_lsn_ = INVALID_LSN;
        return;
    }

    // ================================================================
    // 阶段 1.1：快速头部扫描（不反序列化记录体）
    // 目的：建立 LSN→偏移 映射，定位最后一个检查点，跟踪最大事务ID
    // ================================================================
    int scan_offset = 0;
    int last_checkpoint_offset = -1;
    int total_records = 0;

    while (scan_offset < bytes_read) {
        // 边界检查：至少需要 LOG_HEADER_SIZE 字节
        if (scan_offset + LOG_HEADER_SIZE > bytes_read) break;

        LogType log_type = *reinterpret_cast<const LogType*>(
            log_data_.data() + scan_offset);
        uint32_t log_len = *reinterpret_cast<const uint32_t*>(
            log_data_.data() + scan_offset + OFFSET_LOG_TOT_LEN);
        lsn_t lsn = *reinterpret_cast<const lsn_t*>(
            log_data_.data() + scan_offset + OFFSET_LSN);

        // 有效性检查
        if (log_len == 0 || log_len > bytes_read ||
            scan_offset + static_cast<int>(log_len) > bytes_read) {
            printf("[Recovery::Analyze] 警告: 偏移 %d 处日志长度 %u 无效，停止扫描\n",
                   scan_offset, log_len);
            break;
        }

        // 建立 LSN→偏移 映射
        lsn_offsets_.emplace_back(lsn, scan_offset);

        // 跟踪最大事务 ID
        txn_id_t tid = *reinterpret_cast<const txn_id_t*>(
            log_data_.data() + scan_offset + OFFSET_LOG_TID);
        if (tid != INVALID_TXN_ID &&
            (max_txn_id_ == INVALID_TXN_ID || tid > max_txn_id_)) {
            max_txn_id_ = tid;
        }

        // 记录最后一个检查点的位置
        if (log_type == LogType::CHECKPOINT) {
            last_checkpoint_offset = scan_offset;
        }

        scan_offset += static_cast<int>(log_len);
        total_records++;
    }

    // ================================================================
    // 阶段 1.2：解析检查点并初始化恢复状态
    //
    // 静态检查点语义：
    //   创建检查点时已执行全量刷盘（flush_all_pages + fsync），
    //   因此检查点 LSN 之前的所有数据修改都已持久化到磁盘。
    //
    // 利用检查点数据：
    //   - ATT：从检查点快照恢复活跃事务状态
    //   - DPT：从检查点快照恢复脏页状态（表名→fd 转换）
    //   - REDO 起点：检查点 LSN 之后的第一个操作（跳过已持久化的部分）
    // ================================================================
    lsn_t ckpt_lsn = INVALID_LSN;
    int post_ckpt_offset = 0;  // 检查点之后第一条记录的文件偏移

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

        // 从检查点 DPT 初始化脏页表
        // 检查点 DPT 使用 (表名, 页号, lsn) 格式，需转换为 (fd, 页号) 格式
        for (auto& [tab_name, page_no, lsn] : ckpt->dpt_entries_) {
            int fd = get_file_fd_safe(tab_name);
            if (fd < 0) continue;  // 表文件已不存在，跳过
            PageId pid{fd, page_no};
            // 保留最小的首次变脏 LSN
            if (dpt_.find(pid) == dpt_.end() || lsn < dpt_[pid]) {
                dpt_[pid] = lsn;
            }
        }

        // 确定 REDO 起始 LSN：检查点之后第一条日志的 LSN
        // 静态检查点保证之前的数据已全量刷盘，无需重做
        redo_lsn_ = INVALID_LSN;
        for (auto& [lsn, offset] : lsn_offsets_) {
            if (lsn > ckpt_lsn) {
                redo_lsn_ = lsn;
                break;
            }
        }
    }

    // ================================================================
    // 阶段 1.3：增量扫描（从检查点之后或日志开头开始）
    //
    // 处理逻辑（按日志类型）：
    //   BEGIN:   事务开始 → 加入 ATT
    //   COMMIT:  事务提交 → 从 ATT 移除
    //   ABORT:   保留在 ATT（保守策略，UNDO 阶段幂等回滚）
    //            — 因为 abort() 先刷盘再写日志，ABORT 日志存在=undo已持久化
    //   DML操作:  更新事务在 ATT 中的最后 LSN，将涉及页面加入 DPT
    // ================================================================
    int start_offset = (last_checkpoint_offset >= 0) ? post_ckpt_offset : 0;

    while (start_offset < bytes_read) {
        // 边界检查
        if (start_offset + LOG_HEADER_SIZE > bytes_read) break;

        LogType log_type = *reinterpret_cast<const LogType*>(
            log_data_.data() + start_offset);
        uint32_t log_len = *reinterpret_cast<const uint32_t*>(
            log_data_.data() + start_offset + OFFSET_LOG_TOT_LEN);
        lsn_t lsn = *reinterpret_cast<const lsn_t*>(
            log_data_.data() + start_offset + OFFSET_LSN);
        txn_id_t tid = *reinterpret_cast<const txn_id_t*>(
            log_data_.data() + start_offset + OFFSET_LOG_TID);

        if (log_len == 0 ||
            start_offset + static_cast<int>(log_len) > bytes_read) break;

        switch (log_type) {
            case LogType::begin: {
                // 新事务开始 — 加入 ATT
                att_[tid] = lsn;
                break;
            }
            case LogType::commit: {
                // 事务已提交 — 从 ATT 移除
                att_.erase(tid);
                break;
            }
            case LogType::ABORT:
                // ABORT 日志：事务保留在 ATT 中。
                // 设计理由：abort() 先刷盘 undo 数据再写 ABORT 日志。
                // 因此 ABORT 日志存在于磁盘 → undo 数据已持久化 →
                // UNDO 阶段将幂等回滚（安全无副作用）。
                // 若崩溃发生在刷盘后、写 ABORT 日志前，ABORT 日志丢失，
                // 但事务仍在 ATT 中 → UNDO 阶段正确回滚。
                break;

            case LogType::CHECKPOINT:
                // 已在阶段 1.2 处理，跳过
                break;

            case LogType::INSERT: {
                // 插入操作 — 更新 ATT 和 DPT
                att_[tid] = lsn;
                InsertLogRecord rec;
                rec.deserialize(log_data_.data() + start_offset);
                int fd = get_file_fd_safe(rec.table_name_);
                if (fd < 0) break;
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) {
                    dpt_[pid] = lsn;  // 首次变脏
                } else {
                    dpt_[pid] = std::min(dpt_[pid], lsn);  // 保留最早的 LSN
                }
                break;
            }
            case LogType::DELETE: {
                // 删除操作 — 更新 ATT 和 DPT
                att_[tid] = lsn;
                DeleteLogRecord rec;
                rec.deserialize(log_data_.data() + start_offset);
                int fd = get_file_fd_safe(rec.table_name_);
                if (fd < 0) break;
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) {
                    dpt_[pid] = lsn;
                }
                // DELETE 不更新已存在的 DPT 条目（保留最早变脏的 LSN）
                break;
            }
            case LogType::UPDATE: {
                // 更新操作 — 更新 ATT 和 DPT
                att_[tid] = lsn;
                UpdateLogRecord rec;
                rec.deserialize(log_data_.data() + start_offset);
                int fd = get_file_fd_safe(rec.table_name_);
                if (fd < 0) break;
                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) {
                    dpt_[pid] = lsn;
                }
                break;
            }
            case LogType::INDEX_PAGE_MODIFY: {
                // 索引页物理修改 — 更新 ATT 和 DPT
                att_[tid] = lsn;
                IndexPageModifyLogRecord rec;
                rec.deserialize(log_data_.data() + start_offset);
                int fd = get_file_fd_safe(rec.index_name_);
                if (fd < 0) break;
                PageId pid{fd, rec.page_no_};
                if (dpt_.find(pid) == dpt_.end()) {
                    dpt_[pid] = lsn;
                } else {
                    dpt_[pid] = std::min(dpt_[pid], lsn);
                }
                break;
            }
        }
        start_offset += static_cast<int>(log_len);
    }

    // 无检查点时，从 DPT 最小 LSN 开始 REDO（标准 ARIES 行为）
    if (last_checkpoint_offset < 0) {
        redo_lsn_ = INVALID_LSN;
        for (auto& [pid, lsn] : dpt_) {
            if (redo_lsn_ == INVALID_LSN || lsn < redo_lsn_) {
                redo_lsn_ = lsn;
            }
        }
    }

    printf("[Recovery::Analyze] 记录=%d条 ATT=%zu DPT=%zu redo_lsn=%d ckpt=%d max_txn=%d\n",
           total_records, att_.size(), dpt_.size(), redo_lsn_, checkpoint_lsn_, max_txn_id_);
}

/* ================================================================
 * 阶段二：Redo — 重放历史操作
 *
 * 从 redo_lsn_ 开始，依次重放所有 INSERT/DELETE/UPDATE/INDEX_PAGE_MODIFY
 * 日志记录。通过比较页面 LSN 与日志 LSN 实现幂等性：
 *   - 若 page.LSN >= log.LSN：数据已持久化，跳过
 *   - 若 page.LSN < log.LSN：数据未持久化，应用修改
 *
 * Redo 阶段不区分事务是否提交 — 所有修改都需要重做，
 * 未提交事务的修改会在 UNDO 阶段回滚。
 * ================================================================ */
void RecoveryManager::redo() {
    if (redo_lsn_ == INVALID_LSN) {
        printf("[Recovery::Redo] 无需重做（redo_lsn 无效）\n");
        return;
    }

    // 定位 redo_lsn_ 对应的文件偏移
    int start_offset = -1;
    for (auto& [lsn, offset] : lsn_offsets_) {
        if (lsn == redo_lsn_) {
            start_offset = offset;
            break;
        }
    }
    if (start_offset < 0) {
        printf("[Recovery::Redo] 未找到 redo_lsn=%d 对应的日志记录\n", redo_lsn_);
        return;
    }

    int offset = start_offset;
    int redo_count = 0;
    int total_count = 0;
    int bytes_total = static_cast<int>(log_data_.size());

    while (offset < bytes_total) {
        // 边界检查
        if (offset + LOG_HEADER_SIZE > bytes_total) break;

        LogType log_type = *reinterpret_cast<const LogType*>(
            log_data_.data() + offset);
        uint32_t log_len = *reinterpret_cast<const uint32_t*>(
            log_data_.data() + offset + OFFSET_LOG_TOT_LEN);
        lsn_t lsn = *reinterpret_cast<const lsn_t*>(
            log_data_.data() + offset + OFFSET_LSN);

        if (log_len == 0 || offset + static_cast<int>(log_len) > bytes_total) break;

        total_count++;

        switch (log_type) {
            // -------------------------------------------------------
            // INSERT Redo：将插入的记录写回对应 slot
            // -------------------------------------------------------
            case LogType::INSERT: {
                InsertLogRecord rec;
                rec.deserialize(log_data_.data() + offset);

                int fd = get_file_fd_safe(rec.table_name_);
                if (fd < 0) break;  // 表文件已删除

                PageId pid{fd, rec.rid_.page_no};

                // DPT 检查：页面不在 DPT 中说明不需要 REDO
                if (dpt_.find(pid) == dpt_.end()) break;

                // 确保页面在磁盘上存在（可能因文件未扩展而缺失）
                if (!ensure_page_exists(rec.table_name_, rec.rid_.page_no)) break;

                auto fh = get_table_fh(rec.table_name_);
                // 更新文件头中的 num_pages（确保 >= rid 所在页号 + 1）
                fh->update_num_pages(rec.rid_.page_no + 1);

                auto ph = fh->fetch_page_handle(rec.rid_.page_no);
                if (ph.page->get_page_lsn() < lsn) {
                    // 页面 LSN 较小 → 数据未持久化，应用 REDO
                    memcpy(ph.get_slot(rec.rid_.slot_no),
                           rec.insert_value_.data, rec.insert_value_.size);
                    Bitmap::set(ph.bitmap, rec.rid_.slot_no);
                    ph.page_hdr->num_records++;
                    ph.page->set_page_lsn(lsn);
                    buffer_pool_manager_->mark_dirty(ph.page);
                    redo_count++;
                }
                buffer_pool_manager_->unpin_page(pid, true);
                break;
            }

            // -------------------------------------------------------
            // DELETE Redo：恢复软删除状态（设置 MVCC 头的 xmax_）
            // -------------------------------------------------------
            case LogType::DELETE: {
                DeleteLogRecord rec;
                rec.deserialize(log_data_.data() + offset);

                int fd = get_file_fd_safe(rec.table_name_);
                if (fd < 0) break;

                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) break;
                if (!ensure_page_exists(rec.table_name_, rec.rid_.page_no)) break;

                auto fh = get_table_fh(rec.table_name_);
                fh->update_num_pages(rec.rid_.page_no + 1);

                auto ph = fh->fetch_page_handle(rec.rid_.page_no);
                if (ph.page->get_page_lsn() < lsn) {
                    // 写入删除前的完整记录（含 MVCC 头）
                    memcpy(ph.get_slot(rec.rid_.slot_no),
                           rec.deleted_record_.data, rec.deleted_record_.size);

                    // 关键：设置 xmax_ 为删除时的时间戳，恢复软删除状态
                    // deleted_record_ 中的 xmax 是 INT32_MAX（未删除），
                    // REDO 需要覆盖为实际的删除时间戳
                    char* slot = ph.get_slot(rec.rid_.slot_no);
                    int user_size = fh->get_user_record_size();
                    MvccHeader* hdr = reinterpret_cast<MvccHeader*>(slot + user_size);
                    hdr->xmax_ = rec.xmax_;

                    ph.page->set_page_lsn(lsn);
                    buffer_pool_manager_->mark_dirty(ph.page);
                    redo_count++;
                }
                buffer_pool_manager_->unpin_page(pid, true);
                break;
            }

            // -------------------------------------------------------
            // UPDATE Redo：将新记录写回对应 slot
            // -------------------------------------------------------
            case LogType::UPDATE: {
                UpdateLogRecord rec;
                rec.deserialize(log_data_.data() + offset);

                int fd = get_file_fd_safe(rec.table_name_);
                if (fd < 0) break;

                PageId pid{fd, rec.rid_.page_no};
                if (dpt_.find(pid) == dpt_.end()) break;
                if (!ensure_page_exists(rec.table_name_, rec.rid_.page_no)) break;

                auto fh = get_table_fh(rec.table_name_);
                fh->update_num_pages(rec.rid_.page_no + 1);

                auto ph = fh->fetch_page_handle(rec.rid_.page_no);
                if (ph.page->get_page_lsn() < lsn) {
                    // 写入更新后的完整记录（含 MVCC 头）
                    memcpy(ph.get_slot(rec.rid_.slot_no),
                           rec.new_record_.data, rec.new_record_.size);
                    ph.page->set_page_lsn(lsn);
                    buffer_pool_manager_->mark_dirty(ph.page);
                    redo_count++;
                }
                buffer_pool_manager_->unpin_page(pid, true);
                break;
            }

            // -------------------------------------------------------
            // INDEX_PAGE_MODIFY Redo：应用索引页的后镜像
            // -------------------------------------------------------
            case LogType::INDEX_PAGE_MODIFY: {
                IndexPageModifyLogRecord rec;
                rec.deserialize(log_data_.data() + offset);

                int fd = get_file_fd_safe(rec.index_name_);
                if (fd < 0) break;

                PageId pid{fd, rec.page_no_};
                if (dpt_.find(pid) == dpt_.end()) break;
                if (!ensure_page_exists(rec.index_name_, rec.page_no_)) break;

                Page* page = buffer_pool_manager_->fetch_page(pid);
                if (page == nullptr) break;

                if (page->get_page_lsn() < lsn) {
                    // 应用后镜像（PAGE_SIZE 整页覆盖）
                    memcpy(page->get_data(), rec.after_image_, PAGE_SIZE);
                    page->set_page_lsn(lsn);
                    buffer_pool_manager_->mark_dirty(page);
                    redo_count++;
                }
                buffer_pool_manager_->unpin_page(pid, true);
                break;
            }

            // BEGIN/COMMIT/ABORT/CHECKPOINT：数据恢复不需要处理
            default:
                break;
        }

        offset += static_cast<int>(log_len);
    }

    printf("[Recovery::Redo] 扫描 %d 条记录，应用了 %d 条 REDO\n",
           total_count, redo_count);
}

/* ================================================================
 * 阶段三：Undo — 回滚未完成事务
 *
 * 对 ATT 中的每个未完成事务，沿 prev_lsn_ 链逆向遍历其所有日志，
 * 对每条操作日志执行逆操作（补偿操作）。
 *
 * TOPO 策略（Total Ordering of Pending Operations）：
 *   优先处理 LSN 最大的事务，避免重复回滚同一条记录。
 *
 * 逆操作映射：
 *   INSERT → 物理删除记录
 *   DELETE → 恢复记录（含完整数据 + MVCC 头）
 *   UPDATE → 写回旧记录
 *   INDEX_PAGE_MODIFY → 应用前镜像
 *
 * 幂等性：
 *   所有 UNDO 操作都是幂等的 — 多次重复执行结果相同。
 *   这是处理 ABORT 竞态条件的关键保障。
 * ================================================================ */
void RecoveryManager::undo() {
    if (att_.empty()) {
        printf("[Recovery::Undo] ATT 为空，无需回滚\n");
        return;
    }

    // 构建 LSN→偏移 快速查找表
    std::unordered_map<lsn_t, int> lsn_to_offset;
    for (auto& [lsn, offset] : lsn_offsets_) {
        lsn_to_offset[lsn] = offset;
    }

    int undo_count = 0;

    while (!att_.empty()) {
        // TOPO：选择 ATT 中 LSN 最大的事务
        lsn_t max_lsn = INVALID_LSN;
        txn_id_t victim_tid = INVALID_TXN_ID;

        for (auto& [tid, lsn] : att_) {
            if (lsn > max_lsn) {
                max_lsn = lsn;
                victim_tid = tid;
            }
        }

        // 安全检查
        if (max_lsn == INVALID_LSN ||
            lsn_to_offset.find(max_lsn) == lsn_to_offset.end()) {
            att_.erase(victim_tid);
            continue;
        }

        // 找到并反序列化日志记录
        int offset = lsn_to_offset[max_lsn];
        auto record = parse_log_record(log_data_.data() + offset);
        if (!record) {
            att_.erase(victim_tid);
            continue;
        }

        switch (record->log_type_) {
            // -----------------------------------------------------------
            // INSERT 的 UNDO：删除插入的记录
            // -----------------------------------------------------------
            case LogType::INSERT: {
                auto* r = dynamic_cast<InsertLogRecord*>(record.get());
                if (r) {
                    if (!ensure_page_exists(r->table_name_, r->rid_.page_no)) break;
                    auto fh = get_table_fh(r->table_name_);
                    // 幂等性：仅当记录存在时才删除
                    if (fh->is_record(r->rid_)) {
                        fh->delete_record(r->rid_, nullptr);
                        undo_count++;
                    }
                }
                break;
            }

            // -----------------------------------------------------------
            // DELETE 的 UNDO：恢复被删除的记录
            // -----------------------------------------------------------
            case LogType::DELETE: {
                auto* r = dynamic_cast<DeleteLogRecord*>(record.get());
                if (r) {
                    if (!ensure_page_exists(r->table_name_, r->rid_.page_no)) break;
                    auto fh = get_table_fh(r->table_name_);

                    if (fh->is_record(r->rid_)) {
                        // 原 slot 仍存在（软删除保留 bitmap），
                        // 直接写回旧记录（恢复 xmax=INT32_MAX）
                        fh->update_record(r->rid_, r->deleted_record_.data, nullptr);
                    } else {
                        // 原 slot 已被物理删除或回收，重新插入
                        fh->insert_record(r->rid_, r->deleted_record_.data);
                    }
                    undo_count++;
                }
                break;
            }

            // -----------------------------------------------------------
            // UPDATE 的 UNDO：写回旧记录
            // -----------------------------------------------------------
            case LogType::UPDATE: {
                auto* r = dynamic_cast<UpdateLogRecord*>(record.get());
                if (r) {
                    if (!ensure_page_exists(r->table_name_, r->rid_.page_no)) break;
                    auto fh = get_table_fh(r->table_name_);
                    if (fh->is_record(r->rid_)) {
                        fh->update_record(r->rid_, r->old_record_.data, nullptr);
                        undo_count++;
                    }
                }
                break;
            }

            // -----------------------------------------------------------
            // INDEX_PAGE_MODIFY 的 UNDO：应用索引页前镜像
            // -----------------------------------------------------------
            case LogType::INDEX_PAGE_MODIFY: {
                auto* r = dynamic_cast<IndexPageModifyLogRecord*>(record.get());
                if (r) {
                    int fd = get_file_fd_safe(r->index_name_);
                    if (fd >= 0) {
                        if (!ensure_page_exists(r->index_name_, r->page_no_)) break;
                        PageId pid{fd, r->page_no_};
                        Page* page = buffer_pool_manager_->fetch_page(pid);
                        if (page != nullptr) {
                            // 应用前镜像回滚到修改前的状态
                            memcpy(page->get_data(), r->before_image_, PAGE_SIZE);
                            buffer_pool_manager_->mark_dirty(page);
                            buffer_pool_manager_->unpin_page(pid, true);
                            undo_count++;
                        }
                    }
                }
                break;
            }

            // BEGIN/COMMIT/ABORT/CHECKPOINT：无需数据操作
            default:
                break;
        }

        // 沿 prev_lsn_ 链回溯
        lsn_t prev_lsn = record->prev_lsn_;
        if (prev_lsn == INVALID_LSN) {
            // 到达事务的第一条日志（BEGIN），从 ATT 移除
            att_.erase(victim_tid);
        } else {
            // 继续回溯上一条日志
            att_[victim_tid] = prev_lsn;
        }
    }

    printf("[Recovery::Undo] 应用了 %d 条 UNDO\n", undo_count);
}
