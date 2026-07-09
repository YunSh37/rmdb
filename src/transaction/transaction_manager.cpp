/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "transaction_manager.h"
#include "record/rm_file_handle.h"
#include "system/sm_manager.h"
#include <unordered_set>

std::unordered_map<txn_id_t, Transaction *> TransactionManager::txn_map = {};

/** 从记录中构建索引键（将索引中每一列的值拼接） */
static void build_index_key(const RmRecord& rec, const IndexMeta& index, char* key_buf) {
    int offset = 0;
    for (int j = 0; j < index.col_num; ++j) {
        memcpy(key_buf + offset, rec.data + index.cols[j].offset, index.cols[j].len);
        offset += index.cols[j].len;
    }
}

/** 获取索引名称并确保已打开 */
static std::string ensure_index_open(SmManager* sm, const std::string& tab_name,
                                      const std::vector<ColMeta>& index_cols) {
    std::string ix_name = sm->get_ix_manager()->get_index_name(tab_name, index_cols);
    if (sm->ihs_.count(ix_name) == 0) {
        sm->ihs_.emplace(ix_name, sm->get_ix_manager()->open_index(tab_name, index_cols));
    }
    return ix_name;
}

/** 回滚 INSERT：删除数据记录及所有索引条目 */
static void undo_insert(SmManager* sm, const std::string& tab_name, const Rid& rid) {
    auto& tab = sm->db_.get_table(tab_name);
    auto fh = sm->fhs_.at(tab_name).get();

    // 读取记录以构建索引键
    auto rec = fh->get_record(rid, nullptr);

    // 删除所有索引条目
    for (auto& index : tab.indexes) {
        std::string ix_name = ensure_index_open(sm, tab_name, index.cols);
        auto ih = sm->ihs_.at(ix_name).get();
        char* key = new char[index.col_tot_len];
        build_index_key(*rec, index, key);
        ih->delete_entry(key, nullptr);
        delete[] key;
    }

    // 删除数据记录
    fh->delete_record(rid, nullptr);
}

/** 回滚 DELETE：恢复记录的xmax为MAX（撤销软删除），重建索引条目 */
static void undo_delete(SmManager* sm, const std::string& tab_name,
                        const RmRecord& old_record, const Rid& old_rid) {
    auto& tab = sm->db_.get_table(tab_name);
    auto fh = sm->fhs_.at(tab_name).get();

    // 方法1：如果原slot还存在（软删除未清除bitmap），直接恢复xmax和索引
    // 尝试在原rid上写回完整数据（含MVCC头，恢复xmax=MAX）
    if (fh->is_record(old_rid)) {
        // 原slot还在（软删除保留bitmap），写回old_record恢复MVCC头
        fh->update_record(old_rid, old_record.data, nullptr);
    } else {
        // 原slot已被物理删除（物理删除场景），重新插入
        RmRecord rec_copy(old_record.size);
        memcpy(rec_copy.data, old_record.data, old_record.size);
        Rid new_rid = fh->insert_record(rec_copy.data, nullptr);
        // 重建索引（使用new_rid）
        for (auto& index : tab.indexes) {
            std::string ix_name = ensure_index_open(sm, tab_name, index.cols);
            auto ih = sm->ihs_.at(ix_name).get();
            char* key = new char[index.col_tot_len];
            build_index_key(old_record, index, key);
            ih->insert_entry(key, new_rid, nullptr);
            delete[] key;
        }
        return;
    }

    // 重建所有索引条目（指向原rid）
    for (auto& index : tab.indexes) {
        std::string ix_name = ensure_index_open(sm, tab_name, index.cols);
        auto ih = sm->ihs_.at(ix_name).get();
        char* key = new char[index.col_tot_len];
        build_index_key(old_record, index, key);
        ih->insert_entry(key, old_rid, nullptr);
        delete[] key;
    }
}

/** 回滚 UPDATE：写回旧记录并恢复索引 */
static void undo_update(SmManager* sm, const std::string& tab_name,
                        const RmRecord& old_record, const Rid& rid) {
    auto& tab = sm->db_.get_table(tab_name);
    auto fh = sm->fhs_.at(tab_name).get();

    // 读取当前记录（修改后的）以获取当前索引键
    auto current_rec = fh->get_record(rid, nullptr);

    // 对每个索引，删除当前键并恢复旧键
    for (auto& index : tab.indexes) {
        std::string ix_name = ensure_index_open(sm, tab_name, index.cols);
        auto ih = sm->ihs_.at(ix_name).get();

        // 删除当前（修改后的）索引键
        char* cur_key = new char[index.col_tot_len];
        build_index_key(*current_rec, index, cur_key);

        // 构建旧索引键
        char* old_key = new char[index.col_tot_len];
        build_index_key(old_record, index, old_key);

        // 只有当键值发生改变时才需要维护索引
        if (memcmp(cur_key, old_key, index.col_tot_len) != 0) {
            ih->delete_entry(cur_key, nullptr);
            ih->insert_entry(old_key, rid, nullptr);
        }

        delete[] cur_key;
        delete[] old_key;
    }

    // 写回旧记录
    fh->update_record(rid, old_record.data, nullptr);
}

/**
 * @description: 事务的开始方法
 * @return {Transaction*} 开始事务的指针
 * @param {Transaction*} txn 事务指针，空指针代表需要创建新事务，否则开始已有事务
 * @param {LogManager*} log_manager 日志管理器指针
 */
Transaction * TransactionManager::begin(Transaction* txn, LogManager* log_manager) {
    // 1. 判断传入事务参数是否为空指针
    if (txn == nullptr) {
        // 2. 为空指针，创建新事务，分配新事务ID和时间戳
        txn_id_t txn_id = next_txn_id_.fetch_add(1);
        txn = new Transaction(txn_id);
        txn->set_start_ts(next_timestamp_.fetch_add(1));
    }
    // 3. 把事务加入到全局事务表中
    std::unique_lock<std::mutex> lock(latch_);
    TransactionManager::txn_map[txn->get_transaction_id()] = txn;
    lock.unlock();

    // 4. 设置事务状态为 GROWING（两阶段封锁的锁获取阶段）
    txn->set_state(TransactionState::GROWING);

    // WAL日志：记录事务BEGIN
    if (log_manager != nullptr) {
        BeginLogRecord* log_rec = new BeginLogRecord(txn->get_transaction_id());
        lsn_t lsn = log_manager->add_log_to_buffer(log_rec);
        txn->set_prev_lsn(lsn);
        delete log_rec;
    }

    // 4. 返回当前事务指针
    return txn;
}

/**
 * @description: 事务的提交方法
 * @param {Transaction*} txn 需要提交的事务
 * @param {LogManager*} log_manager 日志管理器指针
 */
void TransactionManager::commit(Transaction* txn, LogManager* log_manager) {
    // 两阶段封锁：进入 SHRINKING 阶段（之后只能释放锁，不能获取锁）
    txn->set_state(TransactionState::SHRINKING);

    // WAL日志：记录事务COMMIT
    // 只读事务（write_set为空，如纯SELECT）无需 fsync——没有数据修改需要持久化，
    // COMMIT日志留在缓冲区中，后续写事务commit或缓冲区满时会统一刷盘。
    if (log_manager != nullptr) {
        CommitLogRecord* log_rec = new CommitLogRecord(txn->get_transaction_id());
        log_rec->prev_lsn_ = txn->get_prev_lsn();
        log_manager->add_log_to_buffer(log_rec);
        if (!txn->get_write_set()->empty()) {
            log_manager->flush_log_to_disk();  // 有写操作才刷盘
        }
        delete log_rec;
    }

    // 1. 清空写操作集合（已提交，无需回滚）
    auto write_set = txn->get_write_set();
    while (!write_set->empty()) {
        WriteRecord* wr = write_set->front();
        write_set->pop_front();
        delete wr;
    }
    // 2. 释放所有锁（遍历lock_set逐一unlock）
    auto lock_set = txn->get_lock_set();
    for (auto it = lock_set->begin(); it != lock_set->end(); ) {
        LockDataId lid = *it;
        it = lock_set->erase(it);
        lock_manager_->unlock(txn, lid);
    }
    // 清理间隙锁（谓词范围锁）
    lock_manager_->remove_predicate_ranges(txn->get_transaction_id());
    // 3. 清空索引相关资源
    txn->get_index_latch_page_set()->clear();
    txn->get_index_deleted_page_set()->clear();
    // 4. 更新事务状态为已提交
    txn->set_state(TransactionState::COMMITTED);
}

/**
 * @description: 事务的终止（回滚）方法
 * 逆序遍历 write_set，物理撤销所有修改（含数据和索引）
 * @param {Transaction *} txn 需要回滚的事务
 * @param {LogManager} *log_manager 日志管理器指针
 */
void TransactionManager::abort(Transaction * txn, LogManager *log_manager) {
    // 1. 收集受影响表名（用于后续刷盘）
    std::unordered_set<std::string> affected_tables;
    auto write_set = txn->get_write_set();
    for (auto& wr : *write_set) {
        affected_tables.insert(wr->GetTableName());
    }

    // 2. 逆序遍历 write_set，撤销所有修改（仅修改缓冲池，不产生WAL）
    while (!write_set->empty()) {
        WriteRecord* wr = write_set->back();
        write_set->pop_back();

        switch (wr->GetWriteType()) {
            case WType::INSERT_TUPLE:
                undo_insert(sm_manager_, wr->GetTableName(), wr->GetRid());
                break;

            case WType::DELETE_TUPLE:
                undo_delete(sm_manager_, wr->GetTableName(), wr->GetRecord(), wr->GetRid());
                break;

            case WType::UPDATE_TUPLE:
                undo_update(sm_manager_, wr->GetTableName(), wr->GetRecord(), wr->GetRid());
                break;
        }
        delete wr;
    }

    // 3. 刷盘所有受影响表的数据页和索引页
    //    必须在ABORT日志写入前执行，确保ABORT日志存在于磁盘时undo已持久化。
    //    这样崩溃恢复时若看到ABORT日志，磁盘上的undo数据必然是完整的。
    for (auto& tab_name : affected_tables) {
        if (sm_manager_->fhs_.count(tab_name)) {
            auto fh = sm_manager_->fhs_.at(tab_name).get();
            sm_manager_->get_bpm()->flush_all_pages(fh->GetFd());
            sm_manager_->get_disk_manager()->sync_file(fh->GetFd());
        }
        // 刷盘该表的所有索引文件
        auto& tab = sm_manager_->db_.get_table(tab_name);
        for (auto& index : tab.indexes) {
            std::string ix_name = sm_manager_->get_ix_manager()->get_index_name(tab_name, index.cols);
            if (sm_manager_->ihs_.count(ix_name)) {
                auto ih = sm_manager_->ihs_.at(ix_name).get();
                sm_manager_->get_bpm()->flush_all_pages(ih->get_fd());
                sm_manager_->get_disk_manager()->sync_file(ih->get_fd());
            }
        }
    }

    // 4. WAL日志：记录事务ABORT（在undo已完成并刷盘后写日志）
    //    崩溃恢复时，ABORT日志的存在保证undo数据已在磁盘上，
    //    恢复流程可以安全地跳过此事务（其undo效果已持久化）。
    if (log_manager != nullptr) {
        AbortLogRecord* log_rec = new AbortLogRecord(txn->get_transaction_id());
        log_rec->prev_lsn_ = txn->get_prev_lsn();
        log_manager->add_log_to_buffer(log_rec);
        log_manager->flush_log_to_disk();
        delete log_rec;
    }

    // 5. 释放所有锁（遍历lock_set逐一unlock）
    auto lock_set = txn->get_lock_set();
    for (auto it = lock_set->begin(); it != lock_set->end(); ) {
        LockDataId lid = *it;
        it = lock_set->erase(it);
        lock_manager_->unlock(txn, lid);
    }
    // 清理间隙锁（谓词范围锁）
    lock_manager_->remove_predicate_ranges(txn->get_transaction_id());
    // 6. 清空索引相关资源
    txn->get_index_latch_page_set()->clear();
    txn->get_index_deleted_page_set()->clear();
    // 7. 更新事务状态为已中止
    txn->set_state(TransactionState::ABORTED);
}
