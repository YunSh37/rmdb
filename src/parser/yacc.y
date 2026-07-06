%{
#include "ast.h"
#include "yacc.tab.h"
#include <iostream>
#include <memory>

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc);

void yyerror(YYLTYPE *locp, const char* s) {
    std::cerr << "Parser Error at line " << locp->first_line << " column " << locp->first_column << ": " << s << std::endl;
}

using namespace ast;
%}

// request a pure (reentrant) parser
%define api.pure full
// enable location in error handler
%locations
// enable verbose syntax error message
%define parse.error verbose

// keywords
%token SHOW TABLES CREATE TABLE DROP DESC INSERT INTO VALUES DELETE FROM ASC ORDER BY
WHERE UPDATE SET SELECT INT CHAR FLOAT BIGINT DATETIME INDEX AND JOIN ON EXIT HELP TXN_BEGIN TXN_COMMIT TXN_ABORT TXN_ROLLBACK ORDER_BY ENABLE_NESTLOOP ENABLE_SORTMERGE EXPLAIN MAX MIN COUNT SUM AS GROUP HAVING LIMIT SEMI STATIC_CHECKPOINT
// non-keywords
%token LEQ NEQ GEQ T_EOF

// type-specific tokens
%token <sv_str> IDENTIFIER VALUE_STRING
%token <sv_int> VALUE_INT
%token <sv_bigint> VALUE_BIGINT
%token <sv_float> VALUE_FLOAT
%token <sv_bool> VALUE_BOOL

// specify types for non-terminal symbol
%type <sv_node> stmt dbStmt ddl dml txnStmt setStmt
%type <sv_field> field
%type <sv_fields> fieldList
%type <sv_type_len> type
%type <sv_comp_op> op
%type <sv_expr> expr
%type <sv_val> value
%type <sv_vals> valueList
%type <sv_str> tbName colName
%type <sv_strs> colNameList
%type <sv_col> col
%type <sv_cols> colList
%type <sv_sel_cols> selector
%type <sv_set_clause> setClause
%type <sv_set_clauses> setClauses
%type <sv_cond> condition
%type <sv_conds> whereClause optWhereClause optOnClause
%type <sv_orderby>  order_clause opt_order_clause
%type <sv_orderby_dir> opt_asc_desc
%type <sv_sel_col> aggFunc
%type <sv_sel_col> selCol
%type <sv_sel_cols> selColList
%type <sv_str> optAlias
%type <sv_cols> optGroupBy
%type <sv_conds> optHaving havingClause
%type <sv_cond> havingCondition
%type <sv_int> optLimit
%type <sv_setKnobType> set_knob_type
%type <sv_from_clause> fromList tableRef

%%
start:
        stmt ';'
    {
        parse_tree = $1;
        YYACCEPT;
    }
    |   HELP
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
    |   EXIT
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
    |   T_EOF
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
    ;

stmt:
        dbStmt
    |   ddl
    |   dml
    |   txnStmt
    |   setStmt
    ;

txnStmt:
        TXN_BEGIN
    {
        $$ = std::make_shared<TxnBegin>();
    }
    |   TXN_COMMIT
    {
        $$ = std::make_shared<TxnCommit>();
    }
    |   TXN_ABORT
    {
        $$ = std::make_shared<TxnAbort>();
    }
    | TXN_ROLLBACK
    {
        $$ = std::make_shared<TxnRollback>();
    }
    ;

dbStmt:
        SHOW TABLES
    {
        $$ = std::make_shared<ShowTables>();
    }
    |   SHOW INDEX FROM tbName
    {
        $$ = std::make_shared<ShowIndexes>($4);
    }
    ;

setStmt:
        SET set_knob_type '=' VALUE_BOOL
    {
        $$ = std::make_shared<SetStmt>($2, $4);
    }
    ;

ddl:
        CREATE TABLE tbName '(' fieldList ')'
    {
        $$ = std::make_shared<CreateTable>($3, $5);
    }
    |   DROP TABLE tbName
    {
        $$ = std::make_shared<DropTable>($3);
    }
    |   DESC tbName
    {
        $$ = std::make_shared<DescTable>($2);
    }
    |   CREATE INDEX tbName '(' colNameList ')'
    {
        $$ = std::make_shared<CreateIndex>($3, $5);
    }
    |   DROP INDEX tbName '(' colNameList ')'
    {
        $$ = std::make_shared<DropIndex>($3, $5);
    }
    |   CREATE STATIC_CHECKPOINT
    {
        $$ = std::make_shared<CheckpointStmt>();
    }
    ;

dml:
        INSERT INTO tbName VALUES '(' valueList ')'
    {
        $$ = std::make_shared<InsertStmt>($3, $6);
    }
    |   DELETE FROM tbName optWhereClause
    {
        $$ = std::make_shared<DeleteStmt>($3, $4);
    }
    |   UPDATE tbName SET setClauses optWhereClause
    {
        $$ = std::make_shared<UpdateStmt>($2, $4, $5);
    }
    |   SELECT selector FROM fromList optWhereClause optGroupBy optHaving opt_order_clause optLimit
    {
        auto all_conds = $5;
        for (auto& jc : $4->join_conds) {
            all_conds.push_back(jc);
        }
        auto stmt = std::make_shared<SelectStmt>($2, $4->tab_names, all_conds, $8);
        stmt->aliases = std::move($4->aliases);
        stmt->group_by = $6;
        stmt->having_conds = $7;
        stmt->limit_count = $9;
        stmt->is_semi_join = $4->is_semi_join;
        if ($4->is_semi_join && !$4->tab_names.empty()) {
            stmt->semi_left_table = $4->tab_names[0];
        }
        $$ = std::move(stmt);
    }
    |   EXPLAIN SELECT selector FROM fromList optWhereClause optGroupBy optHaving opt_order_clause optLimit
    {
        auto all_conds = $6;
        for (auto& jc : $5->join_conds) {
            all_conds.push_back(jc);
        }
        auto select_stmt = std::make_shared<SelectStmt>($3, $5->tab_names, all_conds, $9);
        select_stmt->aliases = std::move($5->aliases);
        select_stmt->group_by = $7;
        select_stmt->having_conds = $8;
        select_stmt->limit_count = $10;
        select_stmt->is_semi_join = $5->is_semi_join;
        if ($5->is_semi_join && !$5->tab_names.empty()) {
            select_stmt->semi_left_table = $5->tab_names[0];
        }
        $$ = std::make_shared<ExplainStmt>(std::move(select_stmt));
    }
    ;

fieldList:
        field
    {
        $$ = std::vector<std::shared_ptr<Field>>{$1};
    }
    |   fieldList ',' field
    {
        $$.push_back($3);
    }
    ;

colNameList:
        colName
    {
        $$ = std::vector<std::string>{$1};
    }
    | colNameList ',' colName
    {
        $$.push_back($3);
    }
    ;

field:
        colName type
    {
        $$ = std::make_shared<ColDef>($1, $2);
    }
    ;

type:
        INT
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
    |   BIGINT
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_BIGINT, sizeof(int64_t));
    }
    |   CHAR '(' VALUE_INT ')'
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_STRING, $3);
    }
    |   FLOAT
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
    |   DATETIME
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_DATETIME, sizeof(int64_t));
    }
    ;

valueList:
        value
    {
        $$ = std::vector<std::shared_ptr<Value>>{$1};
    }
    |   valueList ',' value
    {
        $$.push_back($3);
    }
    ;

value:
        VALUE_INT
    {
        $$ = std::make_shared<IntLit>($1);
    }
    |   VALUE_BIGINT
    {
        $$ = std::make_shared<BigIntLit>($1, $<sv_overflow>1);
    }
    |   VALUE_FLOAT
    {
        $$ = std::make_shared<FloatLit>($1);
    }
    |   VALUE_STRING
    {
        $$ = std::make_shared<StringLit>($1);
    }
    |   VALUE_BOOL
    {
        $$ = std::make_shared<BoolLit>($1);
    }
    ;

condition:
        col op expr
    {
        $$ = std::make_shared<BinaryExpr>($1, $2, $3);
    }
    ;

optWhereClause:
        /* epsilon */ { $$ = std::vector<std::shared_ptr<ast::BinaryExpr>>(); }
    |   WHERE whereClause
    {
        $$ = $2;
    }
    ;

whereClause:
        condition 
    {
        $$ = std::vector<std::shared_ptr<BinaryExpr>>{$1};
    }
    |   whereClause AND condition
    {
        $$.push_back($3);
    }
    ;

col:
        tbName '.' colName
    {
        $$ = std::make_shared<Col>($1, $3);
    }
    |   colName
    {
        $$ = std::make_shared<Col>("", $1);
    }
    ;

colList:
        col
    {
        $$ = std::vector<std::shared_ptr<Col>>{$1};
    }
    |   colList ',' col
    {
        $$.push_back($3);
    }
    ;

op:
        '='
    {
        $$ = SV_OP_EQ;
    }
    |   '<'
    {
        $$ = SV_OP_LT;
    }
    |   '>'
    {
        $$ = SV_OP_GT;
    }
    |   NEQ
    {
        $$ = SV_OP_NE;
    }
    |   LEQ
    {
        $$ = SV_OP_LE;
    }
    |   GEQ
    {
        $$ = SV_OP_GE;
    }
    ;

expr:
        value
    {
        $$ = std::static_pointer_cast<Expr>($1);
    }
    |   col
    {
        $$ = std::static_pointer_cast<Expr>($1);
    }
    ;

/** SELECT 列表项：聚合函数 或 普通列 */
selColList:
        selCol
    {
        $$ = std::vector<std::shared_ptr<SelectCol>>{$1};
    }
    |   selColList ',' selCol
    {
        $$.push_back($3);
    }
    ;

selCol:
        aggFunc optAlias
    {
        $$ = $1;
        if (!$2.empty()) $$->alias = $2;
    }
    |   col optAlias
    {
        auto sc = std::make_shared<SelectCol>($1);
        if (!$2.empty()) sc->alias = $2;
        $$ = sc;
    }
    ;

optAlias:
        AS IDENTIFIER { $$ = $2; }
    |   /* epsilon */ { $$ = ""; }
    ;

/** 聚合函数 */
aggFunc:
        MAX '(' col ')'
    {
        $$ = std::make_shared<SelectCol>(AGG_MAX, $3);
    }
    |   MIN '(' col ')'
    {
        $$ = std::make_shared<SelectCol>(AGG_MIN, $3);
    }
    |   SUM '(' col ')'
    {
        $$ = std::make_shared<SelectCol>(AGG_SUM, $3);
    }
    |   COUNT '(' col ')'
    {
        $$ = std::make_shared<SelectCol>(AGG_COUNT, $3);
    }
    |   COUNT '(' '*' ')'
    {
        $$ = std::make_shared<SelectCol>(AGG_COUNT_STAR, nullptr);
    }
    ;

/** GROUP BY 子句（可选） */
optGroupBy:
        GROUP BY colNameList
    {
        std::vector<std::shared_ptr<Col>> cols;
        for (auto& name : $3) {
            cols.push_back(std::make_shared<Col>("", name));
        }
        $$ = cols;
    }
    |   /* epsilon */ { $$ = std::vector<std::shared_ptr<Col>>(); }
    ;

/** HAVING 子句（可选） */
optHaving:
        HAVING havingClause { $$ = $2; }
    |   /* epsilon */ { $$ = std::vector<std::shared_ptr<ast::BinaryExpr>>(); }
    ;

havingClause:
        havingCondition
    {
        $$ = std::vector<std::shared_ptr<ast::BinaryExpr>>{$1};
    }
    |   havingClause AND havingCondition
    {
        $$.push_back($3);
    }
    ;

havingCondition:
        col op expr
    {
        $$ = std::make_shared<ast::BinaryExpr>($1, $2, $3);
    }
    |   aggFunc op value
    {
        // 将聚合函数转换为伪列：col_name = 聚合函数表达式
        std::string pseudo_col;
        switch ($1->agg_type) {
            case ast::AGG_MAX:  pseudo_col = "MAX(" + ($1->col ? $1->col->col_name : "") + ")"; break;
            case ast::AGG_MIN:  pseudo_col = "MIN(" + ($1->col ? $1->col->col_name : "") + ")"; break;
            case ast::AGG_COUNT: pseudo_col = "COUNT(" + ($1->col ? $1->col->col_name : "") + ")"; break;
            case ast::AGG_SUM:  pseudo_col = "SUM(" + ($1->col ? $1->col->col_name : "") + ")"; break;
            case ast::AGG_COUNT_STAR: pseudo_col = "COUNT(*)"; break;
            default: pseudo_col = "agg"; break;
        }
        auto col_ref = std::make_shared<ast::Col>("", pseudo_col);
        $$ = std::make_shared<ast::BinaryExpr>(col_ref, $2, std::static_pointer_cast<ast::Expr>($3));
    }
    ;

/** LIMIT 子句（可选） */
optLimit:
        LIMIT VALUE_INT { $$ = $2; }
    |   /* epsilon */ { $$ = -1; }
    ;

setClauses:
        setClause
    {
        $$ = std::vector<std::shared_ptr<SetClause>>{$1};
    }
    |   setClauses ',' setClause
    {
        $$.push_back($3);
    }
    ;

setClause:
        colName '=' value
    {
        $$ = std::make_shared<SetClause>($1, $3);
    }
    ;

selector:
        '*'
    {
        $$ = {};  // 空列表表示 SELECT *
    }
    |   selColList
    ;

/** 单个表引用：表名 [别名] */
tableRef:
        tbName
    {
        auto fc = std::make_shared<ast::FromClause>();
        fc->tab_names.push_back($1);
        $$ = fc;
    }
    |   tbName IDENTIFIER
    {
        auto fc = std::make_shared<ast::FromClause>();
        fc->tab_names.push_back($1);
        fc->aliases[$2] = $1;   // 别名 → 真实表名
        $$ = fc;
    }
    ;

/** JOIN ON 子句（可选） */
optOnClause:
        /* epsilon */ { $$ = std::vector<std::shared_ptr<ast::BinaryExpr>>(); }
    |   ON whereClause { $$ = $2; }
    ;

/** FROM 子句：表引用列表，支持逗号和 JOIN 连接 */
fromList:
        tableRef
    {
        $$ = $1;
    }
    |   fromList ',' tableRef
    {
        auto& dst = $1->tab_names;
        dst.insert(dst.end(), $3->tab_names.begin(), $3->tab_names.end());
        $1->aliases.insert($3->aliases.begin(), $3->aliases.end());
        $$ = $1;
    }
    |   fromList JOIN tableRef optOnClause
    {
        auto& dst = $1->tab_names;
        dst.insert(dst.end(), $3->tab_names.begin(), $3->tab_names.end());
        $1->aliases.insert($3->aliases.begin(), $3->aliases.end());
        for (auto& cond : $4) {
            $1->join_conds.push_back(cond);
        }
        $$ = $1;
    }
    |   fromList SEMI JOIN tableRef optOnClause
    {
        auto& dst = $1->tab_names;
        dst.insert(dst.end(), $4->tab_names.begin(), $4->tab_names.end());
        $1->aliases.insert($4->aliases.begin(), $4->aliases.end());
        for (auto& cond : $5) {
            $1->join_conds.push_back(cond);
        }
        $1->is_semi_join = true;
        $$ = $1;
    }
    ;


opt_order_clause:
    ORDER BY order_clause
    {
        $$ = $3;
    }
    |   /* epsilon */ { $$ = nullptr; }
    ;

order_clause:
      col opt_asc_desc
    {
        $$ = std::make_shared<OrderBy>(
            std::vector<std::shared_ptr<Col>>{$1},
            std::vector<OrderByDir>{$2}
        );
    }
    | order_clause ',' col opt_asc_desc
    {
        $1->cols.push_back($3);
        $1->orderby_dirs.push_back($4);
        $$ = $1;
    }
    ;

opt_asc_desc:
    ASC          { $$ = OrderBy_ASC;     }
    |  DESC      { $$ = OrderBy_DESC;    }
    |       { $$ = OrderBy_DEFAULT; }
    ;    

set_knob_type:
    ENABLE_NESTLOOP { $$ = EnableNestLoop; }
    |   ENABLE_SORTMERGE { $$ = EnableSortMerge; }
    ;

tbName: IDENTIFIER;

colName: IDENTIFIER;
%%
