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
#include <map>
#include <climits>
#include <cfloat>  // for FLT_MAX, FLT_MIN

class AggregationExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<int> agg_types_;          // 聚合类型
    std::vector<TabCol> agg_targets_;     // 聚合目标列
    std::vector<TabCol> group_by_cols_;   // 分组列
    std::vector<Condition> having_conds_; // HAVING 条件
    std::vector<ColMeta> output_meta_;    // 输出列元数据
    size_t len_;                          // 输出记录长度

    // 内部结果
    struct AggRow {
        std::unique_ptr<RmRecord> row;
    };
    std::vector<std::unique_ptr<RmRecord>> results_;
    size_t cursor_;

    /** 构建分组键：从记录中提取 GROUP BY 列的值拼接为字符串 */
    std::string build_group_key(const RmRecord& rec, const std::vector<ColMeta>& child_cols) {
        std::string key;
        for (auto& gb : group_by_cols_) {
            for (auto& col : child_cols) {
                if (col.tab_name == gb.tab_name && col.name == gb.col_name) {
                    key.append(rec.data + col.offset, col.len);
                    break;
                }
            }
        }
        return key;
    }

    /** 从记录中读取列的值 */
    template<typename T>
    T get_col_value(const RmRecord& rec, const TabCol& tc, const std::vector<ColMeta>& child_cols) {
        for (auto& col : child_cols) {
            if (col.tab_name == tc.tab_name && col.name == tc.col_name) {
                if (col.type == TYPE_INT) {
                    return static_cast<T>(*(int*)(rec.data + col.offset));
                } else if (col.type == TYPE_FLOAT) {
                    return static_cast<T>(*(float*)(rec.data + col.offset));
                }
            }
        }
        return T{};
    }

    /** 评估 HAVING 条件（对聚合结果行） */
    bool eval_having(const RmRecord& rec) {
        for (auto& cond : having_conds_) {
            // 在输出列中查找 LHS 列
            int lhs_idx = -1;
            for (size_t i = 0; i < output_meta_.size(); i++) {
                if (output_meta_[i].name == cond.lhs_col.col_name) {
                    lhs_idx = i;
                    break;
                }
            }
            if (lhs_idx < 0) continue;  // 列未找到，跳过

            auto& meta = output_meta_[lhs_idx];
            bool result = false;
            if (meta.type == TYPE_INT) {
                int lhs_val = *(int*)(rec.data + meta.offset);
                int rhs_val = cond.rhs_val.int_val;
                result = cmp_int(lhs_val, rhs_val, cond.op);
            } else if (meta.type == TYPE_FLOAT) {
                float lhs_val = *(float*)(rec.data + meta.offset);
                float rhs_val = cond.rhs_val.float_val;
                result = cmp_float(lhs_val, rhs_val, cond.op);
            }
            if (!result) return false;
        }
        return true;
    }

    static bool cmp_int(int lhs, int rhs, CompOp op) {
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

    static bool cmp_float(float lhs, float rhs, CompOp op) {
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

   public:
    AggregationExecutor(std::unique_ptr<AbstractExecutor> prev,
                        std::vector<int> agg_types,
                        std::vector<TabCol> agg_targets,
                        std::vector<TabCol> group_by_cols,
                        std::vector<Condition> having_conds,
                        std::vector<ColMeta> output_meta) {
        prev_ = std::move(prev);
        agg_types_ = std::move(agg_types);
        agg_targets_ = std::move(agg_targets);
        group_by_cols_ = std::move(group_by_cols);
        having_conds_ = std::move(having_conds);
        output_meta_ = std::move(output_meta);
        len_ = 0;
        for (auto& meta : output_meta_) {
            len_ = std::max(len_, (size_t)(meta.offset + meta.len));
        }
        cursor_ = 0;
    }

    void beginTuple() override {
        results_.clear();
        auto& child_cols = prev_->cols();

        if (group_by_cols_.empty()) {
            // ===== 无 GROUP BY：所有行为一组 =====
            prev_->beginTuple();

            // 初始化聚合状态
            std::vector<int> agg_int_max;     // MAX for int
            std::vector<int> agg_int_min;     // MIN for int
            std::vector<float> agg_float_max;  // MAX for float
            std::vector<float> agg_float_min;  // MIN for float
            std::vector<int> agg_count;        // COUNT(col)
            std::vector<int> agg_sum_int;      // SUM for int
            std::vector<float> agg_sum_float;  // SUM for float
            int count_star = 0;

            // 初始化聚合状态
            for (size_t i = 0; i < agg_types_.size(); i++) {
                int at = agg_types_[i];
                if (at == ast::AGG_MAX || at == ast::AGG_MIN) {
                    agg_int_max.push_back(INT_MIN);
                    agg_int_min.push_back(INT_MAX);
                    agg_float_max.push_back(-FLT_MAX);
                    agg_float_min.push_back(FLT_MAX);
                } else if (at == ast::AGG_COUNT) {
                    agg_count.push_back(0);
                } else if (at == ast::AGG_SUM) {
                    agg_sum_int.push_back(0);
                    agg_sum_float.push_back(0.0f);
                }
            }

            bool has_data = false;
            for (; !prev_->is_end(); prev_->nextTuple()) {
                auto rec = prev_->Next();
                if (!rec) continue;
                has_data = true;
                count_star++;

                // 更新聚合
                int max_idx = 0, min_idx = 0, count_idx = 0, sum_idx = 0;
                for (size_t i = 0; i < agg_types_.size(); i++) {
                    int at = agg_types_[i];
                    if (at == ast::AGG_NONE) continue;

                    // 获取目标列类型和值
                    if (at == ast::AGG_COUNT_STAR) continue;

                    ColType target_type = TYPE_INT;
                    int int_val = 0;
                    float float_val = 0.0f;
                    for (auto& col : child_cols) {
                        if (col.tab_name == agg_targets_[i].tab_name &&
                            col.name == agg_targets_[i].col_name) {
                            target_type = col.type;
                            if (col.type == TYPE_INT) {
                                int_val = *(int*)(rec->data + col.offset);
                            } else if (col.type == TYPE_FLOAT) {
                                float_val = *(float*)(rec->data + col.offset);
                            }
                            break;
                        }
                    }

                    switch (at) {
                        case ast::AGG_MAX:
                            if (target_type == TYPE_INT) {
                                if (int_val > agg_int_max[max_idx]) agg_int_max[max_idx] = int_val;
                            } else {
                                if (float_val > agg_float_max[max_idx]) agg_float_max[max_idx] = float_val;
                            }
                            max_idx++;
                            break;
                        case ast::AGG_MIN:
                            if (target_type == TYPE_INT) {
                                if (int_val < agg_int_min[min_idx]) agg_int_min[min_idx] = int_val;
                            } else {
                                if (float_val < agg_float_min[min_idx]) agg_float_min[min_idx] = float_val;
                            }
                            min_idx++;
                            break;
                        case ast::AGG_COUNT:
                            agg_count[count_idx]++;
                            count_idx++;
                            break;
                        case ast::AGG_SUM:
                            if (target_type == TYPE_INT) {
                                agg_sum_int[sum_idx] += int_val;
                            } else {
                                agg_sum_float[sum_idx] += float_val;
                            }
                            sum_idx++;
                            break;
                    }
                }
            }

            // 构建结果行
            if (has_data || !agg_types_.empty()) {
                auto result = std::make_unique<RmRecord>(len_);
                size_t agg_offset = 0;  // 无 GROUP BY 时，聚合结果从 offset 0 开始

                int max_idx = 0, min_idx = 0, count_idx = 0, sum_idx = 0;
                for (size_t i = 0; i < agg_types_.size(); i++) {
                    int at = agg_types_[i];
                    if (at == ast::AGG_NONE) continue;

                    // 找到对应的输出 metadata
                    int out_idx = 0;
                    for (size_t j = 0; j <= i && j < agg_types_.size(); j++) {
                        if (agg_types_[j] != ast::AGG_NONE) out_idx++;
                    }
                    out_idx--;

                    if (out_idx >= 0 && out_idx < (int)output_meta_.size()) {
                        auto& meta = output_meta_[out_idx];
                        switch (at) {
                            case ast::AGG_MAX:
                                if (meta.type == TYPE_INT) {
                                    *(int*)(result->data + meta.offset) = agg_int_max[max_idx++];
                                } else {
                                    *(float*)(result->data + meta.offset) = agg_float_max[max_idx++];
                                }
                                break;
                            case ast::AGG_MIN:
                                if (meta.type == TYPE_INT) {
                                    *(int*)(result->data + meta.offset) = agg_int_min[min_idx++];
                                } else {
                                    *(float*)(result->data + meta.offset) = agg_float_min[min_idx++];
                                }
                                break;
                            case ast::AGG_COUNT:
                                *(int*)(result->data + meta.offset) = agg_count[count_idx++];
                                break;
                            case ast::AGG_SUM:
                                if (meta.type == TYPE_INT) {
                                    *(int*)(result->data + meta.offset) = agg_sum_int[sum_idx++];
                                } else {
                                    *(float*)(result->data + meta.offset) = agg_sum_float[sum_idx++];
                                }
                                break;
                            case ast::AGG_COUNT_STAR:
                                *(int*)(result->data + meta.offset) = count_star;
                                break;
                        }
                    }
                }

                // 评估 HAVING
                if (eval_having(*result)) {
                    results_.push_back(std::move(result));
                }
            }
        } else {
            // ===== 有 GROUP BY：按分组键分组 =====
            std::map<std::string, std::vector<std::unique_ptr<RmRecord>>> groups;
            prev_->beginTuple();
            for (; !prev_->is_end(); prev_->nextTuple()) {
                auto rec = prev_->Next();
                if (!rec) continue;
                auto key = build_group_key(*rec, child_cols);
                auto copy = std::make_unique<RmRecord>(rec->size);
                memcpy(copy->data, rec->data, rec->size);
                groups[key].push_back(std::move(copy));
            }

            // 对每个组计算聚合
            for (auto& [key, records] : groups) {
                // 初始化聚合
                std::vector<int> agg_int_max, agg_int_min;
                std::vector<float> agg_float_max, agg_float_min;
                std::vector<int> agg_count, agg_sum_int;
                std::vector<float> agg_sum_float;
                int count_star = records.size();

                for (size_t i = 0; i < agg_types_.size(); i++) {
                    int at = agg_types_[i];
                    if (at == ast::AGG_MAX || at == ast::AGG_MIN) {
                        agg_int_max.push_back(INT_MIN);
                        agg_int_min.push_back(INT_MAX);
                        agg_float_max.push_back(-FLT_MAX);
                        agg_float_min.push_back(FLT_MAX);
                    } else if (at == ast::AGG_COUNT || at == ast::AGG_SUM) {
                        if (at == ast::AGG_COUNT) agg_count.push_back(0);
                        if (at == ast::AGG_SUM) {
                            agg_sum_int.push_back(0);
                            agg_sum_float.push_back(0.0f);
                        }
                    }
                }

                for (auto& rec : records) {
                    int max_idx = 0, min_idx = 0, count_idx = 0, sum_idx = 0;
                    for (size_t i = 0; i < agg_types_.size(); i++) {
                        int at = agg_types_[i];
                        if (at == ast::AGG_NONE || at == ast::AGG_COUNT_STAR) continue;

                        ColType target_type = TYPE_INT;
                        int int_val = 0;
                        float float_val = 0.0f;
                        for (auto& col : child_cols) {
                            if (col.tab_name == agg_targets_[i].tab_name &&
                                col.name == agg_targets_[i].col_name) {
                                target_type = col.type;
                                if (col.type == TYPE_INT) {
                                    int_val = *(int*)(rec->data + col.offset);
                                } else if (col.type == TYPE_FLOAT) {
                                    float_val = *(float*)(rec->data + col.offset);
                                }
                                break;
                            }
                        }

                        switch (at) {
                            case ast::AGG_MAX:
                                if (target_type == TYPE_INT) {
                                    if (int_val > agg_int_max[max_idx]) agg_int_max[max_idx] = int_val;
                                } else {
                                    if (float_val > agg_float_max[max_idx]) agg_float_max[max_idx] = float_val;
                                }
                                max_idx++;
                                break;
                            case ast::AGG_MIN:
                                if (target_type == TYPE_INT) {
                                    if (int_val < agg_int_min[min_idx]) agg_int_min[min_idx] = int_val;
                                } else {
                                    if (float_val < agg_float_min[min_idx]) agg_float_min[min_idx] = float_val;
                                }
                                min_idx++;
                                break;
                            case ast::AGG_COUNT:
                                agg_count[count_idx]++;
                                count_idx++;
                                break;
                            case ast::AGG_SUM:
                                if (target_type == TYPE_INT) {
                                    agg_sum_int[sum_idx] += int_val;
                                } else {
                                    agg_sum_float[sum_idx] += float_val;
                                }
                                sum_idx++;
                                break;
                        }
                    }
                }

                // 构建结果行
                auto result = std::make_unique<RmRecord>(len_);

                // 先写入 GROUP BY 列
                for (size_t gi = 0; gi < group_by_cols_.size(); gi++) {
                    if (gi < output_meta_.size()) {
                        auto& meta = output_meta_[gi];
                        // 从第一行记录中提取分组列值
                        auto& first_rec = records[0];
                        for (auto& col : child_cols) {
                            if (col.tab_name == group_by_cols_[gi].tab_name &&
                                col.name == group_by_cols_[gi].col_name) {
                                memcpy(result->data + meta.offset,
                                       first_rec->data + col.offset, col.len);
                                break;
                            }
                        }
                    }
                }

                // 再写入聚合结果
                int max_idx = 0, min_idx = 0, count_idx = 0, sum_idx = 0;
                for (size_t i = 0; i < agg_types_.size(); i++) {
                    int at = agg_types_[i];
                    if (at == ast::AGG_NONE) continue;

                    // 找到输出 meta 的索引
                    int meta_idx = group_by_cols_.size();
                    for (size_t j = 0; j < i; j++) {
                        if (agg_types_[j] != ast::AGG_NONE) meta_idx++;
                    }

                    if (meta_idx >= (int)output_meta_.size()) continue;

                    auto& meta = output_meta_[meta_idx];
                    switch (at) {
                        case ast::AGG_MAX:
                            if (meta.type == TYPE_INT) {
                                *(int*)(result->data + meta.offset) = agg_int_max[max_idx++];
                            } else {
                                *(float*)(result->data + meta.offset) = agg_float_max[max_idx++];
                            }
                            break;
                        case ast::AGG_MIN:
                            if (meta.type == TYPE_INT) {
                                *(int*)(result->data + meta.offset) = agg_int_min[min_idx++];
                            } else {
                                *(float*)(result->data + meta.offset) = agg_float_min[min_idx++];
                            }
                            break;
                        case ast::AGG_COUNT:
                            *(int*)(result->data + meta.offset) = agg_count[count_idx++];
                            break;
                        case ast::AGG_SUM:
                            if (meta.type == TYPE_INT) {
                                *(int*)(result->data + meta.offset) = agg_sum_int[sum_idx++];
                            } else {
                                *(float*)(result->data + meta.offset) = agg_sum_float[sum_idx++];
                            }
                            break;
                        case ast::AGG_COUNT_STAR:
                            *(int*)(result->data + meta.offset) = count_star;
                            break;
                    }
                }

                // 评估 HAVING
                if (eval_having(*result)) {
                    results_.push_back(std::move(result));
                }
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

    const std::vector<ColMeta> &cols() const override {
        return output_meta_;
    }

    size_t tupleLen() const override { return len_; }

    Rid &rid() override { return _abstract_rid; }
};