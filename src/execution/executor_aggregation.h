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
    std::vector<std::unique_ptr<RmRecord>> results_;
    size_t cursor_;

    // ===== 聚合状态辅助结构 =====

    /** 聚合状态：分别跟踪 INT / BIGINT+DATETIME / FLOAT / STRING 四种类型 */
    struct AggState {
        // MAX 状态
        int     int_max;
        int64_t bigint_max;
        float   float_max;
        std::string str_max;

        // MIN 状态
        int     int_min;
        int64_t bigint_min;
        float   float_min;
        std::string str_min;

        int     count_val;
        int64_t sum_bigint;
        float   sum_float;

        // 目标列的实际类型（用于判断使用哪个分支）
        ColType target_type;

        void init_max() {
            int_max = INT_MIN;
            bigint_max = INT64_MIN;
            float_max = -FLT_MAX;
            str_max = "";       // 空串表示未初始化
        }
        void init_min() {
            int_min = INT_MAX;
            bigint_min = INT64_MAX;
            float_min = FLT_MAX;
            str_min = "";       // 空串表示未初始化
        }
        void init_count() {
            count_val = 0;
        }
        void init_sum() {
            sum_bigint = 0;
            sum_float = 0.0f;
        }
    };

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

    /** 从记录中读取列的类型和值 */
    struct ColValue {
        ColType type = TYPE_INT;
        int int_val = 0;
        int64_t bigint_val = 0;
        float float_val = 0.0f;
        std::string str_val;
    };

    ColValue read_col(const RmRecord& rec, const TabCol& tc, const std::vector<ColMeta>& child_cols) {
        ColValue cv;
        for (auto& col : child_cols) {
            if (col.tab_name == tc.tab_name && col.name == tc.col_name) {
                cv.type = col.type;
                if (col.type == TYPE_INT) {
                    cv.int_val = *(int*)(rec.data + col.offset);
                } else if (col.type == TYPE_BIGINT || col.type == TYPE_DATETIME) {
                    cv.bigint_val = *(int64_t*)(rec.data + col.offset);
                } else if (col.type == TYPE_FLOAT) {
                    cv.float_val = *(float*)(rec.data + col.offset);
                } else if (col.type == TYPE_STRING) {
                    cv.str_val = std::string(rec.data + col.offset, col.len);
                    // 去除尾部空白填充
                    size_t end = cv.str_val.find_last_not_of(" \0");
                    if (end != std::string::npos) {
                        cv.str_val = cv.str_val.substr(0, end + 1);
                    } else {
                        cv.str_val.clear();
                    }
                }
                break;
            }
        }
        return cv;
    }

    /** 评估 HAVING 条件（对聚合结果行） */
    bool eval_having(const RmRecord& rec) {
        for (auto& cond : having_conds_) {
            int lhs_idx = -1;
            for (size_t i = 0; i < output_meta_.size(); i++) {
                if (output_meta_[i].name == cond.lhs_col.col_name) {
                    lhs_idx = i;
                    break;
                }
            }
            if (lhs_idx < 0) continue;

            auto& meta = output_meta_[lhs_idx];
            bool result = false;
            if (meta.type == TYPE_INT) {
                int lhs_val = *(int*)(rec.data + meta.offset);
                int rhs_val = cond.rhs_val.int_val;
                result = cmp_int(lhs_val, rhs_val, cond.op);
            } else if (meta.type == TYPE_BIGINT || meta.type == TYPE_DATETIME) {
                int64_t lhs_val = *(int64_t*)(rec.data + meta.offset);
                int64_t rhs_val = (meta.type == TYPE_DATETIME) ? cond.rhs_val.datetime_val : cond.rhs_val.bigint_val;
                result = cmp_bigint(lhs_val, rhs_val, cond.op);
            } else if (meta.type == TYPE_FLOAT) {
                float lhs_val = *(float*)(rec.data + meta.offset);
                float rhs_val = cond.rhs_val.float_val;
                result = cmp_float(lhs_val, rhs_val, cond.op);
            } else if (meta.type == TYPE_STRING) {
                std::string lhs_val(rec.data + meta.offset, meta.len);
                size_t end = lhs_val.find_last_not_of(" \0");
                if (end != std::string::npos) lhs_val = lhs_val.substr(0, end + 1);
                std::string rhs_val = cond.rhs_val.str_val;
                result = cmp_str(lhs_val, rhs_val, cond.op);
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

    static bool cmp_bigint(int64_t lhs, int64_t rhs, CompOp op) {
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

    static bool cmp_str(const std::string& lhs, const std::string& rhs, CompOp op) {
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

    /** 更新一行数据的聚合状态 */
    void update_agg(AggState& st, int agg_type, const ColValue& cv) {
        switch (agg_type) {
            case ast::AGG_MAX:
                if (cv.type == TYPE_BIGINT || cv.type == TYPE_DATETIME) {
                    if (cv.bigint_val > st.bigint_max) st.bigint_max = cv.bigint_val;
                } else if (cv.type == TYPE_INT) {
                    if (cv.int_val > st.int_max) st.int_max = cv.int_val;
                } else if (cv.type == TYPE_FLOAT) {
                    if (cv.float_val > st.float_max) st.float_max = cv.float_val;
                } else if (cv.type == TYPE_STRING) {
                    if (st.str_max.empty() || cv.str_val > st.str_max) st.str_max = cv.str_val;
                }
                break;
            case ast::AGG_MIN:
                if (cv.type == TYPE_BIGINT || cv.type == TYPE_DATETIME) {
                    if (cv.bigint_val < st.bigint_min) st.bigint_min = cv.bigint_val;
                } else if (cv.type == TYPE_INT) {
                    if (cv.int_val < st.int_min) st.int_min = cv.int_val;
                } else if (cv.type == TYPE_FLOAT) {
                    if (cv.float_val < st.float_min) st.float_min = cv.float_val;
                } else if (cv.type == TYPE_STRING) {
                    if (st.str_min.empty() || cv.str_val < st.str_min) st.str_min = cv.str_val;
                }
                break;
            case ast::AGG_COUNT:
                st.count_val++;
                break;
            case ast::AGG_SUM:
                if (cv.type == TYPE_BIGINT || cv.type == TYPE_INT || cv.type == TYPE_DATETIME) {
                    st.sum_bigint += (cv.type == TYPE_INT) ? cv.int_val : cv.bigint_val;
                } else {
                    st.sum_float += cv.float_val;
                }
                break;
        }
    }

    /** 从聚合状态写入结果记录 */
    void write_agg_result(RmRecord& result, const AggState& st, int agg_type,
                          const ColMeta& meta) {
        switch (agg_type) {
            case ast::AGG_MAX:
                if (meta.type == TYPE_BIGINT || meta.type == TYPE_DATETIME) {
                    *(int64_t*)(result.data + meta.offset) = st.bigint_max;
                } else if (meta.type == TYPE_INT) {
                    *(int*)(result.data + meta.offset) = st.int_max;
                } else if (meta.type == TYPE_FLOAT) {
                    *(float*)(result.data + meta.offset) = st.float_max;
                } else if (meta.type == TYPE_STRING) {
                    memset(result.data + meta.offset, 0, meta.len);
                    memcpy(result.data + meta.offset, st.str_max.c_str(),
                           std::min(st.str_max.size(), (size_t)meta.len));
                }
                break;
            case ast::AGG_MIN:
                if (meta.type == TYPE_BIGINT || meta.type == TYPE_DATETIME) {
                    *(int64_t*)(result.data + meta.offset) = st.bigint_min;
                } else if (meta.type == TYPE_INT) {
                    *(int*)(result.data + meta.offset) = st.int_min;
                } else if (meta.type == TYPE_FLOAT) {
                    *(float*)(result.data + meta.offset) = st.float_min;
                } else if (meta.type == TYPE_STRING) {
                    memset(result.data + meta.offset, 0, meta.len);
                    memcpy(result.data + meta.offset, st.str_min.c_str(),
                           std::min(st.str_min.size(), (size_t)meta.len));
                }
                break;
            case ast::AGG_COUNT:
                *(int*)(result.data + meta.offset) = st.count_val;
                break;
            case ast::AGG_SUM:
                if (meta.type == TYPE_BIGINT || meta.type == TYPE_DATETIME) {
                    *(int64_t*)(result.data + meta.offset) = st.sum_bigint;
                } else if (meta.type == TYPE_INT) {
                    *(int*)(result.data + meta.offset) = (int)st.sum_bigint;
                } else {
                    *(float*)(result.data + meta.offset) = st.sum_float;
                }
                break;
            case ast::AGG_COUNT_STAR:
                *(int*)(result.data + meta.offset) = st.count_val;
                break;
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

        // ---- 获取每个聚合函数的目标列类型（用于 AggState 初始化）----
        std::vector<ColType> target_types(agg_types_.size(), TYPE_INT);
        for (size_t i = 0; i < agg_types_.size(); i++) {
            if (agg_types_[i] == ast::AGG_NONE || agg_types_[i] == ast::AGG_COUNT_STAR) continue;
            for (auto& col : child_cols) {
                if (col.tab_name == agg_targets_[i].tab_name &&
                    col.name == agg_targets_[i].col_name) {
                    target_types[i] = col.type;
                    break;
                }
            }
        }

        if (group_by_cols_.empty()) {
            // ==================== 无 GROUP BY ====================
            prev_->beginTuple();

            std::vector<AggState> states(agg_types_.size());
            int count_star = 0;

            for (size_t i = 0; i < agg_types_.size(); i++) {
                states[i].target_type = target_types[i];
                int at = agg_types_[i];
                if (at == ast::AGG_MAX) states[i].init_max();
                else if (at == ast::AGG_MIN) states[i].init_min();
                else if (at == ast::AGG_COUNT || at == ast::AGG_COUNT_STAR) states[i].init_count();
                else if (at == ast::AGG_SUM) states[i].init_sum();
            }

            bool has_data = false;
            for (; !prev_->is_end(); prev_->nextTuple()) {
                auto rec = prev_->Next();
                if (!rec) continue;
                has_data = true;
                count_star++;

                for (size_t i = 0; i < agg_types_.size(); i++) {
                    int at = agg_types_[i];
                    if (at == ast::AGG_NONE) continue;
                    if (at == ast::AGG_COUNT_STAR) continue;

                    auto cv = read_col(*rec, agg_targets_[i], child_cols);
                    update_agg(states[i], at, cv);
                }
            }

            if (has_data || !agg_types_.empty()) {
                auto result = std::make_unique<RmRecord>(len_);

                for (size_t i = 0; i < agg_types_.size(); i++) {
                    int at = agg_types_[i];
                    if (at == ast::AGG_NONE) continue;

                    // 找到输出 meta 的索引
                    int meta_idx = 0;
                    for (size_t j = 0; j <= i && j < agg_types_.size(); j++) {
                        if (agg_types_[j] != ast::AGG_NONE) meta_idx++;
                    }
                    meta_idx--;

                    if (meta_idx >= 0 && meta_idx < (int)output_meta_.size()) {
                        if (at == ast::AGG_COUNT_STAR) {
                            *(int*)(result->data + output_meta_[meta_idx].offset) = count_star;
                        } else {
                            write_agg_result(*result, states[i], at, output_meta_[meta_idx]);
                        }
                    }
                }

                if (eval_having(*result)) {
                    results_.push_back(std::move(result));
                }
            }
        } else {
            // ==================== 有 GROUP BY ====================
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

            for (auto& [key, records] : groups) {
                std::vector<AggState> states(agg_types_.size());
                int count_star = (int)records.size();

                for (size_t i = 0; i < agg_types_.size(); i++) {
                    states[i].target_type = target_types[i];
                    int at = agg_types_[i];
                    if (at == ast::AGG_MAX) states[i].init_max();
                    else if (at == ast::AGG_MIN) states[i].init_min();
                    else if (at == ast::AGG_COUNT || at == ast::AGG_COUNT_STAR) states[i].init_count();
                    else if (at == ast::AGG_SUM) states[i].init_sum();
                }

                for (auto& rec : records) {
                    for (size_t i = 0; i < agg_types_.size(); i++) {
                        int at = agg_types_[i];
                        if (at == ast::AGG_NONE || at == ast::AGG_COUNT_STAR) continue;
                        auto cv = read_col(*rec, agg_targets_[i], child_cols);
                        update_agg(states[i], at, cv);
                    }
                }

                auto result = std::make_unique<RmRecord>(len_);

                // 先写入 GROUP BY 列
                for (size_t gi = 0; gi < group_by_cols_.size(); gi++) {
                    if (gi < output_meta_.size()) {
                        auto& meta = output_meta_[gi];
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
                for (size_t i = 0; i < agg_types_.size(); i++) {
                    int at = agg_types_[i];
                    if (at == ast::AGG_NONE) continue;

                    int meta_idx = (int)group_by_cols_.size();
                    for (size_t j = 0; j < i; j++) {
                        if (agg_types_[j] != ast::AGG_NONE) meta_idx++;
                    }

                    if (meta_idx < (int)output_meta_.size()) {
                        if (at == ast::AGG_COUNT_STAR) {
                            *(int*)(result->data + output_meta_[meta_idx].offset) = count_star;
                        } else {
                            write_agg_result(*result, states[i], at, output_meta_[meta_idx]);
                        }
                    }
                }

                if (eval_having(*result)) {
                    results_.push_back(std::move(result));
                }
            }
        }
        cursor_ = 0;
    }

    void nextTuple() override { cursor_++; }

    bool is_end() const override { return cursor_ >= results_.size(); }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) return nullptr;
        auto& rec = results_[cursor_];
        auto result = std::make_unique<RmRecord>(len_);
        memcpy(result->data, rec->data, len_);
        return result;
    }

    const std::vector<ColMeta> &cols() const override { return output_meta_; }

    size_t tupleLen() const override { return len_; }

    Rid &rid() override { return _abstract_rid; }
};