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

/** 半连接执行器（Semi Join Executor）
 *  使用嵌套循环实现：对左表每一行扫描右表，找到第一条匹配即停止
 *  输出仅包含左表列（不包含右表列）
 *  结果中左表每行最多出现一次（天然去重）
 */
class SemiJoinExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> left_;   // 左表（驱动表）
    std::unique_ptr<AbstractExecutor> right_;  // 右表（被探查表）
    size_t len_;                               // 输出记录长度（= 左表记录长度）
    std::vector<ColMeta> cols_;                // 输出列（仅左表列）
    std::vector<Condition> join_conds_;        // 连接条件

    // 物化结果
    std::vector<std::unique_ptr<RmRecord>> results_;
    size_t cursor_;

    /** 比较两个值 */
    template<typename T>
    static bool compare_val(T lhs, T rhs, CompOp op) {
        switch (op) {
            case OP_EQ: return lhs == rhs;
            case OP_NE: return lhs != rhs;
            case OP_LT: return lhs < rhs;
            case OP_GT: return lhs > rhs;
            case OP_LE: return lhs <= rhs;
            case OP_GE: return lhs >= rhs;
            default: return false;
        }
    }

    /** 在列集合中查找指定列 */
    const ColMeta* find_col(const std::vector<ColMeta>& cols, const TabCol& target) {
        for (auto& col : cols) {
            if (col.tab_name == target.tab_name && col.name == target.col_name)
                return &col;
        }
        return nullptr;
    }

    /** 评估连接条件：左表行和右表行是否满足所有 JOIN ON 条件 */
    bool eval_join_conds(const RmRecord& left_rec, const RmRecord& right_rec) {
        auto& left_cols = left_->cols();
        auto& right_cols = right_->cols();

        for (auto& cond : join_conds_) {
            // 查找 lhs 列在哪个表中
            const ColMeta* lhs_meta = find_col(left_cols, cond.lhs_col);
            const RmRecord* lhs_rec;
            if (lhs_meta) {
                lhs_rec = &left_rec;
            } else {
                lhs_meta = find_col(right_cols, cond.lhs_col);
                lhs_rec = &right_rec;
            }

            // 查找 rhs 列在哪个表中
            const ColMeta* rhs_meta = find_col(left_cols, cond.rhs_col);
            const RmRecord* rhs_rec;
            if (rhs_meta) {
                rhs_rec = &left_rec;
            } else {
                rhs_meta = find_col(right_cols, cond.rhs_col);
                rhs_rec = &right_rec;
            }

            if (!lhs_meta || !rhs_meta) return false;

            // 确保类型一致
            if (lhs_meta->type != rhs_meta->type) return false;

            // 读取值并比较
            bool cond_result = false;
            if (lhs_meta->type == TYPE_INT) {
                int lhs_val = *(int*)(lhs_rec->data + lhs_meta->offset);
                int rhs_val = *(int*)(rhs_rec->data + rhs_meta->offset);
                cond_result = compare_val<int>(lhs_val, rhs_val, cond.op);
            } else if (lhs_meta->type == TYPE_FLOAT) {
                float lhs_val = *(float*)(lhs_rec->data + lhs_meta->offset);
                float rhs_val = *(float*)(rhs_rec->data + rhs_meta->offset);
                cond_result = compare_val<float>(lhs_val, rhs_val, cond.op);
            } else if (lhs_meta->type == TYPE_STRING) {
                std::string lhs_val(lhs_rec->data + lhs_meta->offset, lhs_meta->len);
                std::string rhs_val(rhs_rec->data + rhs_meta->offset, rhs_meta->len);
                cond_result = compare_val<std::string>(lhs_val, rhs_val, cond.op);
            }

            if (!cond_result) return false;  // 任一条件不满足即失败
        }
        return true;  // 所有条件都满足
    }

   public:
    SemiJoinExecutor(std::unique_ptr<AbstractExecutor> left,
                     std::unique_ptr<AbstractExecutor> right,
                     std::vector<Condition> conds) {
        left_ = std::move(left);
        right_ = std::move(right);
        len_ = left_->tupleLen();
        cols_ = left_->cols();          // 仅左表列
        join_conds_ = std::move(conds);
        cursor_ = 0;
    }

    void beginTuple() override {
        results_.clear();
        left_->beginTuple();

        // 对左表每一行，扫描右表查找匹配
        for (; !left_->is_end(); left_->nextTuple()) {
            auto left_rec = left_->Next();
            if (!left_rec) continue;

            bool found = false;
            right_->beginTuple();

            // 扫描右表，找到第一条匹配即停止（测试点2：去重语义）
            for (; !right_->is_end() && !found; right_->nextTuple()) {
                auto right_rec = right_->Next();
                if (!right_rec) continue;

                if (eval_join_conds(*left_rec, *right_rec)) {
                    found = true;
                }
            }

            if (found) {
                // 仅保留左表列（测试点4：不输出右表列）
                auto result = std::make_unique<RmRecord>(len_);
                memcpy(result->data, left_rec->data, len_);
                results_.push_back(std::move(result));
            }
        }
        cursor_ = 0;
    }

    void nextTuple() override {
        cursor_++;
    }

    bool is_end() const override {
        return cursor_ >= results_.size();
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) return nullptr;
        auto& rec = results_[cursor_];
        auto result = std::make_unique<RmRecord>(len_);
        memcpy(result->data, rec->data, len_);
        return result;
    }

    const std::vector<ColMeta>& cols() const override {
        return cols_;
    }

    size_t tupleLen() const override { return len_; }

    Rid& rid() override { return _abstract_rid; }
};
