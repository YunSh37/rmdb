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

#include <cstdint>
#include <map>
#include <vector>
#include <string>
#include <memory>

enum JoinType {
    INNER_JOIN, LEFT_JOIN, RIGHT_JOIN, FULL_JOIN, SEMI_JOIN
};

namespace ast {

/** 聚合函数类型 */
enum AggFuncType {
    AGG_NONE, AGG_MAX, AGG_MIN, AGG_COUNT, AGG_SUM, AGG_COUNT_STAR
};

enum SvType {
    SV_TYPE_INT, SV_TYPE_FLOAT, SV_TYPE_STRING, SV_TYPE_BOOL, SV_TYPE_BIGINT, SV_TYPE_DATETIME
};

enum SvCompOp {
    SV_OP_EQ, SV_OP_NE, SV_OP_LT, SV_OP_GT, SV_OP_LE, SV_OP_GE
};

enum OrderByDir {
    OrderBy_DEFAULT,
    OrderBy_ASC,
    OrderBy_DESC
};

enum SetKnobType {
    EnableNestLoop, EnableSortMerge
};

// Base class for tree nodes
struct TreeNode {
    virtual ~TreeNode() = default;  // enable polymorphism
};

struct Help : public TreeNode {
};

struct ShowTables : public TreeNode {
};

struct TxnBegin : public TreeNode {
};

struct TxnCommit : public TreeNode {
};

struct TxnAbort : public TreeNode {
};

struct TxnRollback : public TreeNode {
};

/** 静态检查点语句 */
struct CheckpointStmt : public TreeNode {
};

struct TypeLen : public TreeNode {
    SvType type;
    int len;

    TypeLen(SvType type_, int len_) : type(type_), len(len_) {}
};

struct Field : public TreeNode {
};

struct ColDef : public Field {
    std::string col_name;
    std::shared_ptr<TypeLen> type_len;

    ColDef(std::string col_name_, std::shared_ptr<TypeLen> type_len_) :
            col_name(std::move(col_name_)), type_len(std::move(type_len_)) {}
};

struct CreateTable : public TreeNode {
    std::string tab_name;
    std::vector<std::shared_ptr<Field>> fields;

    CreateTable(std::string tab_name_, std::vector<std::shared_ptr<Field>> fields_) :
            tab_name(std::move(tab_name_)), fields(std::move(fields_)) {}
};

struct DropTable : public TreeNode {
    std::string tab_name;

    DropTable(std::string tab_name_) : tab_name(std::move(tab_name_)) {}
};

struct DescTable : public TreeNode {
    std::string tab_name;

    DescTable(std::string tab_name_) : tab_name(std::move(tab_name_)) {}
};

struct ShowIndexes : public TreeNode {
    std::string tab_name;

    ShowIndexes(std::string tab_name_) : tab_name(std::move(tab_name_)) {}
};

struct CreateIndex : public TreeNode {
    std::string tab_name;
    std::vector<std::string> col_names;

    CreateIndex(std::string tab_name_, std::vector<std::string> col_names_) :
            tab_name(std::move(tab_name_)), col_names(std::move(col_names_)) {}
};

struct DropIndex : public TreeNode {
    std::string tab_name;
    std::vector<std::string> col_names;

    DropIndex(std::string tab_name_, std::vector<std::string> col_names_) :
            tab_name(std::move(tab_name_)), col_names(std::move(col_names_)) {}
};

struct Expr : public TreeNode {
};

struct Value : public Expr {
};

struct IntLit : public Value {
    int64_t val;

    IntLit(int64_t val_) : val(val_) {}
};

struct BigIntLit : public Value {
    int64_t val;
    bool overflow;  // true 表示词法分析时检测到溢出（超出 INT64 范围）

    BigIntLit(int64_t val_, bool overflow_ = false) : val(val_), overflow(overflow_) {}
};

struct DatetimeLit : public Value {
    int64_t val;  // packed datetime

    DatetimeLit(int64_t val_) : val(val_) {}
};

struct FloatLit : public Value {
    float val;

    FloatLit(float val_) : val(val_) {}
};

struct StringLit : public Value {
    std::string val;

    StringLit(std::string val_) : val(std::move(val_)) {}
};

struct BoolLit : public Value {
    bool val;

    BoolLit(bool val_) : val(val_) {}
};

struct Col : public Expr {
    std::string tab_name;
    std::string col_name;

    Col(std::string tab_name_, std::string col_name_) :
            tab_name(std::move(tab_name_)), col_name(std::move(col_name_)) {}
};

/** SELECT 列表项：普通列或聚合函数列 */
struct SelectCol : public TreeNode {
    enum ExprType { COLUMN, AGGREGATE };
    ExprType expr_type;
    std::shared_ptr<Col> col;           // 列引用（聚合函数的参数列也存这里）
    AggFuncType agg_type;               // 聚合类型（COLUMN 时为 AGG_NONE）
    std::string alias;                  // AS 别名（空字符串=无别名）

    SelectCol(std::shared_ptr<Col> col_) :
            expr_type(COLUMN), col(std::move(col_)), agg_type(AGG_NONE) {}
    SelectCol(AggFuncType agg_type_, std::shared_ptr<Col> agg_col_) :
            expr_type(AGGREGATE), col(std::move(agg_col_)), agg_type(agg_type_) {}
    SelectCol(AggFuncType agg_type_, std::shared_ptr<Col> agg_col_, std::string alias_) :
            expr_type(AGGREGATE), col(std::move(agg_col_)), agg_type(agg_type_), alias(std::move(alias_)) {}
};

struct SetClause : public TreeNode {
    std::string col_name;
    std::shared_ptr<Value> val;

    SetClause(std::string col_name_, std::shared_ptr<Value> val_) :
            col_name(std::move(col_name_)), val(std::move(val_)) {}
};

struct BinaryExpr : public TreeNode {
    std::shared_ptr<Col> lhs;
    SvCompOp op;
    std::shared_ptr<Expr> rhs;

    BinaryExpr(std::shared_ptr<Col> lhs_, SvCompOp op_, std::shared_ptr<Expr> rhs_) :
            lhs(std::move(lhs_)), op(op_), rhs(std::move(rhs_)) {}
};

struct OrderBy : public TreeNode
{
    std::vector<std::shared_ptr<Col>> cols;      // ORDER BY 列（支持多列）
    std::vector<OrderByDir> orderby_dirs;         // 每列排序方向
    OrderBy(std::vector<std::shared_ptr<Col>> cols_, std::vector<OrderByDir> dirs_) :
       cols(std::move(cols_)), orderby_dirs(std::move(dirs_)) {}
};

struct InsertStmt : public TreeNode {
    std::string tab_name;
    std::vector<std::shared_ptr<Value>> vals;

    InsertStmt(std::string tab_name_, std::vector<std::shared_ptr<Value>> vals_) :
            tab_name(std::move(tab_name_)), vals(std::move(vals_)) {}
};

struct DeleteStmt : public TreeNode {
    std::string tab_name;
    std::vector<std::shared_ptr<BinaryExpr>> conds;

    DeleteStmt(std::string tab_name_, std::vector<std::shared_ptr<BinaryExpr>> conds_) :
            tab_name(std::move(tab_name_)), conds(std::move(conds_)) {}
};

struct UpdateStmt : public TreeNode {
    std::string tab_name;
    std::vector<std::shared_ptr<SetClause>> set_clauses;
    std::vector<std::shared_ptr<BinaryExpr>> conds;

    UpdateStmt(std::string tab_name_,
               std::vector<std::shared_ptr<SetClause>> set_clauses_,
               std::vector<std::shared_ptr<BinaryExpr>> conds_) :
            tab_name(std::move(tab_name_)), set_clauses(std::move(set_clauses_)), conds(std::move(conds_)) {}
};

struct JoinExpr : public TreeNode {
    std::string left;
    std::string right;
    std::vector<std::shared_ptr<BinaryExpr>> conds;
    JoinType type;

    JoinExpr(std::string left_, std::string right_,
               std::vector<std::shared_ptr<BinaryExpr>> conds_, JoinType type_) :
            left(std::move(left_)), right(std::move(right_)), conds(std::move(conds_)), type(type_) {}
};

/** FROM 子句解析结果：表名列表 + 别名映射 + JOIN ON 条件 */
struct FromClause {
    std::vector<std::string> tab_names;                         // 真实表名列表
    std::map<std::string, std::string> aliases;                 // 别名 → 真实表名
    std::vector<std::shared_ptr<BinaryExpr>> join_conds;        // JOIN ON 条件
    bool is_semi_join = false;                                  // 是否为 SEMI JOIN
};

struct SelectStmt : public TreeNode {
    std::vector<std::shared_ptr<SelectCol>> sel_cols;  // SELECT 列表项
    std::vector<std::string> tabs;
    std::vector<std::shared_ptr<BinaryExpr>> conds;
    std::vector<std::shared_ptr<JoinExpr>> jointree;

    /** 别名映射：alias → real_table_name，由解析器填充，分析器用于解析列引用 */
    std::map<std::string, std::string> aliases;

    /** ORDER BY 信息 */
    bool has_order = false;
    std::vector<std::shared_ptr<Col>> order_cols;       // ORDER BY 列（支持多列）
    std::vector<OrderByDir> order_dirs;                  // 每列排序方向

    /** GROUP BY 子句 */
    std::vector<std::shared_ptr<Col>> group_by;          // GROUP BY 列

    /** HAVING 子句 */
    std::vector<std::shared_ptr<BinaryExpr>> having_conds;

    /** LIMIT 子句（-1 表示无 LIMIT） */
    int limit_count = -1;

    /** SEMI JOIN 标志 */
    bool is_semi_join = false;                           // 是否为 SEMI JOIN 查询
    std::string semi_left_table;                          // SEMI JOIN 左表名（用于右表列校验）

    SelectStmt() = default;
    SelectStmt(std::vector<std::shared_ptr<SelectCol>> sel_cols_,
               std::vector<std::string> tabs_,
               std::vector<std::shared_ptr<BinaryExpr>> conds_,
               std::shared_ptr<OrderBy> order_) :
            sel_cols(std::move(sel_cols_)), tabs(std::move(tabs_)), conds(std::move(conds_)) {
        if (order_) {
            has_order = true;
            order_cols = order_->cols;
            order_dirs = order_->orderby_dirs;
        }
    }
    SelectStmt(std::vector<std::shared_ptr<SelectCol>> sel_cols_,
               std::vector<std::string> tabs_,
               std::vector<std::shared_ptr<BinaryExpr>> conds_) :
            sel_cols(std::move(sel_cols_)), tabs(std::move(tabs_)), conds(std::move(conds_)) {}
};

// Explain 语句，包装一个 SelectStmt
struct ExplainStmt : public TreeNode {
    std::shared_ptr<SelectStmt> select_stmt;

    ExplainStmt(std::shared_ptr<SelectStmt> select_stmt_) :
            select_stmt(std::move(select_stmt_)) {}
};

// set enable_nestloop
struct SetStmt : public TreeNode {
    SetKnobType set_knob_type_;
    bool bool_val_;

    SetStmt(SetKnobType &type, bool bool_value) : 
        set_knob_type_(type), bool_val_(bool_value) { }
};

// Semantic value
struct SemValue {
    int sv_int;
    int64_t sv_bigint;
    int64_t sv_datetime;   // packed datetime value
    float sv_float;
    std::string sv_str;
    bool sv_bool;
    bool sv_overflow;      // BIGINT 溢出标志
    OrderByDir sv_orderby_dir;
    std::vector<OrderByDir> sv_orderby_dirs;
    std::vector<std::string> sv_strs;

    std::shared_ptr<TreeNode> sv_node;

    SvCompOp sv_comp_op;

    std::shared_ptr<TypeLen> sv_type_len;

    std::shared_ptr<Field> sv_field;
    std::vector<std::shared_ptr<Field>> sv_fields;

    std::shared_ptr<Expr> sv_expr;

    std::shared_ptr<Value> sv_val;
    std::vector<std::shared_ptr<Value>> sv_vals;

    std::shared_ptr<Col> sv_col;
    std::vector<std::shared_ptr<Col>> sv_cols;

    std::shared_ptr<SelectCol> sv_sel_col;
    std::vector<std::shared_ptr<SelectCol>> sv_sel_cols;

    AggFuncType sv_agg_type;

    std::shared_ptr<SetClause> sv_set_clause;
    std::vector<std::shared_ptr<SetClause>> sv_set_clauses;

    std::shared_ptr<BinaryExpr> sv_cond;
    std::vector<std::shared_ptr<BinaryExpr>> sv_conds;

    std::shared_ptr<OrderBy> sv_orderby;

    std::shared_ptr<FromClause> sv_from_clause;

    SetKnobType sv_setKnobType;
};

extern std::shared_ptr<ast::TreeNode> parse_tree;

}

#define YYSTYPE ast::SemValue
