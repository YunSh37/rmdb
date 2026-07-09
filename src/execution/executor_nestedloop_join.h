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
#include <algorithm>

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

/** 块嵌套循环连接执行器（Block Nested-Loop Join Executor）
 *
 *  实现真正的块嵌套循环连接，关键特性：
 *    - 不物化内表（避免大表超内存），每个外表块重新扫描内表
 *    - 合并检查：内表一次扫描中对外表块所有元组求值
 *    - stable_sort 保持外表优先输出顺序
 *
 *  内存上限 = BLOCK_SIZE 条外表 + 一批结果，不随数据规模增长。
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

    // ===== BNLJ 块嵌套循环状态 =====
    static constexpr size_t BLOCK_SIZE = 5000;       // 每次读取外表元组数
    static constexpr size_t MAX_BATCH_SIZE = 10000;  // 每批最多缓存的结果数

    std::vector<std::unique_ptr<RmRecord>> outer_block_;  // 当前外表块
    std::vector<std::unique_ptr<RmRecord>> result_batch_; // 当前结果批次
    size_t result_idx_;       // result_batch_ 中当前结果索引

    // 用于批次满时暂停/恢复的状态
    // 当 inner_continue_from_ >= 0 时，表示上次因批次满而暂停，
    // 需要从 outer_block_[inner_continue_from_] 继续检查当前内表元组
    int inner_continue_from_ = 0;  // -1 表示无需继续

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
        for (auto& cond : fed_conds_) {
            const ColMeta* lhs_meta = find_col(left_cols_orig_, cond.lhs_col);
            const RmRecord* lhs_rec;
            if (lhs_meta) {
                lhs_rec = &left_rec;
            } else {
                lhs_meta = find_col(right_cols_orig_, cond.lhs_col);
                lhs_rec = &right_rec;
            }

            const ColMeta* rhs_meta = find_col(left_cols_orig_, cond.rhs_col);
            const RmRecord* rhs_rec;
            if (rhs_meta) {
                rhs_rec = &left_rec;
            } else {
                rhs_meta = find_col(right_cols_orig_, cond.rhs_col);
                rhs_rec = &right_rec;
            }

            if (!lhs_meta || !rhs_meta) return false;
            if (lhs_meta->type != rhs_meta->type) return false;

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

            if (!cond_result) return false;
        }
        return true;
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

    /** 填充下一批结果：
     *  对当前外表块，扫描内表。对内表每条元组，检查外表块中所有元组。
     *  收集匹配结果，按外表索引 stable_sort 以保持外表优先输出顺序。
     *
     *  为支持大数据量（内表可能包含数百万行），按 MAX_BATCH_SIZE 分批输出。
     *  暂停/恢复通过 inner_continue_from_ 字段实现：
     *    - 正常：inner_continue_from_ == -1
     *    - 暂停：inner_continue_from_ == outer_idx，下次从该外表索引继续
     *      检查同一条内表元组（尚未调用 right_->nextTuple()） */
    void fillResultBatch() {
        result_batch_.clear();
        result_idx_ = 0;

        while (result_batch_.empty()) {
            // 需要加载新外表块？
            if (outer_block_.empty()) {
                if (!loadNextOuterBlock()) {
                    finished_ = true;
                    return;
                }
                // 新块开始，重置内表扫描
                right_->beginTuple();
                inner_continue_from_ = -1;  // 无需继续
            }

            // 用于按外表索引排序的临时结构
            struct IndexedResult {
                size_t outer_idx;
                std::unique_ptr<RmRecord> record;
            };
            std::vector<IndexedResult> block_results;

            bool batch_full = false;

            // 扫描内表
            while (!right_->is_end() && !batch_full) {
                auto inner_rec = right_->Next();
                if (inner_rec) {
                    // 确定从哪个外表索引开始检查
                    // inner_continue_from_ >= 0 表示上次因批次满而暂停，
                    // 当前 inner_rec 已检查过 [0, inner_continue_from_) 的外表元组
                    size_t start_i = (inner_continue_from_ >= 0)
                                     ? static_cast<size_t>(inner_continue_from_) : 0;
                    inner_continue_from_ = -1;  // 重置

                    for (size_t i = start_i; i < outer_block_.size(); i++) {
                        if (eval_join_conds(*outer_block_[i], *inner_rec)) {
                            auto merged = std::make_unique<RmRecord>(len_);
                            memcpy(merged->data, outer_block_[i]->data, left_->tupleLen());
                            memcpy(merged->data + left_->tupleLen(), inner_rec->data, right_->tupleLen());
                            block_results.push_back({i, std::move(merged)});

                            if (block_results.size() >= MAX_BATCH_SIZE) {
                                // 批次满：保存恢复位置，暂停
                                if (i + 1 < outer_block_.size()) {
                                    // 还有外表元组没检查完，下次从此继续
                                    inner_continue_from_ = static_cast<int>(i + 1);
                                } else {
                                    // 当前外表块全部检查完，前进内表
                                    inner_continue_from_ = -1;
                                    right_->nextTuple();
                                }
                                batch_full = true;
                                break;
                            }
                        }
                    }

                    // 如果检查完了所有外表元组且批次未满，前进内表
                    if (!batch_full) {
                        right_->nextTuple();
                    }
                } else {
                    // Next() 返回 nullptr，前进
                    right_->nextTuple();
                }
            }

            // 按外表索引稳定排序，保持外表优先输出顺序
            std::stable_sort(block_results.begin(), block_results.end(),
                [](const IndexedResult& a, const IndexedResult& b) {
                    return a.outer_idx < b.outer_idx;
                });

            for (auto& entry : block_results) {
                result_batch_.push_back(std::move(entry.record));
            }

            // 内表扫描完毕（非批次满导致跳出），准备下一外表块
            if (right_->is_end()) {
                outer_block_.clear();
                inner_continue_from_ = -1;
            }
        }
    }

   public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right,
                            std::vector<Condition> conds) {
        left_ = std::move(left);
        right_ = std::move(right);
        len_ = left_->tupleLen() + right_->tupleLen();

        left_cols_orig_ = left_->cols();
        right_cols_orig_ = right_->cols();

        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col : right_cols) {
            col.offset += left_->tupleLen();
        }
        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());

        fed_conds_ = std::move(conds);
        finished_ = false;
        result_idx_ = 0;
        inner_continue_from_ = -1;
    }

    void beginTuple() override {
        finished_ = false;
        result_idx_ = 0;
        outer_block_.clear();
        result_batch_.clear();
        inner_continue_from_ = -1;

        left_->beginTuple();
        fillResultBatch();
    }

    void nextTuple() override {
        if (finished_) return;

        result_idx_++;
        if (result_idx_ >= result_batch_.size()) {
            fillResultBatch();
        }
    }

    bool is_end() const override {
        return finished_;
    }

    std::unique_ptr<RmRecord> Next() override {
        if (finished_ || result_idx_ >= result_batch_.size()) return nullptr;
        auto result = std::make_unique<RmRecord>(len_);
        memcpy(result->data, result_batch_[result_idx_]->data, len_);
        return result;
    }

    const std::vector<ColMeta>& cols() const override {
        return cols_;
    }

    size_t tupleLen() const override { return len_; }

    Rid &rid() override { return _abstract_rid; }
};