# RMDB — 简易关系型数据库

基于 **C++17** 实现的关系型数据库系统，涵盖查询引擎、存储引擎、事务管理与故障恢复等核心模块。

## 功能特性

- **查询执行**：支持 SQL 语句的词法/语法分析、优化与执行
- **索引**：唯一索引支持
- **聚合与分组**：`GROUP BY` 及聚合函数
- **半连接**：Semi-Join 查询支持
- **事务管理**：ACID 事务，支持并发控制
- **MVCC**：多版本并发控制
- **故障恢复**：WAL 日志、检查点（Checkpoint）机制

## 技术栈

- **语言**：C++17
- **构建系统**：CMake 3.28.3+
- **词法/语法分析**：Flex 2.6.4 + Bison 3.8.2
- **测试框架**：Google Test

## 项目结构

```
rmdb/
├── src/                   # 服务端源码
│   ├── analyze/           # 语义分析
│   ├── common/            # 公共工具
│   ├── execution/         # 查询执行引擎
│   ├── index/             # 索引模块
│   ├── optimizer/         # 查询优化器
│   ├── parser/            # SQL 词法/语法解析
│   ├── record/            # 记录管理
│   ├── recovery/          # 故障恢复 & WAL
│   ├── replacer/          # 页面替换策略
│   ├── storage/           # 存储引擎
│   ├── system/            # 系统管理
│   ├── transaction/       # 事务与 MVCC
│   └── test/              # 单元测试
├── rmdb_client/           # 客户端
├── deps/                  # 第三方依赖
│   └── googletest/        # Google Test
├── CMakeLists.txt         # 顶层构建配置
└── build/                 # 构建输出（本地生成）
```

## 快速开始

### 环境要求

- CMake >= 3.28.3
- Flex >= 2.6.4
- Bison >= 3.8.2
- 支持 C++17 的编译器（GCC/Clang）

### 编译

```bash
# 服务端（Debug 模式）
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make rmdb -j4

# 客户端
cd rmdb_client/build && cmake .. && make rmdb_client -j4

# 单元测试
cd build && make unit_test && ./bin/unit_test
```

### 运行

```bash
# 启动服务端
./bin/rmdb test_db

# 启动客户端（新终端）
./rmdb_client
```

## 相关文档

- `RMDB项目结构.pdf` — 架构详解
- `RMDB使用文档.pdf` — 编译运行指南
- `框架图.pdf` — 系统框架图
- `测试说明文档v3.0.pdf` — 测试点说明
- `数据一致性检验规则.pdf` / `一致性检测2.pdf` — 一致性规则
- `project_analysis.md` — 模块分析（自动生成）
- `project_structure.txt` — 文件结构清单

## 开发说明

- 代码注释与文档统一使用中文
- 不修改已有公共接口（除非文档明确允许）
- 每通过一个测试点建议提交：`git commit -m "pass: 题目号-测试点号"`