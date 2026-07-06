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

class IndexScanExecutor : public AbstractExecutor {
   private:
    std::string tab_name_;                      // 表名称
    TabMeta tab_;                               // 表的元数据
    std::vector<Condition> conds_;              // 扫描条件
    RmFileHandle *fh_;                          // 表的数据文件句柄
    std::vector<ColMeta> cols_;                 // 需要读取的字段
    size_t len_;                                // 选取出来的一条记录的长度
    std::vector<Condition> fed_conds_;          // 扫描条件，和conds_字段相同

    std::vector<std::string> index_col_names_;  // index scan涉及到的索引包含的字段
    IndexMeta index_meta_;                      // index scan涉及到的索引元数据

    Rid rid_;
    std::unique_ptr<RecScan> scan_;
    IxIndexHandle *ih_ = nullptr;               // 索引文件句柄（不持有所有权，由sm_manager_->ihs_管理）
    std::unique_ptr<RmRecord> current_record_;  // 当前记录

    SmManager *sm_manager_;
    bool is_end_;                               // 是否扫描结束

    /**
     * @brief 比较函数，用于条件求值（与SeqScanExecutor保持一致）
     */
    int cmp_int(int a, int b, CompOp op) {
        switch (op) {
            case OP_EQ: return a == b;
            case OP_NE: return a != b;
            case OP_LT: return a < b;
            case OP_GT: return a > b;
            case OP_LE: return a <= b;
            case OP_GE: return a >= b;
            default: return false;
        }
    }

    int cmp_bigint(int64_t a, int64_t b, CompOp op) {
        switch (op) {
            case OP_EQ: return a == b;
            case OP_NE: return a != b;
            case OP_LT: return a < b;
            case OP_GT: return a > b;
            case OP_LE: return a <= b;
            case OP_GE: return a >= b;
            default: return false;
        }
    }

    int cmp_float(float a, float b, CompOp op) {
        switch (op) {
            case OP_EQ: return a == b;
            case OP_NE: return a != b;
            case OP_LT: return a < b;
            case OP_GT: return a > b;
            case OP_LE: return a <= b;
            case OP_GE: return a >= b;
            default: return false;
        }
    }

    int cmp_string(const char *a, const char *b, int len, CompOp op) {
        int res = memcmp(a, b, len);
        switch (op) {
            case OP_EQ: return res == 0;
            case OP_NE: return res != 0;
            case OP_LT: return res < 0;
            case OP_GT: return res > 0;
            case OP_LE: return res <= 0;
            case OP_GE: return res >= 0;
            default: return false;
        }
    }

    /**
     * @brief 逐条件求值，所有条件AND连接
     */
    bool eval_conds(const RmRecord& record) {
        for (auto& cond : fed_conds_) {
            // 获取左操作数列的元数据
            auto lhs_col_meta = tab_.get_col(cond.lhs_col.col_name);
            char* lhs_ptr = record.data + lhs_col_meta->offset;

            if (cond.is_rhs_val) {
                // 右操作数是值
                ColType type = lhs_col_meta->type;
                if (type == TYPE_INT) {
                    int lhs = *(int*)lhs_ptr;
                    int rhs = *(int*)cond.rhs_val.raw->data;
                    if (!cmp_int(lhs, rhs, cond.op)) return false;
                } else if (type == TYPE_BIGINT || type == TYPE_DATETIME) {
                    int64_t lhs = *(int64_t*)lhs_ptr;
                    int64_t rhs = *(int64_t*)cond.rhs_val.raw->data;
                    if (!cmp_bigint(lhs, rhs, cond.op)) return false;
                } else if (type == TYPE_FLOAT) {
                    float lhs = *(float*)lhs_ptr;
                    float rhs = *(float*)cond.rhs_val.raw->data;
                    if (!cmp_float(lhs, rhs, cond.op)) return false;
                } else if (type == TYPE_STRING) {
                    if (!cmp_string(lhs_ptr, cond.rhs_val.raw->data, lhs_col_meta->len, cond.op)) return false;
                }
            } else {
                // 右操作数是另一个列
                auto rhs_col_meta = tab_.get_col(cond.rhs_col.col_name);
                char* rhs_ptr = record.data + rhs_col_meta->offset;
                ColType type = lhs_col_meta->type;
                if (type == TYPE_INT) {
                    int lhs = *(int*)lhs_ptr;
                    int rhs = *(int*)rhs_ptr;
                    if (!cmp_int(lhs, rhs, cond.op)) return false;
                } else if (type == TYPE_BIGINT || type == TYPE_DATETIME) {
                    int64_t lhs = *(int64_t*)lhs_ptr;
                    int64_t rhs = *(int64_t*)rhs_ptr;
                    if (!cmp_bigint(lhs, rhs, cond.op)) return false;
                } else if (type == TYPE_FLOAT) {
                    float lhs = *(float*)lhs_ptr;
                    float rhs = *(float*)rhs_ptr;
                    if (!cmp_float(lhs, rhs, cond.op)) return false;
                } else if (type == TYPE_STRING) {
                    if (!cmp_string(lhs_ptr, rhs_ptr, lhs_col_meta->len, cond.op)) return false;
                }
            }
        }
        return true;
    }

    /**
     * @brief 从条件的列名获取索引列在记录中的位置信息
     */
    int get_col_offset_in_key(const std::string& col_name) {
        int offset = 0;
        for (auto& col : index_meta_.cols) {
            if (col.name == col_name) {
                return offset;
            }
            offset += col.len;
        }
        return -1;
    }

   public:
    IndexScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds, std::vector<std::string> index_col_names,
                    Context *context) {
        sm_manager_ = sm_manager;
        context_ = context;
        tab_name_ = std::move(tab_name);
        tab_ = sm_manager_->db_.get_table(tab_name_);
        conds_ = std::move(conds);
        index_col_names_ = index_col_names;
        index_meta_ = *(tab_.get_index_meta(index_col_names_));
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab_.cols;
        len_ = cols_.back().offset + cols_.back().len;
        is_end_ = false;
        std::map<CompOp, CompOp> swap_op = {
            {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
        };

        for (auto &cond : conds_) {
            if (cond.lhs_col.tab_name != tab_name_) {
                // lhs is on other table, now rhs must be on this table
                assert(!cond.is_rhs_val && cond.rhs_col.tab_name == tab_name_);
                // swap lhs and rhs
                std::swap(cond.lhs_col, cond.rhs_col);
                cond.op = swap_op.at(cond.op);
            }
        }
        fed_conds_ = conds_;
    }

    void beginTuple() override {
        is_end_ = false;

        // 延迟到构建扫描范围后再注册 IS 锁 + 间隙锁（原子操作）

        // 获取已打开的索引文件句柄（由sm_manager_统一管理生命周期）
        std::string ix_name = sm_manager_->get_ix_manager()->get_index_name(tab_name_, index_meta_.cols);
        if (sm_manager_->ihs_.count(ix_name) == 0) {
            sm_manager_->ihs_.emplace(ix_name, sm_manager_->get_ix_manager()->open_index(tab_name_, index_meta_.cols));
        }
        ih_ = sm_manager_->ihs_.at(ix_name).get();

        // 构建扫描键：收集等值条件中匹配索引列的那些条件值
        // 构建lower bound key（用于lower_bound扫描）
        int key_len = index_meta_.col_tot_len;
        char* lower_key = new char[key_len];
        char* upper_key = new char[key_len];
        memset(lower_key, 0, key_len);
        memset(upper_key, 0xFF, key_len);  // upper bound初始化为最大

        bool has_lower = false;  // 是否有下界条件
        bool has_upper = false;  // 是否有上界条件
        bool lower_inclusive = false;  // 下界是否包含等号（>=为true，>为false）
        bool upper_inclusive = false;  // 上界是否包含等号（<=为true，<为false）
        bool exact_match = true;  // 是否所有索引列都有等值条件

        // 按索引列顺序构建key
        for (size_t i = 0; i < index_meta_.cols.size(); ++i) {
            const std::string& col_name = index_meta_.cols[i].name;
            int col_offset = 0;
            for (size_t j = 0; j < i; ++j) {
                col_offset += index_meta_.cols[j].len;
            }
            int col_len = index_meta_.cols[i].len;

            // 查找该列的等值条件
            bool found_eq = false;
            for (auto& cond : fed_conds_) {
                if (cond.lhs_col.col_name == col_name && cond.is_rhs_val && cond.op == OP_EQ) {
                    memcpy(lower_key + col_offset, cond.rhs_val.raw->data, col_len);
                    memcpy(upper_key + col_offset, cond.rhs_val.raw->data, col_len);
                    found_eq = true;
                    has_lower = true;
                    has_upper = true;
                    lower_inclusive = true;
                    upper_inclusive = true;
                    break;
                } else if (cond.lhs_col.col_name == col_name && cond.is_rhs_val &&
                          (cond.op == OP_GT || cond.op == OP_GE)) {
                    memcpy(lower_key + col_offset, cond.rhs_val.raw->data, col_len);
                    has_lower = true;
                    lower_inclusive = (cond.op == OP_GE);  // >=为true, >为false
                    exact_match = false;
                } else if (cond.lhs_col.col_name == col_name && cond.is_rhs_val &&
                          (cond.op == OP_LT || cond.op == OP_LE)) {
                    memcpy(upper_key + col_offset, cond.rhs_val.raw->data, col_len);
                    has_upper = true;
                    upper_inclusive = (cond.op == OP_LE);  // <=为true, <为false
                    exact_match = false;
                }
            }
            if (!found_eq && i < index_meta_.cols.size()) {
                exact_match = false;
            }
            if (!found_eq) {
                // 后面的列没有精确条件，lower bound用最小值，upper bound用最大值
                memset(lower_key + col_offset, 0, col_len);
                memset(upper_key + col_offset, 0xFF, col_len);
            }
        }

        // 使用索引进行查找
        if (exact_match && fed_conds_.size() == index_meta_.cols.size()) {
            // 所有索引列都有精确匹配，可以直接点查
            std::vector<Rid> results;
            bool found = ih_->get_value(lower_key, &results, context_->txn_);
            if (found) {
                // 创建内存中的扫描结果
                struct MemScan : public RecScan {
                    std::vector<Rid> rids_;
                    size_t pos_ = 0;
                    void next() override { pos_++; }
                    bool is_end() const override { return pos_ >= rids_.size(); }
                    Rid rid() const override { return rids_[pos_]; }
                };
                auto* mem_scan = new MemScan();
                mem_scan->rids_ = std::move(results);
                scan_.reset(mem_scan);
            }
            // 如果没有找到匹配记录，scan_为nullptr
        } else if (has_lower || has_upper) {
            // 范围扫描
            Iid lower_iid;
            Iid upper_iid;
            if (has_lower) {
                lower_iid = ih_->lower_bound(lower_key);
            } else {
                lower_iid = ih_->leaf_begin();
            }
            if (has_upper) {
                upper_iid = ih_->upper_bound(upper_key);
            } else {
                upper_iid = ih_->leaf_end();
            }
            scan_ = std::make_unique<IxScan>(ih_, lower_iid, upper_iid, sm_manager_->get_bpm());
        } else {
            // 全索引扫描（没有可用条件限定范围）
            scan_ = std::make_unique<IxScan>(ih_, ih_->leaf_begin(), ih_->leaf_end(), sm_manager_->get_bpm());
        }

        // ===== 原子操作：获取 IS 锁 + 注册间隙锁（防止幻读）=====
        // 仅显式事务需要加锁。IS 锁允许其他事务的 IX 锁（并发写入），
        // 通过间隙锁精确控制范围冲突：只有在扫描范围内的写入才被阻止
        if (context_->txn_ != nullptr && context_->lock_mgr_ != nullptr && context_->txn_->get_txn_mode()) {
            std::string ix_name = sm_manager_->get_ix_manager()->get_index_name(tab_name_, index_meta_.cols);
            bool is_full = false;
            std::string low_str, up_str;
            bool has_lower_final = has_lower;
            bool has_upper_final = has_upper;
            bool lower_incl_final = lower_inclusive;
            bool upper_incl_final = upper_inclusive;

            if (exact_match && fed_conds_.size() == index_meta_.cols.size()) {
                // 点查询：范围即精确匹配键（上下界相同，都包含等号）
                low_str = std::string(lower_key, key_len);
                up_str = std::string(upper_key, key_len);
                has_lower_final = true;
                has_upper_final = true;
                lower_incl_final = true;
                upper_incl_final = true;
            } else if (has_lower || has_upper) {
                // 范围扫描：使用已计算的上下界
                low_str = std::string(lower_key, key_len);
                up_str = std::string(upper_key, key_len);
            } else {
                // 全索引扫描（无范围限制）
                is_full = true;
            }

            context_->lock_mgr_->lock_IS_on_table_with_predicate(
                context_->txn_, fh_->GetFd(), ix_name,
                low_str, up_str, has_lower_final, has_upper_final,
                lower_incl_final, upper_incl_final, is_full);
        }

        delete[] lower_key;
        delete[] upper_key;

        // 定位到第一条满足条件的记录
        nextTuple();
    }

    void nextTuple() override {
        if (is_end_) return;

        while (scan_ != nullptr && !scan_->is_end()) {
            rid_ = scan_->rid();
            // MVCC可见性检查
            if (context_->txn_ != nullptr) {
                if (!fh_->is_visible(rid_, context_->txn_->get_start_ts())) {
                    scan_->next();
                    continue;
                }
                // 对可见记录获取行级S锁——仅显式事务需要加锁
                if (context_->lock_mgr_ != nullptr && context_->txn_->get_txn_mode()) {
                    context_->lock_mgr_->lock_shared_on_record(context_->txn_, rid_, fh_->GetFd());
                }
            }
            // 从数据文件读取记录
            auto record = fh_->get_record(rid_, context_);
            // 检查是否满足所有条件
            if (eval_conds(*record)) {
                current_record_ = std::move(record);
                scan_->next();  // 前进到下一条
                return;
            }
            scan_->next();
        }
        // 扫描结束
        is_end_ = true;
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end_) return nullptr;
        return std::make_unique<RmRecord>(*current_record_);
    }

    bool is_end() const override { return is_end_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    Rid &rid() override { return rid_; }
};