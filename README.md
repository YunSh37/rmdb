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
├── deps/                      # 第三方依赖
│   └── googletest/            # Google Test
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

`make unit_test` 编译单元测试二进制文件，`./bin/unit_test` 直接运行并输出每项测试的通过/失败结果。

### 主要测试覆盖

| 测试文件 | 测试内容 |
|----------|----------|
| `storage_test.cpp` | 磁盘管理器（DiskManager）、缓冲池（BufferPoolManager）读写 |
| `index_test.cpp` | B+Tree 索引的插入、查找、删除与唯一性约束 |
| `record_test.cpp` | 记录文件（RmFileHandle）的增删改查与 MVCC 可见性 |
| `recovery_test.cpp` | WAL 日志写入与 ARIES 恢复流程 |
| `lock_manager_test.cpp` | 锁管理器加锁/解锁/兼容矩阵验证 |

### 测试结果示例

```
[==========] Running 25 tests from 5 test suites.
[----------] 5 tests from StorageTest
[       OK ] StorageTest.DiskReadWrite (0 ms)
...
[----------] 5 tests from IndexTest
[       OK ] IndexTest.InsertAndLookup (0 ms)
...
[==========] 25 tests from 5 test suites ran. (120 ms total)
[  PASSED  ] 25 tests.
```

