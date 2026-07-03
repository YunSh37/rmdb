/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "analyze.h"

/**
 * @description: 分析器，进行语义分析和查询重写，需要检查不符合语义规定的部分
 * @param {shared_ptr<ast::TreeNode>} parse parser生成的结果集
 * @return {shared_ptr<Query>} Query 
 */
std::shared_ptr<Query> Analyze::do_analyze(std::shared_ptr<ast::TreeNode> parse)
{
    std::shared_ptr<Query> query = std::make_shared<Query>();
    if (auto x = std::dynamic_pointer_cast<ast::ExplainStmt>(parse))
    {
        // EXPLAIN SELECT: 解包并标记
        query->is_explain = true;
        // 递归分析内部的 SelectStmt
        auto inner_query = do_analyze(x->select_stmt);
        query->tables = std::move(inner_query->tables);
        query->cols = std::move(inner_query->cols);
        query->conds = std::move(inner_query->conds);
        query->set_clauses = std::move(inner_query->set_clauses);
        query->values = std::move(inner_query->values);
        query->aliases = std::move(inner_query->aliases);  // 传递别名映射
        query->parse = std::move(inner_query->parse);
        return query;
    }
    else if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(parse))
    {
        // 处理表名
        query->tables = std::move(x->tabs);
        // 检查表是否存在
        for (auto& tab_name : query->tables) {
            sm_manager_->db_.get_table(tab_name);
        }

        // 保存别名映射的本地引用（后续 move 前需要用它解析别名）
        auto& aliases = x->aliases;

        // 处理target list，在此阶段解析别名引用
        bool has_agg = false;
        for (auto &sv_sel_col : x->sel_cols) {
            if (sv_sel_col->expr_type == ast::SelectCol::COLUMN) {
                // 普通列引用
                std::string tab_name = sv_sel_col->col->tab_name;
                auto alias_it = aliases.find(tab_name);
                if (alias_it != aliases.end()) {
                    tab_name = alias_it->second;
                }
                TabCol sel_col = {.tab_name = tab_name,
                                  .col_name = sv_sel_col->col->col_name};
                if (!sv_sel_col->alias.empty()) {
                    sel_col.col_name = sv_sel_col->alias;
                }
                query->cols.push_back(sel_col);
                query->agg_types.push_back(ast::AGG_NONE);
                query->agg_targets.push_back(TabCol{});
            } else if (sv_sel_col->expr_type == ast::SelectCol::AGGREGATE) {
                // 聚合函数列
                has_agg = true;
                // 解析目标列
                TabCol agg_target;
                if (sv_sel_col->col != nullptr) {
                    std::string tab_name = sv_sel_col->col->tab_name;
                    auto alias_it = aliases.find(tab_name);
                    if (alias_it != aliases.end()) {
                        tab_name = alias_it->second;
                    }
                    agg_target = {.tab_name = tab_name,
                                  .col_name = sv_sel_col->col->col_name};
                }
                // 输出列名：优先使用别名，否则使用聚合函数名
                std::string out_name = sv_sel_col->alias;
                if (out_name.empty()) {
                    // 自动生成列名
                    switch (sv_sel_col->agg_type) {
                        case ast::AGG_MAX:  out_name = "MAX(" + (agg_target.col_name.empty() ? "" : agg_target.col_name) + ")"; break;
                        case ast::AGG_MIN:  out_name = "MIN(" + (agg_target.col_name.empty() ? "" : agg_target.col_name) + ")"; break;
                        case ast::AGG_COUNT: out_name = "COUNT(" + (agg_target.col_name.empty() ? "" : agg_target.col_name) + ")"; break;
                        case ast::AGG_SUM:  out_name = "SUM(" + (agg_target.col_name.empty() ? "" : agg_target.col_name) + ")"; break;
                        case ast::AGG_COUNT_STAR: out_name = "COUNT(*)"; break;
                        default: out_name = "agg"; break;
                    }
                }
                TabCol sel_col = {.tab_name = agg_target.tab_name, .col_name = out_name};
                query->cols.push_back(sel_col);
                query->agg_types.push_back(sv_sel_col->agg_type);
                query->agg_targets.push_back(agg_target);
            }
        }
        query->has_aggregate = has_agg;

        std::vector<ColMeta> all_cols;
        get_all_cols(query->tables, all_cols);
        if (query->cols.empty()) {
            // select all columns（仅在既无普通列也无聚合列时）
            for (auto &col : all_cols) {
                TabCol sel_col = {.tab_name = col.tab_name, .col_name = col.name};
                query->cols.push_back(sel_col);
                query->agg_types.push_back(ast::AGG_NONE);
                query->agg_targets.push_back(TabCol{});
            }
        } else {
            // 校验非聚合列的存在性（聚合目标列在 executor 中处理）
            for (size_t i = 0; i < query->cols.size(); i++) {
                if (query->agg_types[i] == ast::AGG_NONE) {
                    query->cols[i] = check_column(all_cols, query->cols[i]);
                } else if (query->agg_types[i] != ast::AGG_COUNT_STAR) {
                    // 校验并解析聚合目标列的表名
                    query->agg_targets[i] = check_column(all_cols, query->agg_targets[i]);
                }
            }
        }

        // 处理 GROUP BY 子句
        if (!x->group_by.empty()) {
            for (auto& gb_col : x->group_by) {
                std::string tab_name = gb_col->tab_name;
                auto alias_it = aliases.find(tab_name);
                if (alias_it != aliases.end()) {
                    tab_name = alias_it->second;
                }
                TabCol tc = {.tab_name = tab_name, .col_name = gb_col->col_name};
                tc = check_column(all_cols, tc);
                query->group_by_cols.push_back(tc);
            }
        }

        // 健壮性检查：有 GROUP BY 时，SELECT 中非聚合列必须在 GROUP BY 中
        if (!query->group_by_cols.empty()) {
            for (size_t i = 0; i < query->cols.size(); i++) {
                if (query->agg_types[i] == ast::AGG_NONE) {
                    bool found = false;
                    for (auto& gb : query->group_by_cols) {
                        if (gb.tab_name == query->cols[i].tab_name &&
                            gb.col_name == query->cols[i].col_name) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        throw RMDBError("Column '" + query->cols[i].col_name +
                            "' must appear in GROUP BY clause when used with aggregate functions");
                    }
                }
            }
        }
        // 健壮性检查：有聚合函数但没有 GROUP BY 时，SELECT 列表中不能混用普通列和聚合列
        if (has_agg && query->group_by_cols.empty()) {
            for (size_t i = 0; i < query->cols.size(); i++) {
                if (query->agg_types[i] == ast::AGG_NONE) {
                    throw RMDBError("Column '" + query->cols[i].col_name +
                        "' cannot appear in SELECT with aggregate functions without GROUP BY");
                }
            }
        }

        // 处理 HAVING 子句（条件中的列可以是分组列或聚合函数，此处暂不校验）
        if (!x->having_conds.empty()) {
            get_clause(x->having_conds, query->having_conds);
            if (!aliases.empty()) {
                for (auto& cond : query->having_conds) {
                    auto it = aliases.find(cond.lhs_col.tab_name);
                    if (it != aliases.end()) {
                        cond.lhs_col.tab_name = it->second;
                    }
                    if (!cond.is_rhs_val) {
                        auto it2 = aliases.find(cond.rhs_col.tab_name);
                        if (it2 != aliases.end()) {
                            cond.rhs_col.tab_name = it2->second;
                        }
                    }
                }
            }
        }

        // 处理 ORDER BY 子句
        if (x->has_order) {
            for (size_t i = 0; i < x->order_cols.size(); i++) {
                auto& oc = x->order_cols[i];
                std::string tab_name = oc->tab_name;
                auto alias_it = aliases.find(tab_name);
                if (alias_it != aliases.end()) {
                    tab_name = alias_it->second;
                }
                TabCol tc = {.tab_name = tab_name, .col_name = oc->col_name};
                tc = check_column(all_cols, tc);
                query->order_by_cols.push_back(tc);
                bool desc = (i < x->order_dirs.size() && x->order_dirs[i] == ast::OrderBy_DESC);
                query->order_by_desc.push_back(desc);
            }
        }

        // 处理 LIMIT 子句
        query->limit_count = x->limit_count;

        //处理where条件（含 JOIN ON 条件，已在解析器中合并）
        get_clause(x->conds, query->conds);
        // 解析条件中的别名引用（必须在 move 之前，用本地引用）
        if (!aliases.empty()) {
            for (auto& cond : query->conds) {
                auto it = aliases.find(cond.lhs_col.tab_name);
                if (it != aliases.end()) {
                    cond.lhs_col.tab_name = it->second;
                }
                if (!cond.is_rhs_val) {
                    auto it2 = aliases.find(cond.rhs_col.tab_name);
                    if (it2 != aliases.end()) {
                        cond.rhs_col.tab_name = it2->second;
                    }
                }
            }
        }
        // 健壮性检查：WHERE 条件中不能使用聚合函数（聚合函数只在 HAVING 中有效）
        // 此检查在 check_clause 中进行，因为聚合函数不在 WHERE 列的元数据中
        check_clause(query->tables, query->conds);

        // 最后保存别名映射（EXPLAIN 显示用）
        query->aliases = std::move(aliases);
    } else if (auto x = std::dynamic_pointer_cast<ast::UpdateStmt>(parse)) {
        // 检查表是否存在
        sm_manager_->db_.get_table(x->tab_name);
        // 转换 set_clauses
        for (auto& sv_clause : x->set_clauses) {
            SetClause clause;
            clause.lhs = {.tab_name = x->tab_name, .col_name = sv_clause->col_name};
            clause.rhs = convert_sv_value(sv_clause->val);
            query->set_clauses.push_back(clause);
        }
        // 处理 WHERE 条件
        get_clause(x->conds, query->conds);
        check_clause({x->tab_name}, query->conds);
    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(parse)) {
        //处理where条件
        get_clause(x->conds, query->conds);
        check_clause({x->tab_name}, query->conds);        
    } else if (auto x = std::dynamic_pointer_cast<ast::InsertStmt>(parse)) {
        // 处理insert 的values值
        for (auto &sv_val : x->vals) {
            query->values.push_back(convert_sv_value(sv_val));
        }
    } else {
        // do nothing
    }
    query->parse = std::move(parse);
    return query;
}


TabCol Analyze::check_column(const std::vector<ColMeta> &all_cols, TabCol target) {
    if (target.tab_name.empty()) {
        // Table name not specified, infer table name from column name
        std::string tab_name;
        for (auto &col : all_cols) {
            if (col.name == target.col_name) {
                if (!tab_name.empty()) {
                    throw AmbiguousColumnError(target.col_name);
                }
                tab_name = col.tab_name;
            }
        }
        if (tab_name.empty()) {
            throw ColumnNotFoundError(target.col_name);
        }
        target.tab_name = tab_name;
    } else {
        // 显式指定了表名，校验该列确实存在于该表中
        const auto &tab_cols = sm_manager_->db_.get_table(target.tab_name).cols;
        auto pos = std::find_if(tab_cols.begin(), tab_cols.end(),
            [&](const ColMeta& col) { return col.name == target.col_name; });
        if (pos == tab_cols.end()) {
            throw ColumnNotFoundError(target.tab_name + "." + target.col_name);
        }
    }
    return target;
}

void Analyze::get_all_cols(const std::vector<std::string> &tab_names, std::vector<ColMeta> &all_cols) {
    for (auto &sel_tab_name : tab_names) {
        // 这里db_不能写成get_db(), 注意要传指针
        const auto &sel_tab_cols = sm_manager_->db_.get_table(sel_tab_name).cols;
        all_cols.insert(all_cols.end(), sel_tab_cols.begin(), sel_tab_cols.end());
    }
}

void Analyze::get_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds, std::vector<Condition> &conds) {
    conds.clear();
    for (auto &expr : sv_conds) {
        Condition cond;
        cond.lhs_col = {.tab_name = expr->lhs->tab_name, .col_name = expr->lhs->col_name};
        cond.op = convert_sv_comp_op(expr->op);
        if (auto rhs_val = std::dynamic_pointer_cast<ast::Value>(expr->rhs)) {
            cond.is_rhs_val = true;
            cond.rhs_val = convert_sv_value(rhs_val);
        } else if (auto rhs_col = std::dynamic_pointer_cast<ast::Col>(expr->rhs)) {
            cond.is_rhs_val = false;
            cond.rhs_col = {.tab_name = rhs_col->tab_name, .col_name = rhs_col->col_name};
        }
        conds.push_back(cond);
    }
}

void Analyze::check_clause(const std::vector<std::string> &tab_names, std::vector<Condition> &conds) {
    // auto all_cols = get_all_cols(tab_names);
    std::vector<ColMeta> all_cols;
    get_all_cols(tab_names, all_cols);
    // Get raw values in where clause
    for (auto &cond : conds) {
        // Infer table name from column name
        cond.lhs_col = check_column(all_cols, cond.lhs_col);
        if (!cond.is_rhs_val) {
            cond.rhs_col = check_column(all_cols, cond.rhs_col);
        }
        TabMeta &lhs_tab = sm_manager_->db_.get_table(cond.lhs_col.tab_name);
        auto lhs_col = lhs_tab.get_col(cond.lhs_col.col_name);
        ColType lhs_type = lhs_col->type;
        ColType rhs_type;
        if (cond.is_rhs_val) {
            cond.rhs_val.init_raw(lhs_col->len);
            rhs_type = cond.rhs_val.type;
        } else {
            TabMeta &rhs_tab = sm_manager_->db_.get_table(cond.rhs_col.tab_name);
            auto rhs_col = rhs_tab.get_col(cond.rhs_col.col_name);
            rhs_type = rhs_col->type;
        }
        if (lhs_type != rhs_type) {
            // 允许 INT→FLOAT 隐式转换（例如 WHERE score > 90，score 为 FLOAT）
            if (lhs_type == TYPE_FLOAT && rhs_type == TYPE_INT && cond.is_rhs_val) {
                cond.rhs_val.type = TYPE_FLOAT;
                cond.rhs_val.float_val = static_cast<float>(cond.rhs_val.int_val);
            } else if (lhs_type == TYPE_INT && rhs_type == TYPE_FLOAT && cond.is_rhs_val) {
                // INT 列与 FLOAT 值比较（不常见但也允许）
                // rhs 已经是 FLOAT，不需要转换
            } else {
                throw IncompatibleTypeError(coltype2str(lhs_type), coltype2str(rhs_type));
            }
        }
    }
}


Value Analyze::convert_sv_value(const std::shared_ptr<ast::Value> &sv_val) {
    Value val;
    if (auto int_lit = std::dynamic_pointer_cast<ast::IntLit>(sv_val)) {
        val.set_int(int_lit->val);
    } else if (auto float_lit = std::dynamic_pointer_cast<ast::FloatLit>(sv_val)) {
        val.set_float(float_lit->val);
    } else if (auto str_lit = std::dynamic_pointer_cast<ast::StringLit>(sv_val)) {
        val.set_str(str_lit->val);
    } else {
        throw InternalError("Unexpected sv value type");
    }
    return val;
}

CompOp Analyze::convert_sv_comp_op(ast::SvCompOp op) {
    std::map<ast::SvCompOp, CompOp> m = {
        {ast::SV_OP_EQ, OP_EQ}, {ast::SV_OP_NE, OP_NE}, {ast::SV_OP_LT, OP_LT},
        {ast::SV_OP_GT, OP_GT}, {ast::SV_OP_LE, OP_LE}, {ast::SV_OP_GE, OP_GE},
    };
    return m.at(op);
}
