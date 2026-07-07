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
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"
#include "transaction/txn_defs.h"

class DeleteExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;                   // 表的元数据
    std::vector<Condition> conds_;  // delete的条件
    RmFileHandle *fh_;              // 表的数据文件句柄
    std::vector<Rid> rids_;         // 需要删除的记录的位置
    std::string tab_name_;          // 表名称
    SmManager *sm_manager_;

   public:
    DeleteExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<Condition> conds,
                   std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;
    }

    std::unique_ptr<RmRecord> Next() override {
        // 申请表级IX锁（意向排他锁）——仅显式事务需要加锁
        if (context_->txn_ != nullptr && context_->lock_mgr_ != nullptr && context_->txn_->get_txn_mode()) {
            context_->lock_mgr_->lock_IX_on_table(context_->txn_, fh_->GetFd());
        }

        // ===== 间隙锁检查：检查删除的索引键是否与活跃扫描范围冲突 =====
        if (context_->txn_ != nullptr && context_->lock_mgr_ != nullptr && context_->txn_->get_txn_mode()) {
            for (auto& rid : rids_) {
                auto rec = fh_->get_record(rid, context_);
                for (size_t i = 0; i < tab_.indexes.size(); ++i) {
                    auto& index = tab_.indexes[i];
                    char* key = new char[index.col_tot_len];
                    int offset = 0;
                    for (int j = 0; j < index.col_num; ++j) {
                        memcpy(key + offset, rec->data + index.cols[j].offset, index.cols[j].len);
                        offset += index.cols[j].len;
                    }
                    std::string key_str(key, index.col_tot_len);
                    delete[] key;
                    if (context_->lock_mgr_->check_predicate_conflict(
                            fh_->GetFd(), key_str, context_->txn_->get_transaction_id())) {
                        throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                         AbortReason::DEADLOCK_PREVENTION);
                    }
                }
            }
        }

        // 遍历删除所有匹配的记录
        for (auto& rid : rids_) {
            // 获取行级X锁（排他锁）——仅显式事务需要加锁
            if (context_->txn_ != nullptr && context_->lock_mgr_ != nullptr && context_->txn_->get_txn_mode()) {
                context_->lock_mgr_->lock_exclusive_on_record(context_->txn_, rid, fh_->GetFd());
            }

            // 删除前读取记录（用于WAL日志和事务回滚）
            // get_record 返回完整slot（用户数据+MVCC头）
            auto rec = fh_->get_record(rid, context_);

            // WAL日志：记录DELETE操作（用于故障恢复REDO/UNDO）
            if (context_->log_mgr_ != nullptr && context_->txn_ != nullptr) {
                DeleteLogRecord* log_rec = new DeleteLogRecord(
                    context_->txn_->get_transaction_id(), tab_name_, *rec, rid,
                    context_->txn_->get_start_ts());
                log_rec->prev_lsn_ = context_->txn_->get_prev_lsn();
                lsn_t lsn = context_->log_mgr_->add_log_to_buffer(log_rec);
                context_->txn_->set_prev_lsn(lsn);
                delete log_rec;

                // 记录写操作（用于事务回滚）
                auto wr = new WriteRecord(WType::DELETE_TUPLE, tab_name_, rid, *rec);
                context_->txn_->append_write_record(wr);
            } else if (context_->txn_ != nullptr) {
                // 无日志管理器时，仅记录写操作（用于事务回滚）
                auto wr = new WriteRecord(WType::DELETE_TUPLE, tab_name_, rid, *rec);
                context_->txn_->append_write_record(wr);
            }

            // 先从索引中删除记录
            // 设置日志管理器（用于索引物理日志记录）
            if (context_->txn_ != nullptr && context_->log_mgr_ != nullptr) {
                context_->txn_->set_log_mgr(context_->log_mgr_);
            }
            for (size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto& index = tab_.indexes[i];
                std::string ix_name = sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols);
                // 确保索引已打开
                if (sm_manager_->ihs_.count(ix_name) == 0) {
                    sm_manager_->ihs_.emplace(ix_name, sm_manager_->get_ix_manager()->open_index(tab_name_, index.cols));
                }
                auto ih = sm_manager_->ihs_.at(ix_name).get();
                char* key = new char[index.col_tot_len];
                int offset = 0;
                for (int j = 0; j < index.col_num; ++j) {
                    memcpy(key + offset, rec->data + index.cols[j].offset, index.cols[j].len);
                    offset += index.cols[j].len;
                }
                ih->delete_entry(key, context_->txn_, ix_name);
                delete[] key;
            }

            // MVCC软删除：设置xmax（保留bitmap和物理存储）
            if (context_->txn_ != nullptr) {
                fh_->soft_delete_record(rid, context_->txn_->get_start_ts(), context_);
            } else {
                // 无事务上下文，执行物理删除
                fh_->delete_record(rid, context_);
            }

            // 设置页面LSN（WAL）
            if (context_->log_mgr_ != nullptr && context_->txn_ != nullptr) {
                auto page_handle = fh_->fetch_page_handle(rid.page_no);
                page_handle.page->set_page_lsn(context_->txn_->get_prev_lsn());
                sm_manager_->get_bpm()->unpin_page(page_handle.page->get_page_id(), true);
            }
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};