# RMDB 项目分析文档

> 最后更新：2026-07-03 | 题目二"查询执行"完成后更新

## 项目概览

RMDB 是一个教学型 C++17 关系型数据库系统，采用客户端-服务端架构。支持 SQL 解析、查询执行、事务管理、故障恢复等功能。

## 目录结构

```
rmdb/
├── CMakeLists.txt              # 顶层构建配置（C++17, Debug 模式）
├── CLAUDE.md                   # 项目全局指引
├── project_analysis.md         # 本文档
├── project_structure.txt       # 文件树
├── src/                        # 服务端源码
│   ├── CMakeLists.txt          # 源文件构建配置
│   ├── rmdb.cpp                # 入口 + 网络服务
│   ├── unit_test.cpp           # 单元测试（5 个测试套件）
│   ├── parser/                 # SQL 解析（flex + bison）
│   ├── analyze/                # 语义分析
│   ├── optimizer/              # 查询优化（计划生成）
│   ├── execution/              # 执行器（算子实现）
│   ├── system/                 # 系统管理（元数据、DDL）
│   ├── record/                 # 记录管理（表数据文件）
│   ├── index/                  # B+树索引
│   ├── storage/                # 存储引擎（磁盘 + 缓冲池）
│   ├── replacer/               # 缓存替换策略（LRU）
│   ├── transaction/            # 事务管理（MVCC、2PL）
│   ├── recovery/               # 故障恢复（WAL日志）
│   ├── common/                 # 公共类型定义
│   └── test/                   # 测试数据（CSV）
├── rmdb_client/                # 客户端
├── deps/                       # 依赖（googletest）
└── build/                      # 构建输出
```

## 模块详解

### 1. Parser 模块（`src/parser/`）

**职责**：SQL 词法分析和语法解析，生成 AST。

| 文件 | 说明 |
|------|------|
| `yacc.y` | Bison 语法文件，定义完整 SQL 语法规则 |
| `lex.l` | Flex 词法文件，定义 token 规则 |
| `ast.h/cpp` | AST 节点定义（CreateTable、SelectStmt、InsertStmt 等） |
| `parse_node.h` | 语法树节点基类 |
| `parser_defs.h` | 解析器常量定义 |

**支持的 SQL 语法**：
- DDL: `CREATE TABLE`, `DROP TABLE`, `DESC`, `CREATE INDEX`, `DROP INDEX`
- DML: `INSERT INTO`, `DELETE FROM`, `UPDATE`, `SELECT`
- 其他: `SHOW TABLES`, `HELP`, `EXIT`, `BEGIN/COMMIT/ABORT/ROLLBACK`
- 表达式: 6 种比较运算符（`=` `<>` `<` `>` `<=` `>=`），AND 连接
- 数据类型: INT, FLOAT, CHAR(n)

### 2. Analyze 模块（`src/analyze/`）

**职责**：语义分析，检查表/列存在性、类型兼容性，转换 AST 数据类型为内部类型。

| 文件 | 关键方法 | 说明 |
|------|----------|------|
| `analyze.h/cpp` | `do_analyze()` | 主入口，分发到各语句处理器 |
| | `check_column()` | 列元数据校验（推断表名、检查列存在） |
| | `check_clause()` | WHERE 条件类型检查 |
| | `convert_sv_value()` | AST 值 → 内部 Value 类型转换 |

### 3. Optimizer 模块（`src/optimizer/`）

**职责**：生成查询执行计划，进行逻辑和物理优化。

| 文件 | 关键方法 | 说明 |
|------|----------|------|
| `optimizer.h` | `plan_query()` | 优化入口，路由到 planner |
| `planner.h/cpp` | `do_planner()` | 根据 AST 类型生成 Plan |
| | `generate_select_plan()` | SELECT 计划生成（含投影优化） |
| | `make_one_rel()` | 单表/多表 JOIN 计划 |
| `plan.h` | `ScanPlan`, `JoinPlan`, `ProjectionPlan`, `SortPlan`, `DMLPlan`, `DDLPlan`, `OtherPlan` | 计划节点类型定义 |

**执行计划类型 (PlanTag)**：T_SeqScan, T_IndexScan, T_NestLoop, T_SortMerge, T_Projection, T_Sort, T_Insert, T_Update, T_Delete, T_select 等。

### 4. Execution 模块（`src/execution/`）

**职责**：执行器实现，按执行计划树递归执行。

| 文件 | 类 | 说明 |
|------|-----|------|
| `executor_abstract.h` | `AbstractExecutor` | 执行器基类 |
| `executor_seq_scan.h` | `SeqScanExecutor` | 全表扫描 + 条件过滤 |
| `executor_index_scan.h` | `IndexScanExecutor` | 索引扫描（TODO） |
| `executor_insert.h` | `InsertExecutor` | 插入记录 + 索引维护 |
| `executor_update.h` | `UpdateExecutor` | 更新记录 |
| `executor_delete.h` | `DeleteExecutor` | 删除记录 |
| `executor_projection.h` | `ProjectionExecutor` | 列投影 |
| `executor_nestedloop_join.h` | `NestedLoopJoinExecutor` | 嵌套循环连接（TODO） |
| `execution_sort.h` | `SortExecutor` | 排序（TODO） |
| `execution_manager.h/cpp` | `QlManager` | 执行管理器，路由 DDL/DML/工具命令 |
| `portal.h` | `Portal` | 计划→执行器树转换 + 执行分发 |

**Portal 执行标签 (portalTag)**：
- `PORTAL_ONE_SELECT` — SELECT 语句
- `PORTAL_DML_WITHOUT_SELECT` — INSERT/UPDATE/DELETE
- `PORTAL_MULTI_QUERY` — DDL（CREATE/DROP TABLE/INDEX）
- `PORTAL_CMD_UTILITY` — 工具命令（HELP/SHOW/DESC/BEGIN/COMMIT/ABORT）

### 5. System 模块（`src/system/`）

**职责**：元数据管理，DDL 执行。

| 文件 | 关键方法 | 说明 |
|------|----------|------|
| `sm_manager.h/cpp` | `create_db/drop_db/open_db/close_db` | 数据库生命周期管理 |
| | `create_table/drop_table` | 表管理 |
| | `show_tables/desc_table` | 元数据查询 |
| | `create_index/drop_index` | 索引管理（TODO） |
| `sm_meta.h` | `DbMeta`, `TabMeta`, `ColMeta`, `IndexMeta` | 三层元数据结构 |
| `sm_defs.h` | `ColDef` | 列定义结构体 |

**关键数据结构**：
- `DbMeta::tabs_` — `map<string, TabMeta>` 表名→表元数据
- `TabMeta::cols_` — `vector<ColMeta>` 列元数据（含类型、长度、偏移）
- `TabMeta::indexes_` — `vector<IndexMeta>` 索引元数据

### 6. Record 模块（`src/record/`）

**职责**：表数据文件的记录级 CRUD 操作。

| 文件 | 关键方法 | 说明 |
|------|----------|------|
| `rm_file_handle.h/cpp` | `get_record/insert_record/delete_record/update_record` | 记录 CRUD |
| | `fetch_page_handle/create_page_handle/release_page_handle` | 页面管理 |
| `rm_scan.h/cpp` | `RmScan` | 全表扫描迭代器 |
| `rm_manager.h` | `create_file/open_file/close_file/destroy_file` | 文件生命周期 |
| `rm_defs.h` | `RmFileHdr`, `RmPageHdr`, `RmRecord` | 记录层数据结构 |
| `bitmap.h` | `Bitmap::set/reset/is_set/first_bit/next_bit` | 位图管理空闲 slot |

**文件页面结构**：
- **页面 0**（文件头）：`RmFileHdr`（record_size, num_pages, num_records_per_page, first_free_page_no, bitmap_size）
- **页面 1+**（数据页）：`[LSN:4B][RmPageHdr:8B][Bitmap][Slots...]`
- 空闲页通过 `first_free_page_no` → `next_free_page_no` 形成链表

### 7. Storage 模块（`src/storage/`）

**职责**：磁盘文件 I/O 和缓冲池管理。

| 文件 | 关键方法 | 说明 |
|------|----------|------|
| `disk_manager.h/cpp` | `read_page/write_page` | 磁盘页读写（lseek + read/write） |
| | `create_file/open_file/close_file/destroy_file` | 文件操作 |
| | `allocate_page` | 自增页号分配 |
| `buffer_pool_manager.h/cpp` | `fetch_page/unpin_page/new_page` | 缓冲池核心操作 |
| | `flush_page/flush_all_pages/delete_page` | 页面刷盘与删除 |
| `page.h` | `Page` | 页面对象（data[PAGE_SIZE], is_dirty, pin_count） |

**BufferPool 关键流程**：
- `fetch_page`: page_table 命中 → pin++；未命中 → find_victim → 写回脏页 → 读盘
- `unpin_page`: pin_count--；归零 → replacer_->unpin
- `new_page`: find_victim → allocate_page → 初始化为空页

### 8. Replacer 模块（`src/replacer/`）

**职责**：缓冲池页面淘汰策略（LRU）。

| 文件 | 关键方法 | 说明 |
|------|----------|------|
| `lru_replacer.h/cpp` | `victim()` | 返回最久未使用的 frame |
| | `pin()` | 从淘汰列表中移除（被固定） |
| | `unpin()` | 加入淘汰列表（可被淘汰） |

**实现**：`LRUlist_`（list）+ `LRUhash_`（map），front=最近使用，back=最久未使用。

### 9. Index 模块（`src/index/`）

**职责**：B+树索引实现。

**当前状态**：框架完整，核心方法（lower_bound、upper_bound、insert_entry、delete_entry、split、coalesce 等）为 TODO 桩代码（题目二中不涉及）。

### 10. Transaction 模块（`src/transaction/`）

**职责**：事务管理（ACID）、并发控制（2PL）。

| 文件 | 关键方法 | 说明 |
|------|----------|------|
| `transaction.h` | `Transaction` | 事务对象（状态、写集、锁集） |
| `transaction_manager.h/cpp` | `begin/commit/abort` | 事务生命周期 |
| `concurrency/lock_manager.h/cpp` | `LockManager` | 锁管理器（TODO） |
| `txn_defs.h` | `TransactionState`, `WriteRecord`, `LockDataId` | 事务相关类型定义 |

### 11. Recovery 模块（`src/recovery/`）

**职责**：WAL 日志和故障恢复。

| 文件 | 说明 |
|------|------|
| `log_manager.h/cpp` | WAL 日志读写（部分 TODO） |
| `log_recovery.h/cpp` | 恢复流程（analyze/redo/undo）（部分 TODO） |

### 12. Common 模块（`src/common/`）

**职责**：公共类型定义和配置。

| 文件 | 关键定义 | 说明 |
|------|----------|------|
| `config.h` | `PAGE_SIZE=4096`, `BUFFER_POOL_SIZE=65536`, `DB_META_NAME="db.meta"` | 系统常量 |
| `common.h` | `Value`, `Condition`, `SetClause`, `TabCol`, `CompOp` | 公共数据结构 |
| `context.h` | `Context` | 执行上下文（txn, lock_mgr, log_mgr, data_send） |

## 数据流（以 SELECT 为例）

```
客户端 SQL
  ↓
[Parser] yyparse() → AST (SelectStmt)
  ↓
[Analyze] do_analyze() → Query (tables, cols, conds)
  ↓
[Optimizer] plan_query() → Plan (ProjectionPlan → ScanPlan)
  ↓
[Portal] start() → ExecutorTree (ProjectionExecutor → SeqScanExecutor)
  ↓
[Portal] run() → QlManager::select_from()
  ↓
  输出到客户端 + output.txt
```

## 实现状态（题目二完成后）

| 模块 | 状态 | 备注 |
|------|------|------|
| Parser | ✅ 完整 | 所有 SQL 语法支持 |
| Analyze | ✅ 完整 | 含表/列存在性检查、UpdateStmt 处理 |
| Optimizer | ✅ 可用 | 单表查询完整，JOIN/逻辑优化 TODO |
| Execution | ✅ 可用 | SeqScan/Projection/Insert/Update/Delete 完整 |
| System | ✅ 可用 | create_table/drop_table 完整，索引管理 TODO |
| Record | ✅ 完整 | CRUD + RmScan 完整 |
| Storage | ✅ 完整 | DiskManager + BufferPoolManager 完整 |
| Replacer | ✅ 完整 | LRU 淘汰策略 |
| Index | ⚠️ 桩代码 | B+树核心方法 TODO |
| Transaction | ✅ 基本可用 | begin/commit/abort 完整，锁管理 TODO |
| Recovery | ⚠️ 部分 | WAL 日志读写部分 TODO |
| Server/Client | ✅ 完整 | TCP 通信、多线程连接处理 |
