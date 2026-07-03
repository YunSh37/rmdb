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

class SortExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<ColMeta> sort_col_metas_;       // 排序键列的元数据（在子执行器 cols 中的偏移）
    std::vector<bool> is_desc_;                  // 每列排序方向
    size_t tuple_num;
    int limit_;                                  // LIMIT 值，-1 表示无限制
    std::vector<std::unique_ptr<RmRecord>> records_;  // 所有排序后的记录
    size_t cursor_;                              // 当前返回位置
    std::unique_ptr<RmRecord> current_tuple_;
    std::vector<ColMeta> output_cols_;           // 输出列（与子执行器相同）

   public:
    SortExecutor(std::unique_ptr<AbstractExecutor> prev,
                 std::vector<TabCol> sort_cols,
                 std::vector<bool> is_desc,
                 int limit = -1) {
        prev_ = std::move(prev);
        is_desc_ = std::move(is_desc);
        limit_ = limit;
        tuple_num = 0;
        cursor_ = 0;

        // 查找排序键列在子执行器输出中的偏移
        auto& prev_cols = prev_->cols();
        for (auto& sc : sort_cols) {
            for (auto& col : prev_cols) {
                if (col.tab_name == sc.tab_name && col.name == sc.col_name) {
                    sort_col_metas_.push_back(col);
                    break;
                }
            }
        }
        // 如果排序列不够（可能只有部分列匹配），补足
        while (sort_col_metas_.size() < sort_cols.size()) {
            sort_col_metas_.push_back(ColMeta{});
        }
        output_cols_ = prev_cols;
    }

    void beginTuple() override {
        // 从子执行器读取所有记录
        records_.clear();
        prev_->beginTuple();
        for (; !prev_->is_end(); prev_->nextTuple()) {
            auto rec = prev_->Next();
            if (rec) {
                records_.push_back(std::move(rec));
            }
        }

        // 排序
        std::sort(records_.begin(), records_.end(),
            [this](const std::unique_ptr<RmRecord>& a,
                   const std::unique_ptr<RmRecord>& b) -> bool {
                return compare_records(*a, *b, 0);
            });

        // 应用 LIMIT
        if (limit_ > 0 && (int)records_.size() > limit_) {
            records_.resize(limit_);
        }

        cursor_ = 0;
    }

    /** 递归多键比较 */
    bool compare_records(const RmRecord& a, const RmRecord& b, size_t key_idx) {
        if (key_idx >= sort_col_metas_.size()) return false;

        auto& col_meta = sort_col_metas_[key_idx];
        bool desc = (key_idx < is_desc_.size()) ? is_desc_[key_idx] : false;
        ColType type = col_meta.type;

        int cmp = 0;
        if (type == TYPE_INT) {
            int va = *(int*)(a.data + col_meta.offset);
            int vb = *(int*)(b.data + col_meta.offset);
            if (va < vb) cmp = -1;
            else if (va > vb) cmp = 1;
        } else if (type == TYPE_FLOAT) {
            float va = *(float*)(a.data + col_meta.offset);
            float vb = *(float*)(b.data + col_meta.offset);
            if (va < vb) cmp = -1;
            else if (va > vb) cmp = 1;
        } else if (type == TYPE_STRING) {
            std::string sa((char*)(a.data + col_meta.offset), col_meta.len);
            std::string sb((char*)(b.data + col_meta.offset), col_meta.len);
            sa.resize(strlen(sa.c_str()));
            sb.resize(strlen(sb.c_str()));
            cmp = sa.compare(sb);
        }

        if (desc) cmp = -cmp;

        if (cmp != 0) return cmp < 0;
        // 当前键相等，递归比较下一个键
        return compare_records(a, b, key_idx + 1);
    }

    void nextTuple() override {
        cursor_++;
    }

    bool is_end() const override {
        return cursor_ >= records_.size();
    }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) return nullptr;
        auto& rec = records_[cursor_];
        auto result = std::make_unique<RmRecord>(rec->size);
        memcpy(result->data, rec->data, rec->size);
        return result;
    }

    const std::vector<ColMeta> &cols() const override { return output_cols_; }

    size_t tupleLen() const override { return prev_->tupleLen(); }

    Rid &rid() override { return _abstract_rid; }
};