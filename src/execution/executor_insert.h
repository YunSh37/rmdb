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

class InsertExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;                   // 表的元数据
    std::vector<Value> values_;     // 需要插入的数据
    RmFileHandle *fh_;              // 表的数据文件句柄
    std::string tab_name_;          // 表名称
    Rid rid_;                       // 插入的位置，由于系统默认插入时不指定位置，因此当前rid_在插入后才赋值
    SmManager *sm_manager_;

   public:
    InsertExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<Value> values, Context *context) {
        sm_manager_ = sm_manager;
        tab_ = sm_manager_->db_.get_table(tab_name);
        values_ = values;
        tab_name_ = tab_name;
        if (values.size() != tab_.cols.size()) {
            throw InvalidValueCountError();
        }
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        context_ = context;
    };

    std::unique_ptr<RmRecord> Next() override {
        // 申请表级IX锁（意向排他锁）——仅显式事务需要加锁
        if (context_->txn_ != nullptr && context_->lock_mgr_ != nullptr && context_->txn_->get_txn_mode()) {
            context_->lock_mgr_->lock_IX_on_table(context_->txn_, fh_->GetFd());
        }

        // Make record buffer（包含MVCC头部空间，共record_size字节）
        RmRecord rec(fh_->get_file_hdr().record_size);
        for (size_t i = 0; i < values_.size(); i++) {
            auto &col = tab_.cols[i];
            auto &val = values_[i];
            if (col.type != val.type) {
                // 允许 INT→BIGINT 隐式转换
                if (col.type == TYPE_BIGINT && val.type == TYPE_INT) {
                    val.type = TYPE_BIGINT;
                    val.bigint_val = static_cast<int64_t>(val.int_val);
                } else if (col.type == TYPE_BIGINT && val.type == TYPE_BIGINT) {
                    // BIGINT 列接受 BIGINT 值，检查范围
                    if (val.bigint_val > INT64_MAX || val.bigint_val < INT64_MIN) {
                        throw IncompatibleTypeError(coltype2str(col.type), coltype2str(val.type));
                    }
                } else if (col.type == TYPE_INT && val.type == TYPE_BIGINT) {
                    // BIGINT 值无法安全转为 INT，报错
                    throw IncompatibleTypeError(coltype2str(col.type), coltype2str(val.type));
                } else if (col.type == TYPE_FLOAT && val.type == TYPE_INT) {
                    // 允许 INT→FLOAT 隐式转换
                    val.type = TYPE_FLOAT;
                    val.float_val = static_cast<float>(val.int_val);
                } else if (col.type == TYPE_FLOAT && val.type == TYPE_BIGINT) {
                    // 允许 BIGINT→FLOAT 隐式转换
                    val.type = TYPE_FLOAT;
                    val.float_val = static_cast<float>(val.bigint_val);
                } else if (col.type == TYPE_DATETIME && val.type == TYPE_STRING) {
                    // 字符串→DATETIME 转换，解析并验证 'YYYY-MM-DD HH:MM:SS'
                    val.set_datetime(datetime_parse(val.str_val));
                } else {
                    throw IncompatibleTypeError(coltype2str(col.type), coltype2str(val.type));
                }
            }
            val.init_raw(col.len);
            memcpy(rec.data + col.offset, val.raw->data, col.len);
        }

        // 初始化MVCC头部（存储在record末尾）
        int user_size = fh_->get_user_record_size();
        MvccHeader* mvcc_hdr = reinterpret_cast<MvccHeader*>(rec.data + user_size);
        if (context_->txn_ != nullptr) {
            mvcc_hdr->xmin_ = context_->txn_->get_start_ts();
            mvcc_hdr->xmax_ = INT32_MAX;  // 未被删除
        } else {
            mvcc_hdr->xmin_ = 0;
            mvcc_hdr->xmax_ = INT32_MAX;
        }

        // 唯一索引约束检查：插入前检查所有索引是否有重复键
        for (size_t i = 0; i < tab_.indexes.size(); ++i) {
            auto& index = tab_.indexes[i];
            std::string ix_name = sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols);
            // 确保索引已打开
            if (sm_manager_->ihs_.count(ix_name) == 0) {
                sm_manager_->ihs_.emplace(ix_name, sm_manager_->get_ix_manager()->open_index(tab_name_, index.cols));
            }
            auto ih = sm_manager_->ihs_.at(ix_name).get();
            // 构建索引键（仅使用用户数据部分）
            char* key = new char[index.col_tot_len];
            int offset = 0;
            for (int j = 0; j < index.col_num; ++j) {
                memcpy(key + offset, rec.data + index.cols[j].offset, index.cols[j].len);
                offset += index.cols[j].len;
            }

            // ===== 间隙锁检查：防止幻读 =====
            // 仅显式事务需要检查
            if (context_->txn_ != nullptr && context_->lock_mgr_ != nullptr && context_->txn_->get_txn_mode()) {
                std::string key_str(key, index.col_tot_len);
                if (context_->lock_mgr_->check_predicate_conflict(
                        fh_->GetFd(), key_str, context_->txn_->get_transaction_id())) {
                    delete[] key;
                    throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                     AbortReason::DEADLOCK_PREVENTION);
                }
            }

            // 检查键是否已存在
            std::vector<Rid> results;
            if (ih->get_value(key, &results, context_->txn_)) {
                delete[] key;
                throw DuplicateKeyError();
            }
            delete[] key;
        }

        // Insert into record file（rec.data包含用户数据+MVCC头）
        rid_ = fh_->insert_record(rec.data, context_);

        // WAL日志：记录INSERT操作（用于故障恢复REDO/UNDO）
        if (context_->log_mgr_ != nullptr && context_->txn_ != nullptr) {
            InsertLogRecord* log_rec = new InsertLogRecord(context_->txn_->get_transaction_id(), rec, rid_, tab_name_);
            log_rec->prev_lsn_ = context_->txn_->get_prev_lsn();
            lsn_t lsn = context_->log_mgr_->add_log_to_buffer(log_rec);
            context_->txn_->set_prev_lsn(lsn);

            // 设置页面LSN
            auto page_handle = fh_->fetch_page_handle(rid_.page_no);
            page_handle.page->set_page_lsn(lsn);
            sm_manager_->get_bpm()->unpin_page(page_handle.page->get_page_id(), true);

            // 将日志记录加入write_set（abort时需要通过日志undo）
            context_->txn_->append_write_record(new WriteRecord(WType::INSERT_TUPLE, tab_name_, rid_));
            delete log_rec;
        } else if (context_->txn_ != nullptr) {
            // 无日志管理器时，仅记录写操作（用于事务回滚）
            auto wr = new WriteRecord(WType::INSERT_TUPLE, tab_name_, rid_);
            context_->txn_->append_write_record(wr);
        }

        // 维护索引：将新记录插入所有索引
        // 设置日志管理器（用于索引物理日志记录）
        if (context_->txn_ != nullptr && context_->log_mgr_ != nullptr) {
            context_->txn_->set_log_mgr(context_->log_mgr_);
        }
        for (size_t i = 0; i < tab_.indexes.size(); ++i) {
            auto& index = tab_.indexes[i];
            std::string ix_name = sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols);
            auto ih = sm_manager_->ihs_.at(ix_name).get();
            char* key = new char[index.col_tot_len];
            int offset = 0;
            for (int j = 0; j < index.col_num; ++j) {
                memcpy(key + offset, rec.data + index.cols[j].offset, index.cols[j].len);
                offset += index.cols[j].len;
            }
            ih->insert_entry(key, rid_, context_->txn_, ix_name);
            delete[] key;
        }

        // 获取行级X锁（排他锁）——仅显式事务需要加锁
        if (context_->txn_ != nullptr && context_->lock_mgr_ != nullptr && context_->txn_->get_txn_mode()) {
            context_->lock_mgr_->lock_exclusive_on_record(context_->txn_, rid_, fh_->GetFd());
        }

        return nullptr;
    }
    Rid &rid() override { return rid_; }
};