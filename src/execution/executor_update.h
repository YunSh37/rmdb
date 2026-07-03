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

class UpdateExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<Rid> rids_;
    std::string tab_name_;
    std::vector<SetClause> set_clauses_;
    SmManager *sm_manager_;

   public:
    UpdateExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<SetClause> set_clauses,
                   std::vector<Condition> conds, std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        set_clauses_ = set_clauses;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;
    }
    std::unique_ptr<RmRecord> Next() override {
        // 遍历所有需要更新的记录
        for (auto& rid : rids_) {
            // 1. 读取当前记录
            auto rec = fh_->get_record(rid, context_);
            // 2. 对每个 set_clause 修改对应列
            for (auto& clause : set_clauses_) {
                // 找到要更新的列
                auto col_it = std::find_if(tab_.cols.begin(), tab_.cols.end(),
                    [&](const ColMeta& col) { return col.name == clause.lhs.col_name; });
                if (col_it == tab_.cols.end()) {
                    throw ColumnNotFoundError(clause.lhs.col_name);
                }
                // 类型检查
                if (col_it->type != clause.rhs.type) {
                    throw IncompatibleTypeError(coltype2str(col_it->type), coltype2str(clause.rhs.type));
                }
                // 确保 rhs 的 raw 数据已初始化
                if (clause.rhs.raw == nullptr) {
                    Value val = clause.rhs;
                    val.init_raw(col_it->len);
                    memcpy(rec->data + col_it->offset, val.raw->data, col_it->len);
                } else {
                    memcpy(rec->data + col_it->offset, clause.rhs.raw->data, col_it->len);
                }
            }
            // 3. 写回修改后的记录
            fh_->update_record(rid, rec->data, context_);
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};