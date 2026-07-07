/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "planner.h"

#include <algorithm>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>

#include "execution/executor_delete.h"
#include "execution/executor_index_scan.h"
#include "execution/executor_insert.h"
#include "execution/executor_nestedloop_join.h"
#include "execution/executor_projection.h"
#include "execution/executor_seq_scan.h"
#include "execution/executor_update.h"
#include "index/ix.h"
#include "record_printer.h"

/**
 * @brief 索引匹配规则：查找可用索引
 *
 * 策略（优先级从高到低）：
 *   1. 等值条件精确匹配索引 → 点查（最佳）
 *   2. 范围条件匹配索引（含复合索引） → 范围扫描（间隙锁需要索引支持）
 *   3. 无匹配索引 → SeqScan
 *
 * 策略2的安全约束：
 *   - 必须至少有一个范围条件（GT/GE/LT/LE）
 *   - 索引第一列必须在过滤条件中（B+树最左前缀匹配）
 *   - 所有过滤条件涉及的列必须属于同一个索引（防止引入额外扫描）
 *   - 优先选择列数较少的索引（更窄的扫描范围）
 */
bool Planner::get_index_cols(std::string tab_name, std::vector<Condition> curr_conds, std::vector<std::string>& index_col_names,
                              bool enable_range) {
    index_col_names.clear();

    // ===== 策略1：等值条件精确匹配（保持原有行为）=====
    for (auto& cond : curr_conds) {
        if (cond.is_rhs_val && cond.op == OP_EQ && cond.lhs_col.tab_name == tab_name) {
            index_col_names.push_back(cond.lhs_col.col_name);
        }
    }
    TabMeta& tab = sm_manager_->db_.get_table(tab_name);
    if (!index_col_names.empty() && tab.is_index(index_col_names)) {
        return true;  // 精确等值匹配成功
    }

    // ===== 策略2：范围条件匹配索引（仅 enable_range=true 时启用）=====
    // 该策略用于 SELECT 的间隙锁（题目十）：IndexScan 注册谓词范围锁
    // DELETE/UPDATE 不需要此策略，使用 SeqScan 即可
    // IMPORTANT: 仅匹配单列索引，不匹配复合索引。
    //   复合索引在部分匹配时涉及复杂的键构造，且 IndexScan 对此场景的
    //   范围扫描逻辑未充分验证（题目十一 crash_recovery_index_test 回归）。
    if (!enable_range) {
        index_col_names.clear();
        return false;
    }

    // 收集所有过滤条件涉及的列（去重）和是否有范围条件
    std::set<std::string> filter_col_set;
    bool has_range_cond = false;
    for (auto& cond : curr_conds) {
        if (cond.is_rhs_val && cond.lhs_col.tab_name == tab_name) {
            filter_col_set.insert(cond.lhs_col.col_name);
            if (cond.op == OP_GT || cond.op == OP_GE || cond.op == OP_LT || cond.op == OP_LE) {
                has_range_cond = true;
            }
        }
    }

    // 无过滤条件或无范围条件 → 回退 SeqScan
    if (filter_col_set.empty() || !has_range_cond) {
        index_col_names.clear();
        return false;
    }

    // 寻找最佳匹配的单列索引：
    //   仅匹配单列索引（col_num == 1 且列名在过滤条件中）
    //   避免复合索引部分匹配导致的 题目十一 回归
    IndexMeta* best_index = nullptr;
    for (auto& index : tab.indexes) {
        if (index.col_num != 1) continue;  // 仅匹配单列索引
        if (index.col_num == 0) continue;

        // 索引列必须在过滤条件中
        if (filter_col_set.find(index.cols[0].name) == filter_col_set.end()) {
            continue;
        }

        // 优先选择第一个匹配的（无需列数比较，都是单列）
        if (best_index == nullptr) {
            best_index = &index;
            break;
        }
    }

    if (best_index != nullptr) {
        // 返回索引的列名列表（IndexScanExecutor 需要索引元数据）
        index_col_names.push_back(best_index->cols[0].name);
        return true;
    }

    index_col_names.clear();
    return false;
}

/** 判断条件是否为连接条件（两侧都是列且来自不同表） */
static bool is_join_cond(const Condition& cond) {
    return !cond.is_rhs_val && cond.lhs_col.tab_name != cond.rhs_col.tab_name;
}

/** 判断条件是否为单表过滤条件 */
static bool is_filter_cond(const Condition& cond) {
    return cond.is_rhs_val;
}

/** 获取表的估计大小（记录数），用于连接顺序优化 */
static int get_table_size(SmManager* sm_manager, const std::string& tab_name) {
    auto it = sm_manager->fhs_.find(tab_name);
    if (it != sm_manager->fhs_.end()) {
        auto fh = it->second->get_file_hdr();
        int data_pages = fh.num_pages - 1;  // 减去文件头页
        if (data_pages < 0) data_pages = 0;
        return data_pages * fh.num_records_per_page;
    }
    return 0;  // 表未打开时返回0
}

/**
 * @brief 从条件列表中提取指定表的过滤条件（不修改原列表）
 */
static std::vector<Condition> extract_table_conds(const std::vector<Condition>& conds, const std::string& tab_name) {
    std::vector<Condition> result;
    for (auto& cond : conds) {
        if (cond.is_rhs_val && cond.lhs_col.tab_name == tab_name) {
            result.push_back(cond);
        }
    }
    return result;
}

/**
 * @brief 表算子条件谓词生成（保留原函数签名以供兼容）
 */
std::vector<Condition> pop_conds(std::vector<Condition> &conds, std::string tab_names) {
    std::vector<Condition> solved_conds;
    auto it = conds.begin();
    while (it != conds.end()) {
        if ((tab_names.compare(it->lhs_col.tab_name) == 0 && it->is_rhs_val) || (it->lhs_col.tab_name.compare(it->rhs_col.tab_name) == 0)) {
            solved_conds.emplace_back(std::move(*it));
            it = conds.erase(it);
        } else {
            it++;
        }
    }
    return solved_conds;
}

int push_conds(Condition *cond, std::shared_ptr<Plan> plan)
{
    if(auto x = std::dynamic_pointer_cast<ScanPlan>(plan))
    {
        if(x->tab_name_.compare(cond->lhs_col.tab_name) == 0) {
            return 1;
        } else if(x->tab_name_.compare(cond->rhs_col.tab_name) == 0){
            return 2;
        } else {
            return 0;
        }
    }
    else if(auto x = std::dynamic_pointer_cast<JoinPlan>(plan))
    {
        int left_res = push_conds(cond, x->left_);
        if(left_res == 3){
            return 3;
        }
        int right_res = push_conds(cond, x->right_);
        if(right_res == 3){
            return 3;
        }
        if(left_res == 0 || right_res == 0) {
            return left_res + right_res;
        }
        if(left_res == 2) {
            std::map<CompOp, CompOp> swap_op = {
                {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
            };
            std::swap(cond->lhs_col, cond->rhs_col);
            cond->op = swap_op.at(cond->op);
        }
        x->conds_.emplace_back(std::move(*cond));
        return 3;
    }
    return false;
}

std::shared_ptr<Plan> pop_scan(int *scantbl, std::string table, std::vector<std::string> &joined_tables,
                std::vector<std::shared_ptr<Plan>> plans)
{
    for (size_t i = 0; i < plans.size(); i++) {
        auto x = std::dynamic_pointer_cast<ScanPlan>(plans[i]);
        if(x->tab_name_.compare(table) == 0)
        {
            scantbl[i] = 1;
            joined_tables.emplace_back(x->tab_name_);
            return plans[i];
        }
    }
    return nullptr;
}


std::shared_ptr<Query> Planner::logical_optimization(std::shared_ptr<Query> query, Context *context)
{
    // 逻辑优化：在此阶段完成选择下推和投影下推
    // 实际的计划结构优化在 make_one_rel 中完成
    return query;
}

std::shared_ptr<Plan> Planner::physical_optimization(std::shared_ptr<Query> query, Context *context)
{
    std::shared_ptr<Plan> plan = make_one_rel(query);

    // 如果有聚合函数，在 scan/filter 之上插入 AggregationPlan
    if (query->has_aggregate) {
        plan = generate_aggregation_plan(query, std::move(plan));
    }

    // 处理 HAVING：在 AggregationPlan 之上添加 FilterPlan
    if (!query->having_conds.empty()) {
        plan = std::make_shared<FilterPlan>(std::move(plan), query->having_conds);
    }

    // 处理orderby
    plan = generate_sort_plan(query, std::move(plan));

    return plan;
}

/**
 * @brief 收集指定表在投影下推中需要保留的列
 * 仅包含 SELECT 列 + JOIN 连接键列（不包含过滤条件列，因为 Filter 在 Project 之下）
 * @param tab_name 表名
 * @param sel_cols SELECT 列表
 * @param all_conds 所有条件（包含过滤和连接条件）
 * @return 需要保留的列列表
 */
static std::vector<TabCol> get_needed_cols_for_table(
    const std::string& tab_name,
    const std::vector<TabCol>& sel_cols,
    const std::vector<Condition>& all_conds)
{
    std::vector<TabCol> needed;
    std::set<std::string> added;  // 去重用

    // 1. SELECT 列表中属于该表的列
    for (auto& col : sel_cols) {
        if (col.tab_name == tab_name && added.find(col.col_name) == added.end()) {
            needed.push_back(col);
            added.insert(col.col_name);
        }
    }

    // 2. 仅连接条件（非过滤条件）涉及的列
    //    过滤条件列不包含在 Project 中，因为 Filter 在 Project 之下已处理
    for (auto& cond : all_conds) {
        if (cond.is_rhs_val) continue;  // 跳过过滤条件
        // 左操作数（连接键）
        if (cond.lhs_col.tab_name == tab_name &&
            added.find(cond.lhs_col.col_name) == added.end()) {
            needed.push_back(cond.lhs_col);
            added.insert(cond.lhs_col.col_name);
        }
        // 右操作数（连接键）
        if (cond.rhs_col.tab_name == tab_name &&
            added.find(cond.rhs_col.col_name) == added.end()) {
            needed.push_back(cond.rhs_col);
            added.insert(cond.rhs_col.col_name);
        }
    }

    return needed;
}


std::shared_ptr<Plan> Planner::make_one_rel(std::shared_ptr<Query> query)
{
    auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse);
    std::vector<std::string> tables = query->tables;
    auto& all_conds = query->conds;
    auto& sel_cols = query->cols;

    // 判断是否为 SELECT *（用 AST 判断，不能用 sel_cols，因为分析器已把 * 展开为全部列名）
    bool is_select_star = false;
    if (auto select_stmt = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse)) {
        is_select_star = select_stmt->sel_cols.empty();
    }

    // ================================================================
    // 第1步：为每个表创建 Scan + Filter + Project 子树
    // ================================================================
    std::vector<std::shared_ptr<Plan>> table_plans(tables.size());
    for (size_t i = 0; i < tables.size(); i++) {
        // 提取该表的过滤条件（不修改 all_conds，用于后续投影下推判断）
        auto filter_conds = extract_table_conds(all_conds, tables[i]);

        // 用 pop_conds 从 query->conds 中移走（保持兼容性）
        auto curr_conds = pop_conds(query->conds, tables[i]);

        // 检查索引
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(tables[i], curr_conds, index_col_names, true);

        std::shared_ptr<Plan> scan;
        if (index_exist == false) {
            index_col_names.clear();
            scan = std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, tables[i], curr_conds, index_col_names);
        } else {
            scan = std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, tables[i], curr_conds, index_col_names);
        }

        // 选择下推：先包裹 FilterPlan（在 Scan 之上、Project 之下）
        if (!curr_conds.empty()) {
            scan = std::make_shared<FilterPlan>(scan, curr_conds);
        }

        // 投影下推：再包裹 ProjectionPlan（在 Filter 之上，仅多表连接且非 SELECT * 时）
        if (!is_select_star && tables.size() > 1) {
            // 仅保留 SELECT + JOIN 列（不包含过滤条件列，因为 Filter 已处理）
            auto needed_cols = get_needed_cols_for_table(tables[i], sel_cols, all_conds);
            if (!needed_cols.empty()) {
                scan = std::make_shared<ProjectionPlan>(T_Projection, scan, needed_cols);
            }
        }

        table_plans[i] = scan;
    }

    // ================================================================
    // 第2步：单表情况直接返回
    // ================================================================
    if (tables.size() == 1) {
        return table_plans[0];
    }

    // ================================================================
    // 第2.5步：SEMI JOIN 特殊处理 — 左表必须为驱动表
    // ================================================================
    if (query->is_semi_join && tables.size() == 2) {
        // 确定左表和右表在 tables 中的索引
        int left_idx = 0;
        int right_idx = 1;
        if (tables[0] != query->semi_left_table) {
            left_idx = 1;
            right_idx = 0;
        }

        // 收集连接条件
        std::vector<Condition> semi_join_conds;
        for (auto& cond : query->conds) {
            if (is_join_cond(cond)) {
                semi_join_conds.push_back(cond);
            }
        }

        // 构建 JoinPlan：左表在左，右表在右
        auto join_plan = std::make_shared<JoinPlan>(
            T_NestLoop, table_plans[left_idx], table_plans[right_idx], semi_join_conds);
        join_plan->type = SEMI_JOIN;
        return join_plan;
    }

    // ================================================================
    // 第3步：连接顺序优化 — 按表大小排序，构建左深连接树
    // ================================================================
    // 收集剩余的连接条件（query->conds 中只剩下跨表条件）
    std::vector<Condition> join_conds;
    for (auto& cond : query->conds) {
        if (is_join_cond(cond)) {
            join_conds.push_back(cond);
        }
    }

    // 按估计表大小排序（小表优先）
    std::vector<int> table_order;
    for (size_t i = 0; i < tables.size(); i++) {
        table_order.push_back(i);
    }
    std::sort(table_order.begin(), table_order.end(), [&](int a, int b) {
        return get_table_size(sm_manager_, tables[a]) < get_table_size(sm_manager_, tables[b]);
    });

    // 从最小表开始构建左深连接树
    std::shared_ptr<Plan> join_tree = table_plans[table_order[0]];
    std::set<std::string> joined_tables = {tables[table_order[0]]};

    for (size_t round = 1; round < tables.size(); round++) {
        // 找到下一个要连接的表：优先选择与已连接表有连接条件的表
        int next_idx = -1;
        std::vector<Condition> next_join_conds;

        for (size_t j = 0; j < tables.size(); j++) {
            int ti = table_order[j];
            if (joined_tables.count(tables[ti])) continue;

            // 检查是否有连接条件涉及此表和已连接的表
            for (auto& jc : join_conds) {
                bool lhs_in_joined = joined_tables.count(jc.lhs_col.tab_name) > 0;
                bool rhs_in_joined = joined_tables.count(jc.rhs_col.tab_name) > 0;
                bool lhs_is_ti = jc.lhs_col.tab_name == tables[ti];
                bool rhs_is_ti = jc.rhs_col.tab_name == tables[ti];

                if ((lhs_in_joined && rhs_is_ti) || (rhs_in_joined && lhs_is_ti)) {
                    next_idx = ti;
                    // 保持原始条件顺序（不交换）
                    next_join_conds.push_back(jc);
                    break;
                }
            }
            if (next_idx >= 0) break;
        }

        // 如果没有找到连接条件，选择下一个最小的未连接表
        if (next_idx < 0) {
            for (size_t j = 0; j < tables.size(); j++) {
                int ti = table_order[j];
                if (!joined_tables.count(tables[ti])) {
                    next_idx = ti;
                    break;
                }
            }
        }

        joined_tables.insert(tables[next_idx]);

        // 构建 JoinPlan：已连接树在左，新表在右
        std::shared_ptr<Plan> right = table_plans[next_idx];
        join_tree = std::make_shared<JoinPlan>(T_NestLoop, join_tree, right, next_join_conds);
    }

    return join_tree;
}


/**
 * @brief 生成聚合执行计划
 * 在子计划（scan + filter）之上插入 AggregationPlan
 */
std::shared_ptr<Plan> Planner::generate_aggregation_plan(std::shared_ptr<Query> query, std::shared_ptr<Plan> plan)
{
    std::vector<ColMeta> all_cols;
    for (auto& tab_name : query->tables) {
        const auto& tab_cols = sm_manager_->db_.get_table(tab_name).cols;
        all_cols.insert(all_cols.end(), tab_cols.begin(), tab_cols.end());
    }

    // 构建输出列和元数据
    std::vector<TabCol> output_cols;
    std::vector<ColMeta> output_meta;
    size_t curr_offset = 0;

    // 先输出 GROUP BY 列
    for (auto& gb : query->group_by_cols) {
        output_cols.push_back(gb);
        // 查找列元数据
        for (auto& col : all_cols) {
            if (col.tab_name == gb.tab_name && col.name == gb.col_name) {
                ColMeta meta = col;
                meta.offset = curr_offset;
                curr_offset += meta.len;
                output_meta.push_back(meta);
                break;
            }
        }
    }

    // 再输出聚合结果列
    for (size_t i = 0; i < query->cols.size(); i++) {
        if (query->agg_types[i] != ast::AGG_NONE) {
            TabCol out_col = query->cols[i];
            output_cols.push_back(out_col);

            // 确定聚合结果类型
            ColType agg_result_type = TYPE_INT;  // COUNT 默认为 INT
            int agg_len = sizeof(int);

            int agg_type = query->agg_types[i];
            if (agg_type == ast::AGG_COUNT || agg_type == ast::AGG_COUNT_STAR) {
                agg_result_type = TYPE_INT;
                agg_len = sizeof(int);
            } else {
                // MAX/MIN/SUM: 结果类型与目标列相同
                ColType target_type = TYPE_INT;
                for (auto& col : all_cols) {
                    if (col.tab_name == query->agg_targets[i].tab_name &&
                        col.name == query->agg_targets[i].col_name) {
                        target_type = col.type;
                        agg_len = col.len;
                        break;
                    }
                }
                // SUM(INT) → BIGINT 防止溢出
                if (agg_type == ast::AGG_SUM && target_type == TYPE_INT) {
                    agg_result_type = TYPE_BIGINT;
                    agg_len = sizeof(int64_t);
                } else {
                    agg_result_type = target_type;
                }
            }

            ColMeta meta;
            meta.tab_name = out_col.tab_name;
            meta.name = out_col.col_name;
            meta.type = agg_result_type;
            meta.len = agg_len;
            meta.offset = curr_offset;
            curr_offset += agg_len;
            output_meta.push_back(meta);
        }
    }

    return std::make_shared<AggregationPlan>(
        std::move(plan),
        query->agg_types,
        query->agg_targets,
        query->group_by_cols,
        query->having_conds,
        std::move(output_cols),
        std::move(output_meta)
    );
}

std::shared_ptr<Plan> Planner::generate_sort_plan(std::shared_ptr<Query> query, std::shared_ptr<Plan> plan)
{
    auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse);
    if(!x->has_order || x->order_cols.empty()) {
        return plan;
    }
    std::vector<std::string> tables = query->tables;
    std::vector<ColMeta> all_cols;
    for (auto &sel_tab_name : tables) {
        // 这里db_不能写成get_db(), 注意要传指针
        const auto &sel_tab_cols = sm_manager_->db_.get_table(sel_tab_name).cols;
        all_cols.insert(all_cols.end(), sel_tab_cols.begin(), sel_tab_cols.end());
    }
    // 解析 ORDER BY 多列
    std::vector<TabCol> sort_cols;
    std::vector<bool> sort_desc;
    for (size_t i = 0; i < x->order_cols.size(); i++) {
        auto& oc = x->order_cols[i];
        for (auto &col : all_cols) {
            if(col.name.compare(oc->col_name) == 0) {
                sort_cols.push_back({.tab_name = col.tab_name, .col_name = col.name});
                sort_desc.push_back(i < x->order_dirs.size() && x->order_dirs[i] == ast::OrderBy_DESC);
                break;
            }
        }
    }
    return std::make_shared<SortPlan>(T_Sort, std::move(plan), sort_cols, sort_desc,
                                      x->limit_count);
}


/**
 * @brief select plan 生成
 *
 * @param sel_cols select plan 选取的列
 * @param tab_names select plan 目标的表
 * @param conds select plan 选取条件
 */
std::shared_ptr<Plan> Planner::generate_select_plan(std::shared_ptr<Query> query, Context *context) {
    //逻辑优化
    query = logical_optimization(std::move(query), context);

    //物理优化
    auto sel_cols = query->cols;
    std::shared_ptr<Plan> plannerRoot = physical_optimization(query, context);
    auto proj_plan = std::make_shared<ProjectionPlan>(T_Projection, std::move(plannerRoot),
                                                        std::move(sel_cols));

    // 检测是否为 SELECT * 查询（用于 EXPLAIN 显示 [*]）
    if (auto select_stmt = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse)) {
        if (select_stmt->sel_cols.empty()) {
            proj_plan->is_star_ = true;
        }
    }

    return proj_plan;
}

// 生成DDL语句和DML语句的查询执行计划
std::shared_ptr<Plan> Planner::do_planner(std::shared_ptr<Query> query, Context *context)
{
    std::shared_ptr<Plan> plannerRoot;
    if (auto x = std::dynamic_pointer_cast<ast::CreateTable>(query->parse)) {
        // create table;
        std::vector<ColDef> col_defs;
        for (auto &field : x->fields) {
            if (auto sv_col_def = std::dynamic_pointer_cast<ast::ColDef>(field)) {
                ColDef col_def = {.name = sv_col_def->col_name,
                                  .type = interp_sv_type(sv_col_def->type_len->type),
                                  .len = sv_col_def->type_len->len};
                col_defs.push_back(col_def);
            } else {
                throw InternalError("Unexpected field type");
            }
        }
        plannerRoot = std::make_shared<DDLPlan>(T_CreateTable, x->tab_name, std::vector<std::string>(), col_defs);
    } else if (auto x = std::dynamic_pointer_cast<ast::DropTable>(query->parse)) {
        // drop table;
        plannerRoot = std::make_shared<DDLPlan>(T_DropTable, x->tab_name, std::vector<std::string>(), std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::CreateIndex>(query->parse)) {
        // create index;
        plannerRoot = std::make_shared<DDLPlan>(T_CreateIndex, x->tab_name, x->col_names, std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::DropIndex>(query->parse)) {
        // drop index
        plannerRoot = std::make_shared<DDLPlan>(T_DropIndex, x->tab_name, x->col_names, std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::InsertStmt>(query->parse)) {
        // insert;
        plannerRoot = std::make_shared<DMLPlan>(T_Insert, std::shared_ptr<Plan>(),  x->tab_name,  
                                                    query->values, std::vector<Condition>(), std::vector<SetClause>());
    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(query->parse)) {
        // delete;
        // 生成表扫描方式
        std::shared_ptr<Plan> table_scan_executors;
        // 只有一张表，不需要进行物理优化了
        // int index_no = get_indexNo(x->tab_name, query->conds);
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(x->tab_name, query->conds, index_col_names);
        
        if (index_exist == false) {  // 该表没有索引
            index_col_names.clear();
            table_scan_executors = 
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        } else {  // 存在索引
            table_scan_executors =
                std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        }

        plannerRoot = std::make_shared<DMLPlan>(T_Delete, table_scan_executors, x->tab_name,  
                                                std::vector<Value>(), query->conds, std::vector<SetClause>());
    } else if (auto x = std::dynamic_pointer_cast<ast::UpdateStmt>(query->parse)) {
        // update;
        // 生成表扫描方式
        std::shared_ptr<Plan> table_scan_executors;
        // 只有一张表，不需要进行物理优化了
        // int index_no = get_indexNo(x->tab_name, query->conds);
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(x->tab_name, query->conds, index_col_names);

        if (index_exist == false) {  // 该表没有索引
        index_col_names.clear();
            table_scan_executors = 
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        } else {  // 存在索引
            table_scan_executors =
                std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        }
        plannerRoot = std::make_shared<DMLPlan>(T_Update, table_scan_executors, x->tab_name,
                                                     std::vector<Value>(), query->conds, 
                                                     query->set_clauses);
    } else if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse)) {

        std::shared_ptr<plannerInfo> root = std::make_shared<plannerInfo>(x);
        // 生成select语句的查询执行计划
        std::shared_ptr<Plan> projection = generate_select_plan(std::move(query), context);
        plannerRoot = std::make_shared<DMLPlan>(T_select, projection, std::string(), std::vector<Value>(),
                                                    std::vector<Condition>(), std::vector<SetClause>());
    } else {
        throw InternalError("Unexpected AST root");
    }
    return plannerRoot;
}