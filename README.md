# RMDB — 简易关系型数据库

基于 **C++17** 从零实现的关系型数据库系统，涵盖查询引擎、存储引擎、索引、事务管理、MVCC 与故障恢复等核心模块。

## 功能特性

| 模块 | 功能 |
|------|------|
| **查询执行** | SQL 词法/语法分析（Flex + Bison）、查询优化（选择/投影下推、连接顺序优化）、计划执行 |
| **索引** | 唯一索引（单列/多列），支持等值查询与范围查询 |
| **聚合与分组** | `COUNT` / `SUM` / `MAX` / `MIN` / `AVG`，`GROUP BY`，`HAVING`，`ORDER BY`，`LIMIT` |
| **半连接** | Semi Join（`SEMI JOIN`），自动去重，支持别名 |
| **事务管理** | `BEGIN` / `COMMIT` / `ABORT`，ACID 特性，两阶段锁（2PL）并发控制 |
| **MVCC** | 多版本并发控制，快照隔离，元组版本链，时间戳管理 |
| **故障恢复** | WAL（Write-Ahead Logging），ARIES 三阶段恢复（Analyze → Redo → Undo），静态检查点 |

## 技术栈

- **语言**：C++17
- **构建系统**：CMake ≥ 3.28
- **词法/语法分析**：Flex 2.6.4 + Bison 3.8.2
- **测试框架**：Google Test（C++ 单元测试）+ Python 自动化集成测试
- **运行环境**：WSL（Windows Subsystem for Linux）

## 项目结构

```
rmdb/
├── src/                       # 服务端源码
│   ├── parser/                # SQL 词法分析（Flex）与语法分析（Bison）
│   ├── analyze/               # 语义分析
│   ├── optimizer/             # 查询优化器（选择/投影下推、连接顺序优化）
│   ├── execution/             # 查询执行引擎
│   ├── index/                 # 唯一索引（B+Tree）
│   ├── record/                # 记录管理（RmFileHandle）
│   ├── storage/               # 存储引擎（DiskManager、BufferPoolManager）
│   ├── replacer/              # 页面替换策略（LRU）
│   ├── transaction/           # 事务管理 + MVCC + 锁管理
│   ├── recovery/              # WAL 日志 + ARIES 故障恢复
│   ├── system/                # 系统管理（SM_Manager、索引重建）
│   ├── common/                # 公共工具
│   ├── rmdb.cpp               # 服务端入口
│   └── unit_test.cpp          # Google Test 单元测试
├── rmdb_client/               # 命令行客户端
│   ├── src/
│   └── build/
├── tools/                     # Python 自动化测试套件
│   ├── run_all_tests.py       # 测试入口（运行全部题目）
│   └── tests/
│       ├── base.py            # 公共基础设施（RMDBTester）
│       ├── test_topic2.py     # 题目二：查询执行
│       ├── test_topic3.py     # 题目三：唯一索引
│       ├── test_topic4.py     # 题目四：选择/投影下推
│       ├── test_topic5.py     # 题目五：聚合与分组
│       ├── test_topic6.py     # 题目六：半连接
│       ├── test_topic7.py     # 题目七：事务控制
│       ├── test_topic8.py     # 题目八：MVCC
│       └── test_topic9.py     # 题目九：故障恢复
├── deps/                      # 第三方依赖
│   └── googletest/            # Google Test
├── CMakeLists.txt             # 顶层 CMake 构建配置
├── build/                     # 构建输出（本地生成，已 gitignore）
├── RMDB项目结构.pdf            # 架构详解
├── RMDB使用文档.pdf            # 编译运行指南
└── 框架图.pdf                  # 系统框架图
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

# Python（用于自动化测试）
sudo apt install -y python3
```

验证安装：

```bash
cmake --version     # ≥ 3.28.3
flex --version      # 2.6.4
bison --version     # 3.8.2
g++ --version       # 支持 C++17
python3 --version   # ≥ 3.8
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

## 自动化测试

Python 测试套件位于 `tools/` 目录，通过 socket 与服务端通信，覆盖题目二~九的全部测试点。

### 运行全部测试

```bash
cd /mnt/d/Python_Project/RMDB_proj/rmdb
python3 tools/run_all_tests.py
```

### 单独运行某个题目

```bash
# 在项目根目录执行
python3 tools/tests/test_topic2.py    # 题目二：查询执行
python3 tools/tests/test_topic3.py    # 题目三：唯一索引（含性能测试，约3-4分钟）
python3 tools/tests/test_topic4.py    # 题目四：选择/投影下推
python3 tools/tests/test_topic5.py    # 题目五：聚合与分组
python3 tools/tests/test_topic6.py    # 题目六：半连接
python3 tools/tests/test_topic7.py    # 题目七：事务控制
python3 tools/tests/test_topic8.py    # 题目八：MVCC
python3 tools/tests/test_topic9.py    # 题目九：故障恢复（含TPC-C，约2-3分钟）
```

每个脚本会自动启动服务端 → 执行测试 → 输出通过/失败 → 停止服务端。

### 测试耗时参考

| 题目 | 耗时 | 说明 |
|------|------|------|
| 题目二 | < 10 秒 | 基本 DDL/DML |
| 题目三 | 3-4 分钟 | 含 1000×1000 索引性能对比 |
| 题目四 | < 30 秒 | 含 EXPLAIN 验证 |
| 题目五 | < 10 秒 | 聚合/分组/排序 |
| 题目六 | < 10 秒 | Semi Join |
| 题目七 | < 10 秒 | 事务提交/回滚 |
| 题目八 | < 10 秒 | MVCC 可见性/版本链 |
| 题目九 | 2-3 分钟 | TPC-C 建表 + crash/recovery 计时 |
| **全部** | **约 8-10 分钟** | |

