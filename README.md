# RMDB — 简易关系型数据库

基于 **C++17** 从零实现的关系型数据库系统，涵盖查询引擎、存储引擎、索引、事务管理、MVCC 与故障恢复等核心模块。

## 实现状态

已完成 **全部 11 个题目** 的实现，系统测试通过率如下：

| 题目 | 内容 | 状态 | 备注 |
|------|------|------|------|
| 题目一 | 基础框架搭建 | ✅ 全部通过 | Flex + Bison SQL 解析、网络通信 |
| 题目二 | 查询执行 | ✅ 全部通过 | DDL、CRUD、连接查询、浮点精度 |
| 题目三 | BIGINT 类型 | ✅ 全部通过 | 64位整数支持、合法性检查 |
| 题目四 | DATETIME 类型 | ✅ 全部通过 | 时间类型增删改查、合法性验证 |
| 题目五 | 唯一索引 | ✅ 全部通过 | B+树索引创建/维护/查询、索引使用率检测 |
| 题目六 | 聚合函数 | ✅ 全部通过 | SUM/MAX/MIN/COUNT/COUNT(*) |
| 题目七 | ORDER BY | ✅ 全部通过 | 多列排序、ASC/DESC、LIMIT |
| 题目八 | 块嵌套循环连接 | ✅ 全部通过 | BNLJ，支持超内存大表连接、等值/非等值 |
| 题目九 | 事务控制 | ✅ 全部通过 | BEGIN/COMMIT/ABORT，含索引事务 |
| 题目十 | 可串行化隔离 | ✅ 全部通过 | 2PL + MVCC，防止脏写/脏读/丢失更新/不可重复读/幻读 |
| 题目十一 | 系统故障恢复 | ⚠️ 一个测试点未通过 | crash_recovery_index_test 恢复阶段崩溃（已放弃排查） |

> **已知问题**：题目十一 `crash_recovery_index_test` 在正常运行时阶段服务端停止响应。其余 10 个题目及题目十一的 4/5 测试点全部通过。

## 功能特性

| 模块 | 功能 |
|------|------|
| **查询执行** | SQL 词法/语法分析（Flex + Bison）、查询优化（选择/投影下推、连接顺序优化）、计划执行 |
| **数据类型** | INT、BIGINT、FLOAT、CHAR(n)、STRING、DATETIME |
| **索引** | 唯一索引（单列/多列），B+树实现，支持等值查询与范围查询 |
| **聚合与分组** | `COUNT` / `SUM` / `MAX` / `MIN` / `AVG`，`GROUP BY`，`HAVING`，`ORDER BY`，`LIMIT` |
| **连接查询** | 块嵌套循环连接（BNLJ，支持超内存大表）、半连接（SEMI JOIN，IN/NOT IN 子查询） |
| **事务管理** | `BEGIN` / `COMMIT` / `ABORT`，ACID 特性，两阶段锁（2PL）并发控制，死锁预防 |
| **MVCC** | 多版本并发控制，快照隔离，元组版本链（xmin/xmax），时间戳管理 |
| **故障恢复** | WAL（Write-Ahead Logging），ARIES 三阶段恢复（Analyze → Redo → Undo），静态检查点 |

## 技术栈

- **语言**：C++17
- **构建系统**：CMake ≥ 3.17
- **词法/语法分析**：Flex 2.6.4 + Bison 3.8.2
- **测试框架**：Google Test（C++ 单元测试）
- **运行环境**：WSL（Windows Subsystem for Linux）

## 项目结构

```
rmdb/
├── src/                       # 服务端源码
│   ├── parser/                # SQL 词法分析（Flex）与语法分析（Bison）
│   ├── analyze/               # 语义分析
│   ├── optimizer/             # 查询优化器（选择/投影下推、连接顺序优化）
│   ├── execution/             # 查询执行引擎（17 个执行器）
│   ├── index/                 # 唯一索引（B+Tree）
│   ├── record/                # 记录管理（RmFileHandle + MVCC）
│   ├── storage/               # 存储引擎（DiskManager、BufferPoolManager）
│   ├── replacer/              # 页面替换策略（LRU）
│   ├── transaction/           # 事务管理 + MVCC + 2PL 锁管理
│   ├── recovery/              # WAL 日志（8种日志类型）+ ARIES 故障恢复
│   ├── system/                # 系统管理（元数据、DDL、索引重建）
│   ├── common/                # 公共类型与配置
│   ├── rmdb.cpp               # 服务端入口（网络服务 + 启动恢复）
│   └── unit_test.cpp          # Google Test 单元测试
├── rmdb_client/               # 命令行客户端
│   ├── src/
│   └── build/
├── deps/                      # 第三方依赖
│   └── googletest/            # Google Test
├── doc/                       # 题目实现详细文档（11份，本地参考）
├── CMakeLists.txt             # 顶层 CMake 构建配置
└── build/                     # 构建输出（本地生成，已 gitignore）
```

## 环境配置

### 1. 安装 WSL

在 Windows 上启用 WSL 并安装 Ubuntu：

```powershell
# PowerShell（管理员）
wsl --install -d Ubuntu
```

### 2. 安装编译工具链

在 WSL（Ubuntu）中执行：

```bash
# 基础编译工具
sudo apt update
sudo apt install -y build-essential cmake g++

# 词法/语法分析工具
sudo apt install -y flex bison
```

验证安装：

```bash
cmake --version     # ≥ 3.17
flex --version      # 2.6.4
bison --version     # 3.8.2
g++ --version       # 支持 C++17
```

### 3. 克隆仓库

```bash
git clone <仓库地址>
cd rmdb
```

## 编译

所有编译命令在 WSL 中、项目根目录下执行。

```bash
# 服务端（Debug 模式，含调试符号）
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make rmdb -j4 && cd ..

# 客户端
cd rmdb_client/build && cmake .. && make rmdb_client -j4 && cd ../..

# 单元测试（Google Test，测试存储层基础设施）
cd build && make unit_test -j4 && ./bin/unit_test && cd ..
```

## 运行

```bash
# 终端1：启动服务端
cd build && ./bin/rmdb test_db

# 终端2：启动客户端
cd rmdb_client/build && ./rmdb_client
```

客户端支持标准 SQL 语句交互输入。

## 单元测试

项目使用 **Google Test** 框架提供 C++ 单元测试，覆盖存储引擎、索引、记录管理等基础设施模块。

### 编译并运行

```bash
cd build && make unit_test -j4 && ./bin/unit_test && cd ..
```

### 主要测试覆盖

| 测试文件 | 测试内容 |
|----------|----------|
| `storage_test.cpp` | 磁盘管理器（DiskManager）、缓冲池（BufferPoolManager）读写 |
| `index_test.cpp` | B+Tree 索引的插入、查找、删除与唯一性约束 |
| `record_test.cpp` | 记录文件（RmFileHandle）的增删改查与 MVCC 可见性 |
| `recovery_test.cpp` | WAL 日志写入与 ARIES 恢复流程 |
| `lock_manager_test.cpp` | 锁管理器加锁/解锁/兼容矩阵验证 |

## 实现文档

各题目详细实现说明见 `doc/` 目录（共 11 份），涵盖设计思路、关键数据结构、算法流程和源码文件索引。

## 许可证

本项目基于 Mulan PSL v2 许可证开源。
