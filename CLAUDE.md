# RMDB 全局指引

## 项目定位
C++17 关系型数据库，实现：查询执行、唯一索引、聚合与分组、半连接、事务、MVCC、故障恢复（含检查点）。测试点详见 `测试说明文档v3.0.pdf`，一致性规则见 `数据一致性检验规则.pdf` 和 `一致性检测2.pdf`。

## 关键文件索引
- 项目结构总览：`project_structure.txt` `project_analysis.md`（需持续维护）
- 架构详解：`RMDB项目结构.pdf`
- 编译运行指南：`RMDB使用文档.pdf`
- 框架图：`框架图.pdf`

## 环境说明
- wsl环境下运行
- cmake version 3.28.3
- flex 2.6.4
- bison (GNU Bison) 3.8.2

## 核心技能
### 1. 理解项目结构
- 阅读 `project_analysis.md`，记录每个模块的类、关键方法、文件依赖。
- 每次修改前先查阅 `project_analysis.md`；若结构变化（增删文件），同步更新 `project_analysis.md` 和 `project_structure.txt`。
- pdf文件读取如有困难，先用 pdftotext或者其它工具转换

### 2. Git 管理
- 建议每通过一个题目就 `git add . && git commit -m "pass: 题目名称"`。
- 建议每通过一个测试点就 `git add . && git commit -m "pass: 题目号-测试点号"`。


## 编译与测试
```bash
# 服务端
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make rmdb -j4
# 客户端
cd rmdb_client/build && cmake .. && make rmdb_client -j4
# 运行
./bin/rmdb test_db   # 服务端
./rmdb_client        # 客户端（新终端）
# 单元测试
make unit_test && ./bin/unit_test
```

## 注意事项
- 不要修改已有公共接口（除非文档明确允许）。
- 复杂问题先查 `RMDB项目结构.pdf` 对应章节。
- 所有输出、代码注释使用中文。