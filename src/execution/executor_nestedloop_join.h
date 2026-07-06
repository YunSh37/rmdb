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

/** 块嵌套循环连接执行器（Block Nested-Loop Join Executor）
 *  采用块式读取+惰性求值，避免一次性物化所有结果，支持超内存的大表连接。
 *
 *  算法流程：
 *    1. 物化内表（右表）全部元组到内存（通常较小）
 *    2. 外表（左表/驱动表）按块读取（每次 BLOCK_SIZE 条元组）
 *    3. 对外表块中每条元组，扫描内表物化结果，满足连接条件则输出
 *    4. 逐条惰性生成结果（不一次性物化），避免内存溢出
 *
 *  输出包含左表和右表的所有列。
 */
class NestedLoopJoinExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> left_;    // 左儿子节点（驱动表/外表）
    std::unique_ptr<AbstractExecutor> right_;   // 右儿子节点（被探查表/内表）
    size_t len_;                                // join后获得的每条记录的长度
    std::vector<ColMeta> cols_;                 // join后获得的记录的字段

    std::vector<Condition> fed_conds_;          // join条件
    bool finished_;                              // 是否已无更多结果

    // 保存左表和右表的原始列元数据（offset 未被修改），用于条件评估
    std::vector<ColMeta> left_cols_orig_;
    std::vector<ColMeta> right_cols_orig_;

    // ===== 块嵌套循环状态 =====
    static constexpr size_t BLOCK_SIZE = 5000;  // 每次读取外表元组数

    std::vector<std::unique_ptr<RmRecord>> inner_all_;   // 物化的内表全部元组
    std::vector<std::unique_ptr<RmRecord>> outer_block_;  // 当前外表块
    size_t outer_idx_;   // 当前外表块中正在处理的元组索引
    size_t inner_idx_;   // 当前内表扫描位置

    // 预计算的下一个结果（惰性求值：advanceToNext 在 beginTuple/nextTuple 中计算）
    std::unique_ptr<RmRecord> current_result_;

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

    /** 加载下一块外表元组（最多 BLOCK_SIZE 条） */
    bool loadNextOuterBlock() {
        outer_block_.clear();
        for (size_t i = 0; i < BLOCK_SIZE && !left_->is_end(); i++) {
            auto rec = left_->Next();
            if (rec) {
                auto copy = std::make_unique<RmRecord>(left_->tupleLen());
                memcpy(copy->data, rec->data, left_->tupleLen());
                outer_block_.push_back(std::move(copy));
            }
            left_->nextTuple();
        }
        return !outer_block_.empty();
    }

    /** 物化内表全部元组 */
    void materializeInner() {
        inner_all_.clear();
        right_->beginTuple();
        while (!right_->is_end()) {
            auto rec = right_->Next();
            if (rec) {
                auto copy = std::make_unique<RmRecord>(right_->tupleLen());
                memcpy(copy->data, rec->data, right_->tupleLen());
                inner_all_.push_back(std::move(copy));
            }
            right_->nextTuple();
        }
    }

    /** 前进到下一个匹配结果（惰性求值核心）
     *  如果找到，将结果存入 current_result_；如果无更多结果，设置 finished_ = true */
    void advanceToNext() {
        while (true) {
            // 当前外表块已处理完？加载下一块
            if (outer_idx_ >= outer_block_.size()) {
                if (!loadNextOuterBlock()) {
                    finished_ = true;
                    current_result_.reset();
                    return;
                }
                outer_idx_ = 0;
            }

            // 扫描内表，查找与当前外表元组匹配的记录
            while (inner_idx_ < inner_all_.size()) {
                auto& left_rec = outer_block_[outer_idx_];
                auto& right_rec = inner_all_[inner_idx_];
                inner_idx_++;

                if (eval_join_conds(*left_rec, *right_rec)) {
                    // 构建组合记录：左表列 + 右表列
                    current_result_ = std::make_unique<RmRecord>(len_);
                    memcpy(current_result_->data, left_rec->data, left_->tupleLen());
                    memcpy(current_result_->data + left_->tupleLen(), right_rec->data, right_->tupleLen());
                    return;  // 找到下一个结果
                }
            }

            // 内表扫描完毕，前进外表块中的下一条元组
            inner_idx_ = 0;
            outer_idx_++;
        }
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

        fed_conds_ = std::move(conds);
        finished_ = false;
        outer_idx_ = 0;
        inner_idx_ = 0;
    }

    void beginTuple() override {
        finished_ = false;
        outer_idx_ = 0;
        inner_idx_ = 0;
        outer_block_.clear();
        current_result_.reset();

        // 1. 物化内表（右表）全部元组到内存
        materializeInner();

        // 2. 初始化外表扫描
        left_->beginTuple();

        // 3. 预计算第一个匹配结果
        advanceToNext();
    }

    void nextTuple() override {
        advanceToNext();
    }

    bool is_end() const override {
        return finished_;
    }

    std::unique_ptr<RmRecord> Next() override {
        if (finished_ || !current_result_) return nullptr;
        auto result = std::make_unique<RmRecord>(len_);
        memcpy(result->data, current_result_->data, len_);
        return result;
    }

    const std::vector<ColMeta>& cols() const override {
        return cols_;
    }

    size_t tupleLen() const override { return len_; }

    Rid &rid() override { return _abstract_rid; }
};