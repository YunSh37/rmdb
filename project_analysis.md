# RMDB 项目分析文档

> 最后更新：2026-07-03 | 题目四"选择运算下推与投影下推"完成后更新（支持表别名和 JOIN ON）

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
| `ast.h/cpp` | AST 节点定义（CreateTable、SelectStmt、InsertStmt、FromClause 等） |
| `parse_node.h` | 语法树节点基类 |
| `parser_defs.h` | 解析器常量定义 |

**支持的 SQL 语法**：
- DDL: `CREATE TABLE`, `DROP TABLE`, `DESC`, `CREATE INDEX`, `DROP INDEX`
- DML: `INSERT INTO`, `DELETE FROM`, `UPDATE`, `SELECT`, `EXPLAIN SELECT`
- 其他: `SHOW TABLES`, `SHOW INDEX FROM`, `HELP`, `EXIT`, `BEGIN/COMMIT/ABORT/ROLLBACK`
- 表达式: 6 种比较运算符（`=` `<>` `<` `>` `<=` `>=`），AND 连接
- 数据类型: INT, FLOAT, CHAR(n)
- **表别名**: `table_name alias`（如 `students s`）
- **JOIN ON**: `JOIN table_name [alias] ON condition`（如 `join classes c on s.class_id = c.class_id`）

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
| `optimizer.h` | `plan_query()` | 优化入口，路由到 planner；EXPLAIN 查询额外包装 ExplainPlan |
| `planner.h/cpp` | `do_planner()` | 根据 AST 类型生成 Plan |
| | `generate_select_plan()` | SELECT 计划生成（含顶层投影，检测 SELECT *） |
| | `make_one_rel()` | 多表 JOIN 计划（选择下推、投影下推、连接顺序优化） |
| `plan.h` | `ScanPlan`, `JoinPlan`, `ProjectionPlan`, `FilterPlan`, `ExplainPlan`, `SortPlan`, `DMLPlan`, `DDLPlan`, `OtherPlan` | 计划节点类型定义 |

**新增计划类型**：
- `FilterPlan`：选择下推节点，包装单表过滤条件（仅用于 EXPLAIN 显示）
- `ExplainPlan`：EXPLAIN 包装节点，Portal 检测后打印计划树而非执行
- `ProjectionPlan::is_star_`：标记 SELECT * 查询

**优化规则**：
- **选择下推**：过滤条件（`is_rhs_val`）按表分离，包装为 FilterPlan
- **投影下推**：多表 JOIN 时，每个 Scan 分支只保留需要的列（SELECT + JOIN + WHERE 涉及的列）
- **连接顺序优化**：按表估计大小（`num_pages × num_records_per_page`）升序排列，小表优先作为左子树

### 4. Execution 模块（`src/execution/`）

**职责**：执行器实现，按执行计划树递归执行。

| 文件 | 类 | 说明 |
|------|-----|------|
| `executor_abstract.h` | `AbstractExecutor` | 执行器基类 |
| `executor_seq_scan.h` | `SeqScanExecutor` | 全表扫描 + 条件过滤 |
| `executor_index_scan.h` | `IndexScanExecutor` | 索引扫描 + 条件过滤 + 回表 |
| `executor_insert.h` | `InsertExecutor` | 插入记录 + 索引维护 + 唯一性检查 |
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
| | `show_tables/desc_table/show_index` | 元数据查询 |
| | `create_index/drop_index` | 索引管理（含全表扫描构建索引） |
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

**职责**：B+树唯一索引实现，支持索引创建/删除/查询/维护。

| 文件 | 关键方法 | 说明 |
|------|----------|------|
| `ix_defs.h` | `IxFileHdr`, `IxPageHdr`, `Iid` | 索引文件头、页面头、索引槽标识 |
| `ix_index_handle.h/cpp` | `IxNodeHandle`, `IxIndexHandle` | B+树核心实现 |
| | `lower_bound/upper_bound` | 二分查找定位 ≥/＞ key 的索引槽 |
| | `leaf_lookup/internal_lookup` | 叶节点/内部节点查找 |
| | `insert/insert_entry` | 插入键值对（含重复键检查） |
| | `delete_entry/remove` | 删除键值对 |
| | `split/insert_into_parent` | 节点分裂 |
| | `coalesce_or_redistribute/coalesce/redistribute` | 合并与重分配 |
| | `adjust_root` | 根节点调整 |
| | `get_value` | 精确查找键值对 |
| `ix_manager.h` | `IxManager` | 索引文件生命周期管理 |
| | `create_index/destroy_index` | 创建/销毁索引文件 |
| | `open_index/close_index` | 打开/关闭索引文件 |
| `ix_scan.h/cpp` | `IxScan` | 索引扫描迭代器（遍历叶子链表） |
| `ix.h` | — | 聚合头文件 |

**B+树页面结构**：
- **页面 0**（文件头）：`IxFileHdr`（root_page, first_leaf, last_leaf, col_types/lens, btree_order）
- **页面 1**（叶子链表头）：哨兵节点，prev_leaf/next_leaf 指向根节点
- **页面 2**（根节点）：B+树的根，初始为叶子节点
- **页面 3+**（数据节点）：`[IxPageHdr:24B][keys][rids]`

**唯一索引实现**：
- 插入/更新时，在应用层（`InsertExecutor`/`UpdateExecutor`）通过 `get_value()` 检查键是否重复
- 重复时抛出 `DuplicateKeyError`，被 `rmdb.cpp` 捕获后将 "failure" 写入 `output.txt`
- `IxNodeHandle::insert()` 底层也有重复键检测（静默忽略，不抛异常）

### 10. Transaction 模块（`src/transaction/`）

**职责**：事务管理（ACID）、并发控制（2PL）。

| 文件 | 关键方法 | 说明 |
|------|----------|------|
| `transaction.h` | `Transaction` | 事务对象（状态、写集、锁集） |
| `transaction_manager.h/cpp` | `begin/commit/abort` | 事务生命周期 |
| `concurrency/lock_manager.h/cpp` | `LockManager` | 锁管理器（TODO） |
| `txn_defs.h` | `TransactionState`, `WriteRecord`, `LockDataId` | 事务相关类型定义 |

### 11. Recovery 模块（`src/recovery/`）

**职责**：WAL 日志和故障恢复（ARIES算法）。

| 文件 | 说明 |
|------|------|
| `log_defs.h` | 日志记录头部偏移常量、序列化偏移定义 |
| `log_manager.h/cpp` | WAL日志管理器：7种日志类型（Begin/Commit/Abort/Insert/Delete/Update/Checkpoint）、LogBuffer、LogManager（add_log_to_buffer/flush_log_to_disk） |
| `log_recovery.h/cpp` | ARIES三阶段恢复：analyze（构建ATT+DPT）→ redo（历史重做）→ undo（回滚loser事务）；支持检查点加速恢复 |

**日志记录类型**：
| 类型 | 包含数据 | 用途 |
|------|----------|------|
| BeginLogRecord | txn_id | 标记事务开始 |
| CommitLogRecord | txn_id | 标记事务已提交 |
| AbortLogRecord | txn_id | 标记事务已回滚 |
| InsertLogRecord | table_name, record(含MVCC头), rid | REDO重放插入、UNDO回滚删除 |
| DeleteLogRecord | table_name, old_record(含MVCC头), rid | REDO重放删除、UNDO恢复记录 |
| UpdateLogRecord | table_name, old_record, new_record, rid | REDO重放更新、UNDO恢复旧值 |
| CheckpointLogRecord | ATT快照、DPT快照 | 缩短恢复时间 |

**恢复流程**（`rmdb.cpp`启动时自动执行）：
1. `analyze()`: 扫描日志 → 构建ATT + DPT + aborted_txns → 确定redo_lsn
2. `redo()`: 从redo_lsn开始重放所有操作（跳过已中止事务），页面LSN ≥ 日志LSN则跳过
3. `undo()`: 回滚ATT中所有未完成事务（沿prev_lsn链逆向）
4. 截断旧日志文件，创建新日志文件

**检查点**（`create static_checkpoint`）：
- 刷盘所有脏页
- 记录当前ATT和DPT快照
- 恢复时从检查点LSN开始（跳过早于检查点的日志）

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

## 实现状态（题目四完成后）

| 模块 | 状态 | 备注 |
|------|------|------|
| Parser | ✅ 完整 | 所有 SQL 语法支持，含 EXPLAIN、CREATE/DROP INDEX、SHOW INDEX |
| Analyze | ✅ 完整 | 含 ExplainStmt 解包、表/列存在性检查、UpdateStmt 处理 |
| Optimizer | ✅ 可用 | 选择下推（FilterPlan）、投影下推（ProjectionPlan）、连接顺序优化（按表大小）、EXPLAIN 计划打印 |
| Execution | ✅ 可用 | SeqScan/IndexScan/Projection/Insert/Update/Delete 完整，含索引维护，NestedLoopJoin TODO |
| System | ✅ 可用 | create_table/drop_table/create_index/drop_index/show_index 完整 |
| Record | ✅ 完整 | CRUD + RmScan 完整 |
| Storage | ✅ 完整 | DiskManager + BufferPoolManager 完整 |
| Replacer | ✅ 完整 | LRU 淘汰策略 |
| Index | ✅ 完整 | B+树唯一索引，含 split/coalesce/redistribute/adjust_root |
| Transaction | ✅ 完整 | begin/commit/abort + MVCC + 2PL 锁管理 + WAL日志 |
| Recovery | ✅ 完整 | ARIES三阶段恢复 + 7种日志类型 + 静态检查点 |
| Server/Client | ✅ 完整 | TCP 通信、多线程连接处理 |
