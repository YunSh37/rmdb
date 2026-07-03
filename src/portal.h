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
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <string>
#include <sstream>
#include "optimizer/plan.h"
#include "execution/executor_abstract.h"
#include "execution/executor_nestedloop_join.h"
#include "execution/executor_projection.h"
#include "execution/executor_seq_scan.h"
#include "execution/executor_index_scan.h"
#include "execution/executor_update.h"
#include "execution/executor_insert.h"
#include "execution/executor_delete.h"
#include "execution/executor_aggregation.h"
#include "execution/execution_sort.h"
#include "common/common.h"

typedef enum portalTag{
    PORTAL_Invalid_Query = 0,
    PORTAL_ONE_SELECT,
    PORTAL_DML_WITHOUT_SELECT,
    PORTAL_MULTI_QUERY,
    PORTAL_CMD_UTILITY
} portalTag;


struct PortalStmt {
    portalTag tag;

    std::vector<TabCol> sel_cols;
    std::unique_ptr<AbstractExecutor> root;
    std::shared_ptr<Plan> plan;

    PortalStmt(portalTag tag_, std::vector<TabCol> sel_cols_, std::unique_ptr<AbstractExecutor> root_, std::shared_ptr<Plan> plan_) :
            tag(tag_), sel_cols(std::move(sel_cols_)), root(std::move(root_)), plan(std::move(plan_)) {}
};

class Portal
{
   private:
    SmManager *sm_manager_;

    /** 递归打印计划树（用于 EXPLAIN）
     *  @param aliases 别名映射：alias → real_table_name（反向查找用别名显示） */
    void explain_plan(std::shared_ptr<Plan> plan, int indent, std::stringstream &ss,
                       const std::map<std::string, std::string>& aliases = {}) {
        std::string prefix(indent * 4, ' ');  // 4空格缩进

        // 构建反向映射：real_name → alias（用于 EXPLAIN 显示别名）
        std::map<std::string, std::string> rev_alias;
        for (auto& [alias, real] : aliases) {
            rev_alias[real] = alias;
        }

        // 获取表名的显示名称（优先使用别名）
        auto display_name = [&](const std::string& tab_name) -> std::string {
            auto it = rev_alias.find(tab_name);
            return (it != rev_alias.end()) ? it->second : tab_name;
        };

        if (auto x = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
            ss << prefix << "Project(columns=[";
            if (x->is_star_) {
                ss << "*";
            } else {
                // 按字母序排序后再显示
                auto sorted_cols = x->sel_cols_;
                std::sort(sorted_cols.begin(), sorted_cols.end(),
                    [](const TabCol& a, const TabCol& b) {
                        std::string da = a.tab_name + "." + a.col_name;
                        std::string db = b.tab_name + "." + b.col_name;
                        return da < db;
                    });
                for (size_t i = 0; i < sorted_cols.size(); i++) {
                    if (i > 0) ss << ",";
                    std::string dname = display_name(sorted_cols[i].tab_name);
                    if (!dname.empty())
                        ss << dname << ".";
                    ss << sorted_cols[i].col_name;
                }
            }
            ss << "])\n";
            explain_plan(x->subplan_, indent + 1, ss, aliases);
        } else if (auto x = std::dynamic_pointer_cast<JoinPlan>(plan)) {
            ss << prefix << "Join(tables=[";
            // 收集所有涉及的表名（真实表名）并按字母序排列
            std::vector<std::string> all_tabs;
            collect_tables(x, all_tabs);
            std::sort(all_tabs.begin(), all_tabs.end());
            for (size_t i = 0; i < all_tabs.size(); i++) {
                if (i > 0) ss << ",";
                ss << all_tabs[i];
            }
            ss << "],condition=[";
            for (size_t i = 0; i < x->conds_.size(); i++) {
                if (i > 0) ss << ",";
                auto& cond = x->conds_[i];
                ss << display_name(cond.lhs_col.tab_name) << ".";
                ss << cond.lhs_col.col_name;
                ss << "=";  // join 条件始终是等值连接
                ss << display_name(cond.rhs_col.tab_name) << ".";
                ss << cond.rhs_col.col_name;
            }
            ss << "])\n";
            explain_plan(x->left_, indent + 1, ss, aliases);
            explain_plan(x->right_, indent + 1, ss, aliases);
        } else if (auto x = std::dynamic_pointer_cast<FilterPlan>(plan)) {
            ss << prefix << "Filter(condition=[";
            for (size_t i = 0; i < x->conds_.size(); i++) {
                if (i > 0) ss << ",";
                auto& cond = x->conds_[i];
                ss << display_name(cond.lhs_col.tab_name) << ".";
                ss << cond.lhs_col.col_name;
                ss << comp_op_to_str(cond.op);
                if (cond.is_rhs_val) {
                    if (cond.rhs_val.type == TYPE_INT)
                        ss << cond.rhs_val.int_val;
                    else if (cond.rhs_val.type == TYPE_FLOAT)
                        ss << std::fixed << std::setprecision(6) << cond.rhs_val.float_val;
                    else if (cond.rhs_val.type == TYPE_STRING)
                        ss << "'" << cond.rhs_val.str_val << "'";
                }
            }
            ss << "])\n";
            explain_plan(x->subplan_, indent + 1, ss, aliases);
        } else if (auto x = std::dynamic_pointer_cast<ScanPlan>(plan)) {
            ss << prefix << "Scan(table=" << x->tab_name_ << ")\n";  // 始终用真实表名
        } else if (auto x = std::dynamic_pointer_cast<SortPlan>(plan)) {
            ss << prefix << "Sort(columns=[";
            for (size_t i = 0; i < x->sort_cols_.size(); i++) {
                if (i > 0) ss << ",";
                ss << display_name(x->sort_cols_[i].tab_name) << ".";
                ss << x->sort_cols_[i].col_name;
                if (i < x->is_desc_.size() && x->is_desc_[i]) ss << ",DESC";
            }
            if (x->limit_ > 0) ss << ",limit=" << x->limit_;
            ss << "])\n";
            explain_plan(x->subplan_, indent + 1, ss, aliases);
        } else if (auto x = std::dynamic_pointer_cast<AggregationPlan>(plan)) {
            ss << prefix << "Aggregation(agg=[";
            bool first_agg = true;
            for (size_t i = 0; i < x->agg_types_.size(); i++) {
                if (x->agg_types_[i] == ast::AGG_NONE) continue;
                if (!first_agg) ss << ",";
                first_agg = false;
                switch (x->agg_types_[i]) {
                    case ast::AGG_MAX:  ss << "MAX"; break;
                    case ast::AGG_MIN:  ss << "MIN"; break;
                    case ast::AGG_COUNT: ss << "COUNT"; break;
                    case ast::AGG_SUM:  ss << "SUM"; break;
                    case ast::AGG_COUNT_STAR: ss << "COUNT(*)"; break;
                }
                if (x->agg_types_[i] != ast::AGG_COUNT_STAR && i < x->agg_targets_.size()) {
                    ss << "(" << x->agg_targets_[i].col_name << ")";
                }
            }
            if (!x->group_by_cols_.empty()) {
                ss << "],group_by=[";
                for (size_t i = 0; i < x->group_by_cols_.size(); i++) {
                    if (i > 0) ss << ",";
                    ss << x->group_by_cols_[i].col_name;
                }
            }
            ss << "])\n";
            explain_plan(x->subplan_, indent + 1, ss, aliases);
        } else if (auto x = std::dynamic_pointer_cast<DMLPlan>(plan)) {
            if (x->subplan_) explain_plan(x->subplan_, indent, ss, aliases);
        }
    }

    /** 收集 JoinPlan 子树中所有表名 */
    void collect_tables(std::shared_ptr<Plan> plan, std::vector<std::string> &tabs) {
        if (auto x = std::dynamic_pointer_cast<ScanPlan>(plan)) {
            tabs.push_back(x->tab_name_);
        } else if (auto x = std::dynamic_pointer_cast<JoinPlan>(plan)) {
            collect_tables(x->left_, tabs);
            collect_tables(x->right_, tabs);
        } else if (auto x = std::dynamic_pointer_cast<FilterPlan>(plan)) {
            collect_tables(x->subplan_, tabs);
        } else if (auto x = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
            collect_tables(x->subplan_, tabs);
        }
    }

    /** 比较运算符转字符串 */
    std::string comp_op_to_str(CompOp op) {
        switch (op) {
            case OP_EQ: return "=";
            case OP_NE: return "<>";
            case OP_LT: return "<";
            case OP_GT: return ">";
            case OP_LE: return "<=";
            case OP_GE: return ">=";
            default: return "?";
        }
    }

   public:
    Portal(SmManager *sm_manager) : sm_manager_(sm_manager){}
    ~Portal(){}

    // 将查询执行计划转换成对应的算子树
    std::shared_ptr<PortalStmt> start(std::shared_ptr<Plan> plan, Context *context)
    {
        // EXPLAIN 处理：打印计划树并返回
        if (auto x = std::dynamic_pointer_cast<ExplainPlan>(plan)) {
            std::stringstream ss;
            explain_plan(x->subplan_, 0, ss, x->aliases_);
            std::string plan_str = ss.str();
            memcpy(context->data_send_ + *(context->offset_), plan_str.c_str(), plan_str.length());
            *(context->offset_) = plan_str.length();
            return std::make_shared<PortalStmt>(PORTAL_CMD_UTILITY, std::vector<TabCol>(), std::unique_ptr<AbstractExecutor>(), plan);
        }
        // 这里可以将select进行拆分，例如：一个select，带有return的select等
        if (auto x = std::dynamic_pointer_cast<OtherPlan>(plan)) {
            return std::make_shared<PortalStmt>(PORTAL_CMD_UTILITY, std::vector<TabCol>(), std::unique_ptr<AbstractExecutor>(),plan);
        } else if(auto x = std::dynamic_pointer_cast<SetKnobPlan>(plan)) {
            return std::make_shared<PortalStmt>(PORTAL_CMD_UTILITY, std::vector<TabCol>(), std::unique_ptr<AbstractExecutor>(), plan);
        } else if (auto x = std::dynamic_pointer_cast<DDLPlan>(plan)) {
            return std::make_shared<PortalStmt>(PORTAL_MULTI_QUERY, std::vector<TabCol>(), std::unique_ptr<AbstractExecutor>(),plan);
        } else if (auto x = std::dynamic_pointer_cast<DMLPlan>(plan)) {
            switch(x->tag) {
                case T_select:
                {
                    std::shared_ptr<ProjectionPlan> p = std::dynamic_pointer_cast<ProjectionPlan>(x->subplan_);
                    std::unique_ptr<AbstractExecutor> root= convert_plan_executor(p, context);
                    return std::make_shared<PortalStmt>(PORTAL_ONE_SELECT, std::move(p->sel_cols_), std::move(root), plan);
                }

                case T_Update:
                {
                    std::unique_ptr<AbstractExecutor> scan= convert_plan_executor(x->subplan_, context);
                    std::vector<Rid> rids;
                    for (scan->beginTuple(); !scan->is_end(); scan->nextTuple()) {
                        rids.push_back(scan->rid());
                    }
                    std::unique_ptr<AbstractExecutor> root =std::make_unique<UpdateExecutor>(sm_manager_,
                                                            x->tab_name_, x->set_clauses_, x->conds_, rids, context);
                    return std::make_shared<PortalStmt>(PORTAL_DML_WITHOUT_SELECT, std::vector<TabCol>(), std::move(root), plan);
                }
                case T_Delete:
                {
                    std::unique_ptr<AbstractExecutor> scan= convert_plan_executor(x->subplan_, context);
                    std::vector<Rid> rids;
                    for (scan->beginTuple(); !scan->is_end(); scan->nextTuple()) {
                        rids.push_back(scan->rid());
                    }

                    std::unique_ptr<AbstractExecutor> root =
                        std::make_unique<DeleteExecutor>(sm_manager_, x->tab_name_, x->conds_, rids, context);

                    return std::make_shared<PortalStmt>(PORTAL_DML_WITHOUT_SELECT, std::vector<TabCol>(), std::move(root), plan);
                }

                case T_Insert:
                {
                    std::unique_ptr<AbstractExecutor> root =
                            std::make_unique<InsertExecutor>(sm_manager_, x->tab_name_, x->values_, context);

                    return std::make_shared<PortalStmt>(PORTAL_DML_WITHOUT_SELECT, std::vector<TabCol>(), std::move(root), plan);
                }


                default:
                    throw InternalError("Unexpected field type");
                    break;
            }
        } else {
            throw InternalError("Unexpected field type");
        }
        return nullptr;
    }

    // 遍历算子树并执行算子生成执行结果
    void run(std::shared_ptr<PortalStmt> portal, QlManager* ql, txn_id_t *txn_id, Context *context){
        switch(portal->tag) {
            case PORTAL_ONE_SELECT:
            {
                ql->select_from(std::move(portal->root), std::move(portal->sel_cols), context);
                break;
            }

            case PORTAL_DML_WITHOUT_SELECT:
            {
                ql->run_dml(std::move(portal->root));
                break;
            }
            case PORTAL_MULTI_QUERY:
            {
                ql->run_mutli_query(portal->plan, context);
                break;
            }
            case PORTAL_CMD_UTILITY:
            {
                ql->run_cmd_utility(portal->plan, txn_id, context);
                break;
            }
            default:
            {
                throw InternalError("Unexpected field type");
            }
        }
    }

    // 清空资源
    void drop(){}


    std::unique_ptr<AbstractExecutor> convert_plan_executor(std::shared_ptr<Plan> plan, Context *context)
    {
        if(auto x = std::dynamic_pointer_cast<ProjectionPlan>(plan)){
            return std::make_unique<ProjectionExecutor>(convert_plan_executor(x->subplan_, context),
                                                        x->sel_cols_);
        } else if(auto x = std::dynamic_pointer_cast<ScanPlan>(plan)) {
            if(x->tag == T_SeqScan) {
                return std::make_unique<SeqScanExecutor>(sm_manager_, x->tab_name_, x->conds_, context);
            }
            else {
                return std::make_unique<IndexScanExecutor>(sm_manager_, x->tab_name_, x->conds_, x->index_col_names_, context);
            }
        } else if(auto x = std::dynamic_pointer_cast<JoinPlan>(plan)) {
            std::unique_ptr<AbstractExecutor> left = convert_plan_executor(x->left_, context);
            std::unique_ptr<AbstractExecutor> right = convert_plan_executor(x->right_, context);
            std::unique_ptr<AbstractExecutor> join = std::make_unique<NestedLoopJoinExecutor>(
                                std::move(left),
                                std::move(right), std::move(x->conds_));
            return join;
        } else if(auto x = std::dynamic_pointer_cast<SortPlan>(plan)) {
            return std::make_unique<SortExecutor>(convert_plan_executor(x->subplan_, context),
                                            std::move(x->sort_cols_), std::move(x->is_desc_), x->limit_);
        } else if(auto x = std::dynamic_pointer_cast<AggregationPlan>(plan)) {
            return std::make_unique<AggregationExecutor>(
                convert_plan_executor(x->subplan_, context),
                x->agg_types_, x->agg_targets_,
                x->group_by_cols_, x->having_conds_,
                x->output_meta_);
        } else if(auto x = std::dynamic_pointer_cast<FilterPlan>(plan)) {
            // FilterPlan: 直接返回子节点的 executor（条件已在 ScanPlan 中处理）
            return convert_plan_executor(x->subplan_, context);
        }
        return nullptr;
    }

};