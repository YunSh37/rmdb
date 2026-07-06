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

class UpdateExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<Rid> rids_;
    std::string tab_name_;
    std::vector<SetClause> set_clauses_;
    SmManager *sm_manager_;

    /**
     * @brief 检查某个索引列是否被更新
     */
    bool is_col_updated(const std::string& col_name) {
        for (auto& clause : set_clauses_) {
            if (clause.lhs.col_name == col_name) return true;
        }
        return false;
    }

   public:
    UpdateExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<SetClause> set_clauses,
                   std::vector<Condition> conds, std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        set_clauses_ = set_clauses;
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

        // ===== 间隙锁检查：检查更新的索引键是否与活跃扫描范围冲突 =====
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

        // 遍历所有需要更新的记录
        for (auto& rid : rids_) {
            // 获取行级X锁（排他锁）——仅显式事务需要加锁
            if (context_->txn_ != nullptr && context_->lock_mgr_ != nullptr && context_->txn_->get_txn_mode()) {
                context_->lock_mgr_->lock_exclusive_on_record(context_->txn_, rid, fh_->GetFd());
            }

            // 1. 读取当前记录（包含用户数据+MVCC头）
            auto rec = fh_->get_record(rid, context_);

            // 保存旧记录副本（用于WAL日志和事务回滚）
            RmRecord old_rec(*rec);  // 深拷贝旧记录

            // 2. 如果有索引，先记录旧键（用于后续删除）
            std::vector<std::string> old_keys;  // 每个索引的旧键
            for (size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto& index = tab_.indexes[i];
                char* old_key = new char[index.col_tot_len];
                int offset = 0;
                for (int j = 0; j < index.col_num; ++j) {
                    memcpy(old_key + offset, rec->data + index.cols[j].offset, index.cols[j].len);
                    offset += index.cols[j].len;
                }
                old_keys.push_back(std::string(old_key, index.col_tot_len));
                delete[] old_key;
            }

            // 3. 对每个 set_clause 修改对应列
            for (auto& clause : set_clauses_) {
                auto col_it = std::find_if(tab_.cols.begin(), tab_.cols.end(),
                    [&](const ColMeta& col) { return col.name == clause.lhs.col_name; });
                if (col_it == tab_.cols.end()) {
                    throw ColumnNotFoundError(clause.lhs.col_name);
                }
                if (col_it->type != clause.rhs.type) {
                    // 允许 INT→BIGINT 隐式转换
                    if (col_it->type == TYPE_BIGINT && clause.rhs.type == TYPE_INT) {
                        clause.rhs.type = TYPE_BIGINT;
                        clause.rhs.bigint_val = static_cast<int64_t>(clause.rhs.int_val);
                    } else if (col_it->type == TYPE_INT && clause.rhs.type == TYPE_BIGINT) {
                        throw IncompatibleTypeError(coltype2str(col_it->type), coltype2str(clause.rhs.type));
                    } else if (col_it->type == TYPE_FLOAT && clause.rhs.type == TYPE_INT) {
                        // 允许 INT→FLOAT 隐式转换（例如 SET score = 90，score 为 FLOAT）
                        clause.rhs.type = TYPE_FLOAT;
                        clause.rhs.float_val = static_cast<float>(clause.rhs.int_val);
                    } else if (col_it->type == TYPE_FLOAT && clause.rhs.type == TYPE_BIGINT) {
                        clause.rhs.type = TYPE_FLOAT;
                        clause.rhs.float_val = static_cast<float>(clause.rhs.bigint_val);
                    } else if (col_it->type == TYPE_DATETIME && clause.rhs.type == TYPE_STRING) {
                        // 字符串→DATETIME 转换，解析并验证 'YYYY-MM-DD HH:MM:SS'
                        clause.rhs.set_datetime(datetime_parse(clause.rhs.str_val));
                    } else {
                        throw IncompatibleTypeError(coltype2str(col_it->type), coltype2str(clause.rhs.type));
                    }
                }
                if (clause.rhs.raw == nullptr) {
                    Value val = clause.rhs;
                    val.init_raw(col_it->len);
                    memcpy(rec->data + col_it->offset, val.raw->data, col_it->len);
                } else {
                    memcpy(rec->data + col_it->offset, clause.rhs.raw->data, col_it->len);
                }
            }

            // 3a. 更新MVCC头部：将xmin更新为当前事务时间戳（标记为新版本）
            if (context_->txn_ != nullptr) {
                int user_size = fh_->get_user_record_size();
                MvccHeader* mvcc_hdr = reinterpret_cast<MvccHeader*>(rec->data + user_size);
                mvcc_hdr->xmin_ = context_->txn_->get_start_ts();
                // xmax保持不变（通常为INT32_MAX）
            }

            // WAL日志：记录UPDATE操作（含新旧记录，用于REDO/UNDO）
            if (context_->log_mgr_ != nullptr && context_->txn_ != nullptr) {
                UpdateLogRecord* log_rec = new UpdateLogRecord(context_->txn_->get_transaction_id(), tab_name_,
                                                               old_rec, *rec, rid);
                log_rec->prev_lsn_ = context_->txn_->get_prev_lsn();
                lsn_t lsn = context_->log_mgr_->add_log_to_buffer(log_rec);
                context_->txn_->set_prev_lsn(lsn);
                delete log_rec;

                // 记录写操作（用于事务回滚）
                auto wr = new WriteRecord(WType::UPDATE_TUPLE, tab_name_, rid, old_rec);
                context_->txn_->append_write_record(wr);
            } else if (context_->txn_ != nullptr) {
                // 无日志管理器时，仅记录写操作（用于事务回滚）
                auto wr = new WriteRecord(WType::UPDATE_TUPLE, tab_name_, rid, old_rec);
                context_->txn_->append_write_record(wr);
            }

            // 4. 维护索引：删除旧键，检查新键唯一性，插入新键
            for (size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto& index = tab_.indexes[i];
                std::string ix_name = sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols);
                // 确保索引已打开
                if (sm_manager_->ihs_.count(ix_name) == 0) {
                    sm_manager_->ihs_.emplace(ix_name, sm_manager_->get_ix_manager()->open_index(tab_name_, index.cols));
                }
                auto ih = sm_manager_->ihs_.at(ix_name).get();

                // 构建新键
                char* new_key = new char[index.col_tot_len];
                int offset = 0;
                for (int j = 0; j < index.col_num; ++j) {
                    memcpy(new_key + offset, rec->data + index.cols[j].offset, index.cols[j].len);
                    offset += index.cols[j].len;
                }

                // 检查是否有索引列被更新
                bool updated = false;
                for (int j = 0; j < index.col_num; ++j) {
                    if (is_col_updated(index.cols[j].name)) {
                        updated = true;
                        break;
                    }
                }

                if (updated) {
                    // 只有索引列被更新时才需要维护
                    // 检查新键是否等于旧键
                    if (memcmp(new_key, old_keys[i].c_str(), index.col_tot_len) != 0) {
                        // 新键不同于旧键，检查唯一性
                        std::vector<Rid> results;
                        if (ih->get_value(new_key, &results, context_->txn_)) {
                            delete[] new_key;
                            throw DuplicateKeyError();
                        }
                        // 删除旧键
                        ih->delete_entry(old_keys[i].c_str(), context_->txn_);
                        // 插入新键
                        ih->insert_entry(new_key, rid, context_->txn_);
                    }
                }
                delete[] new_key;
            }

            // 5. 写回修改后的记录（包含用户数据+更新后的MVCC头）
            fh_->update_record(rid, rec->data, context_);

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