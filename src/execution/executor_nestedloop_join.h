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

/** 嵌套循环连接执行器（Nested Loop Join Executor）
 *  使用简单的嵌套循环实现：对左表每一行扫描右表的每一行
 *  输出包含左表和右表的所有列
 */
class NestedLoopJoinExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> left_;    // 左儿子节点（驱动表）
    std::unique_ptr<AbstractExecutor> right_;   // 右儿子节点（被探查表）
    size_t len_;                                // join后获得的每条记录的长度
    std::vector<ColMeta> cols_;                 // join后获得的记录的字段

    std::vector<Condition> fed_conds_;          // join条件
    bool isend;

    // 保存左表和右表的原始列元数据（offset 未被修改），用于条件评估
    std::vector<ColMeta> left_cols_orig_;
    std::vector<ColMeta> right_cols_orig_;

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

    /** 评估连接条件：左表行和右表行是否满足所有 JOIN ON 条件
     *  使用原始 offset（cols_orig_），因为 cols_ 中的 offset 已被调整为输出格式 */
    bool eval_join_conds(const RmRecord& left_rec, const RmRecord& right_rec) {
        for (auto& cond : fed_conds_) {
            // 查找 lhs 列在哪个表中
            const ColMeta* lhs_meta = find_col(left_cols_orig_, cond.lhs_col);
            const RmRecord* lhs_rec;
            if (lhs_meta) {
                lhs_rec = &left_rec;
            } else {
                lhs_meta = find_col(right_cols_orig_, cond.lhs_col);
                lhs_rec = &right_rec;
            }

            // 查找 rhs 列在哪个表中
            const ColMeta* rhs_meta = find_col(left_cols_orig_, cond.rhs_col);
            const RmRecord* rhs_rec;
            if (rhs_meta) {
                rhs_rec = &left_rec;
            } else {
                rhs_meta = find_col(right_cols_orig_, cond.rhs_col);
                rhs_rec = &right_rec;
            }

            if (!lhs_meta || !rhs_meta) return false;

            // 确保类型一致
            if (lhs_meta->type != rhs_meta->type) return false;

            // 读取值并比较（使用原始 offset）
            bool cond_result = false;
            if (lhs_meta->type == TYPE_INT) {
                int lhs_val = *(int*)(lhs_rec->data + lhs_meta->offset);
                int rhs_val = *(int*)(rhs_rec->data + rhs_meta->offset);
                cond_result = compare_val<int>(lhs_val, rhs_val, cond.op);
            } else if (lhs_meta->type == TYPE_BIGINT || lhs_meta->type == TYPE_DATETIME) {
                int64_t lhs_val = *(int64_t*)(lhs_rec->data + lhs_meta->offset);
                int64_t rhs_val = *(int64_t*)(rhs_rec->data + rhs_meta->offset);
                cond_result = compare_val<int64_t>(lhs_val, rhs_val, cond.op);
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
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right,
                            std::vector<Condition> conds) {
        left_ = std::move(left);
        right_ = std::move(right);
        len_ = left_->tupleLen() + right_->tupleLen();

        // 保存原始列元数据（用于条件评估中的 offset 计算）
        left_cols_orig_ = left_->cols();
        right_cols_orig_ = right_->cols();

        // 构建输出列（调整右表列 offset）
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col : right_cols) {
            col.offset += left_->tupleLen();
        }
        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());

        isend = false;
        fed_conds_ = std::move(conds);
        cursor_ = 0;
    }

    void beginTuple() override {
        results_.clear();
        left_->beginTuple();

        // 对左表每一行，扫描右表查找匹配
        for (; !left_->is_end(); left_->nextTuple()) {
            auto left_rec = left_->Next();
            if (!left_rec) continue;

            right_->beginTuple();

            // 扫描右表每一行
            for (; !right_->is_end(); right_->nextTuple()) {
                auto right_rec = right_->Next();
                if (!right_rec) continue;

                if (eval_join_conds(*left_rec, *right_rec)) {
                    // 创建组合记录：左表列 + 右表列
                    auto result = std::make_unique<RmRecord>(len_);
                    memcpy(result->data, left_rec->data, left_->tupleLen());
                    memcpy(result->data + left_->tupleLen(), right_rec->data, right_->tupleLen());
                    results_.push_back(std::move(result));
                }
            }
        }
        cursor_ = 0;
        isend = results_.empty();
    }

    void nextTuple() override {
        cursor_++;
        if (cursor_ >= results_.size()) {
            isend = true;
        }
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

    Rid &rid() override { return _abstract_rid; }
};