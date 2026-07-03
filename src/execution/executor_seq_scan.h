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
#include "record/rm_scan.h"
#include "system/sm.h"

class SeqScanExecutor : public AbstractExecutor {
   private:
    std::string tab_name_;              // 表的名称
    std::vector<Condition> conds_;      // scan的条件
    RmFileHandle *fh_;                  // 表的数据文件句柄
    std::vector<ColMeta> cols_;         // scan后生成的记录的字段
    size_t len_;                        // scan后生成的每条记录的长度
    std::vector<Condition> fed_conds_;  // 同conds_，两个字段相同

    Rid rid_;
    std::unique_ptr<RecScan> scan_;     // table_iterator
    bool is_end_;                       // 是否已扫描结束

    SmManager *sm_manager_;

    /** 判断一条记录是否满足所有条件（AND 连接） */
    bool eval_conds(const RmRecord& rec) {
        // 逐条件比对，所有条件必须满足
        for (auto& cond : fed_conds_) {
            // 找到左操作数列在记录中的偏移
            auto lhs_it = std::find_if(cols_.begin(), cols_.end(),
                [&](const ColMeta& col) { return col.name == cond.lhs_col.col_name; });
            if (lhs_it == cols_.end()) continue;
            char* lhs_data = rec.data + lhs_it->offset;

            // 如果右操作数是列引用（表连接场景），当前简化处理
            if (!cond.is_rhs_val) {
                continue;  // 单表扫描场景下 rhs 总是值
            }

            // 根据类型进行比较
            ColType type = lhs_it->type;
            if (type == TYPE_INT) {
                int lhs_val = *(int*)lhs_data;
                int rhs_val = cond.rhs_val.int_val;
                if (!cmp_int(lhs_val, rhs_val, cond.op)) return false;
            } else if (type == TYPE_FLOAT) {
                float lhs_val = *(float*)lhs_data;
                float rhs_val = cond.rhs_val.float_val;
                if (!cmp_float(lhs_val, rhs_val, cond.op)) return false;
            } else if (type == TYPE_STRING) {
                std::string lhs_str(lhs_data, lhs_it->len);
                // 去除尾部填充的 '\0'
                lhs_str.resize(strlen(lhs_str.c_str()));
                std::string rhs_str = cond.rhs_val.str_val;
                if (!cmp_string(lhs_str, rhs_str, cond.op)) return false;
            }
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

    static bool cmp_string(const std::string& lhs, const std::string& rhs, CompOp op) {
        int cmp = lhs.compare(rhs);
        switch (op) {
            case OP_EQ: return cmp == 0;
            case OP_NE: return cmp != 0;
            case OP_LT: return cmp < 0;
            case OP_GT: return cmp > 0;
            case OP_LE: return cmp <= 0;
            case OP_GE: return cmp >= 0;
            default: return false;
        }
    }

   public:
    SeqScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = std::move(tab_name);
        conds_ = std::move(conds);
        TabMeta &tab = sm_manager_->db_.get_table(tab_name_);
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab.cols;
        len_ = cols_.back().offset + cols_.back().len;

        context_ = context;

        fed_conds_ = conds_;
        is_end_ = true;
    }

    void beginTuple() override {
        // 创建 RmScan 迭代器
        scan_ = std::make_unique<RmScan>(fh_);
        is_end_ = scan_->is_end();
        // 定位到第一条满足 WHERE 条件的记录
        if (!is_end_) {
            rid_ = scan_->rid();
            // 检查当前记录是否满足条件
            auto rec = fh_->get_record(rid_, context_);
            if (!eval_conds(*rec)) {
                nextTuple();
            }
        }
    }

    void nextTuple() override {
        // 向后扫描找到下一条满足条件的记录
        while (!scan_->is_end()) {
            scan_->next();
            if (scan_->is_end()) {
                is_end_ = true;
                return;
            }
            rid_ = scan_->rid();
            auto rec = fh_->get_record(rid_, context_);
            if (eval_conds(*rec)) {
                is_end_ = false;
                return;
            }
        }
        is_end_ = true;
    }

    bool is_end() const override { return is_end_; }

    std::unique_ptr<RmRecord> Next() override {
        // 读取当前记录并返回
        if (is_end_) return nullptr;
        return fh_->get_record(rid_, context_);
    }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    size_t tupleLen() const override { return len_; }

    Rid &rid() override { return rid_; }
};