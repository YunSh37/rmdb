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

#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "parser/parser.h"
#include "system/sm.h"
#include "common/common.h"

class Query{
    public:
    std::shared_ptr<ast::TreeNode> parse;
    // TODO jointree
    // where条件
    std::vector<Condition> conds;
    // 投影列（输出列，包含别名信息）
    std::vector<TabCol> cols;
    // 表名
    std::vector<std::string> tables;
    // update 的set 值
    std::vector<SetClause> set_clauses;
    //insert 的values值
    std::vector<Value> values;
    // 是否为 EXPLAIN 查询
    bool is_explain = false;

    // 别名映射：alias → real_table_name（EXPLAIN 显示用）
    std::map<std::string, std::string> aliases;

    // ========== 聚合与分组 ==========
    /** 是否包含聚合函数 */
    bool has_aggregate = false;
    /** 每个 SELECT 列的聚合类型（AGG_NONE=普通列，AGG_MAX/MIN/COUNT/SUM/COUNT_STAR） */
    std::vector<int> agg_types;
    /** 聚合目标列（与 cols 一一对应，AGG_NONE 和 COUNT_STAR 时为空 TabCol） */
    std::vector<TabCol> agg_targets;

    // ========== GROUP BY ==========
    std::vector<TabCol> group_by_cols;

    // ========== HAVING ==========
    std::vector<Condition> having_conds;

    // ========== ORDER BY 多列 ==========
    std::vector<TabCol> order_by_cols;
    std::vector<bool> order_by_desc;

    // ========== LIMIT ==========
    int limit_count = -1;  // -1 表示无 LIMIT

    // ========== SEMI JOIN ==========
    bool is_semi_join = false;         // 是否为 SEMI JOIN 查询
    std::string semi_left_table;       // SEMI JOIN 左表名（用于验证右表列不被选择）

    Query(){}

};

class Analyze
{
private:
    SmManager *sm_manager_;
public:
    Analyze(SmManager *sm_manager) : sm_manager_(sm_manager){}
    ~Analyze(){}

    std::shared_ptr<Query> do_analyze(std::shared_ptr<ast::TreeNode> root);

private:
    TabCol check_column(const std::vector<ColMeta> &all_cols, TabCol target,
                        const std::string& preferred_tab = "");
    void get_all_cols(const std::vector<std::string> &tab_names, std::vector<ColMeta> &all_cols);
    void get_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds, std::vector<Condition> &conds);
    void check_clause(const std::vector<std::string> &tab_names, std::vector<Condition> &conds,
                      const std::string& preferred_tab = "");
    Value convert_sv_value(const std::shared_ptr<ast::Value> &sv_val);
    CompOp convert_sv_comp_op(ast::SvCompOp op);
};

