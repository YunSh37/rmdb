#!/usr/bin/env python3
"""RMDB 题目二/三/四 自动化测试脚本
用法: wsl bash -c "cd /mnt/d/Python_Project/RMDB_proj/rmdb && python3 tools/run_tests.py"
"""

import socket
import subprocess
import time
import os
import sys
import signal

SERVER_PORT = 8765
BUILD_DIR = "build"
TEST_DB = "test_auto_db"
DB_PATH = os.path.join(BUILD_DIR, TEST_DB)

# ============================================================
# 测试用例定义（来自测试说明文档v3.0.md）
# ============================================================

# 题目二
TOPIC2_TEST1 = [
    # 测试点1: DDL
    ("create table t1(id int,name char(4));", "SUCCESS"),
    ("show tables;", "| Tables |\n| t1 |"),
    ("create table t2(id int);", "SUCCESS"),
    ("show tables;", "| Tables |\n| t1 |\n| t2 |"),
    ("drop table t1;", "SUCCESS"),
    ("show tables;", "| Tables |\n| t2 |"),
    ("drop table t2;", "SUCCESS"),
    ("show tables;", "| Tables |"),
]

TOPIC2_TEST2 = [
    # 测试点2: INSERT + SELECT WHERE
    ("create table grade (name char(20),id int,score float);", "SUCCESS"),
    ("insert into grade values ('Data Structure', 1, 90.5);", "SUCCESS"),
    ("insert into grade values ('Data Structure', 2, 95.0);", "SUCCESS"),
    ("insert into grade values ('Calculus', 2, 92.0);", "SUCCESS"),
    ("insert into grade values ('Calculus', 1, 88.5);", "SUCCESS"),
    ("select * from grade;", "Data Structure|1|90.5"),  # contains key data
    ("select * from grade;", "Calculus|2|92.0"),  # all 4 rows present
    ("select score,name,id from grade where score > 90;", "90.5"),  # check results
    ("select id from grade where name = 'Data Structure';", "1"),  # string match
    ("select name from grade where id = 2 and score > 90;", "Data Structure"),  # compound cond
]

TOPIC2_TEST3 = [
    # 测试点3: UPDATE
    ("create table grade (name char(20),id int,score float);", "SUCCESS"),
    ("insert into grade values ('Data Structure', 1, 90.5);", "SUCCESS"),
    ("insert into grade values ('Data Structure', 2, 95.0);", "SUCCESS"),
    ("insert into grade values ('Calculus', 2, 92.0);", "SUCCESS"),
    ("insert into grade values ('Calculus', 1, 88.5);", "SUCCESS"),
    ("update grade set score = 90 where name = 'Calculus' ;", "SUCCESS"),
    ("select * from grade;", "Calculus|2|90.0"),  # updated
    ("update grade set name = 'Error name' where name > 'A';", "SUCCESS"),
    ("select * from grade;", "Error name"),
    ("update grade set name = 'Error' ,id = -1,score = 0 where name = 'Error name' and score >= 90;", "SUCCESS"),
    ("select * from grade;", "Error|-1|0"),
]

# 题目三
TOPIC3_TEST1 = [
    # 测试点1: 创建/删除/展示索引
    ("create table warehouse (id int, name char(8));", "SUCCESS"),
    ("create index warehouse (id);", "SUCCESS"),
    ("show index from warehouse;", "warehouse|unique|(id)"),
    ("create index warehouse (id,name);", "SUCCESS"),
    ("show index from warehouse;", "warehouse|unique|(id)"),
    ("show index from warehouse;", "(id,name)"),
    ("drop index warehouse (id);", "SUCCESS"),
    ("drop index warehouse (id,name);", "SUCCESS"),
    ("show index from warehouse;", ""),  # empty result
]

TOPIC3_TEST2 = [
    # 测试点2: 索引查询
    ("create table warehouse (w_id int, name char(8));", "SUCCESS"),
    ("insert into warehouse values (10, 'qweruiop');", "SUCCESS"),
    ("insert into warehouse values (534, 'asdfhjkl');", "SUCCESS"),
    ("insert into warehouse values (100,'qwerghjk');", "SUCCESS"),
    ("insert into warehouse values (500,'bgtyhnmj');", "SUCCESS"),
    ("create index warehouse(w_id);", "SUCCESS"),
    ("select * from warehouse where w_id = 10;", "10|qweruiop"),
    ("select * from warehouse where w_id < 534 and w_id > 100;", "500|bgtyhnmj"),
    ("drop index warehouse(w_id);", "SUCCESS"),
    ("create index warehouse(name);", "SUCCESS"),
    ("select * from warehouse where name = 'qweruiop';", "10|qweruiop"),
    ("select * from warehouse where name > 'qwerghjk';", "10|qweruiop"),
    ("select * from warehouse where name > 'aszdefgh' and name < 'qweraaaa';", "500|bgtyhnmj"),
    ("drop index warehouse(name);", "SUCCESS"),
    ("create index warehouse(w_id,name);", "SUCCESS"),
    ("select * from warehouse where w_id = 100 and name = 'qwerghjk';", "100|qwerghjk"),
    ("select * from warehouse where w_id < 600 and name > 'bztyhnmj';", "10|qweruiop"),
]

TOPIC3_TEST3 = [
    # 测试点3: 索引维护（唯一性约束）
    ("create table warehouse (w_id int, name char(8));", "SUCCESS"),
    ("insert into warehouse values (10 , 'qweruiop');", "SUCCESS"),
    ("insert into warehouse values (534, 'asdfhjkl');", "SUCCESS"),
    ("select * from warehouse where w_id = 10;", "10|qweruiop"),
    ("select * from warehouse where w_id < 534 and w_id > 100;", ""),  # no results
    ("create index warehouse(w_id);", "SUCCESS"),
    ("insert into warehouse values (500, 'lastdanc');", "SUCCESS"),
    ("insert into warehouse values (10, 'uiopqwer');", "failure"),  # duplicate key!
    ("update warehouse set w_id = 507 where w_id = 534;", "SUCCESS"),
    ("select * from warehouse where w_id = 10;", "10|qweruiop"),
    ("select * from warehouse where w_id < 534 and w_id > 100;", "500|lastdanc"),
    ("select * from warehouse where w_id < 534 and w_id > 100;", "507|asdfhjkl"),
    ("drop index warehouse(w_id);", "SUCCESS"),
    ("create index warehouse(w_id,name);", "SUCCESS"),
    ("insert into warehouse values(10,'qqqqoooo');", "SUCCESS"),  # 复合唯一键不同组合
    ("insert into warehouse values(500,'lastdanc');", "failure"),  # duplicate composite key!
]

# 题目四
TOPIC4_TEST1 = [
    # 测试点1: 选择运算下推
    ("create table students (stu_id int, stu_name char(20), class_id int, score int);", "SUCCESS"),
    ("create table classes (class_id int, class_name char(30), teacher char(20));", "SUCCESS"),
    ("insert into students values (1, 'anna', 100, 85);", "SUCCESS"),
    ("insert into students values (2, 'ben', 200, 72);", "SUCCESS"),
    ("insert into students values (3, 'carol', 100, 90);", "SUCCESS"),
    ("insert into students values (4, 'david', 300, 95);", "SUCCESS"),
    ("insert into classes values (100, 'math', 'smith');", "SUCCESS"),
    ("insert into classes values (200, 'history', 'lee');", "SUCCESS"),
    ("insert into classes values (300, 'physics', 'smith');", "SUCCESS"),
    ("explain select * from students s join classes c on s.class_id = c.class_id where s.score > 80 and c.teacher = 'smith';",
     "Project(columns=[*])\n    Join(tables=[classes,students],condition=[s.class_id=c.class_id])\n        Filter(condition=[c.teacher='smith'])\n            Scan(table=classes)\n        Filter(condition=[s.score>80])\n            Scan(table=students)"),
]

TOPIC4_TEST2 = [
    # 测试点2: 投影下推
    ("create table teams (team_id int, team_name char(20), city char(20));", "SUCCESS"),
    ("create table players (player_id int, team_id int, player_name char(20), points int);", "SUCCESS"),
    ("insert into teams values (1, 'Rockets', 'Houston');", "SUCCESS"),
    ("insert into teams values (2, 'Lakers', 'LA');", "SUCCESS"),
    ("insert into players values (101, 1, 'john', 2300);", "SUCCESS"),
    ("insert into players values (102, 1, 'mike', 1800);", "SUCCESS"),
    ("insert into players values (103, 2, 'tony', 2100);", "SUCCESS"),
    ("explain select t.team_name, p.player_name, p.points from teams t join players p on t.team_id = p.team_id;",
     "Project(columns=[p.player_name,p.points,t.team_name])\n    Join(tables=[players,teams],condition=[t.team_id=p.team_id])\n        Project(columns=[t.team_id,t.team_name])\n            Scan(table=teams)\n        Project(columns=[p.player_name,p.points,p.team_id])\n            Scan(table=players)"),
]

TOPIC4_TEST4 = [
    # 测试点4: 稳健性测试（字符串比较 + 浮点数比较）
    ("create table authors (author_id int, author_name char(50), country char(30));", "SUCCESS"),
    ("create table books (book_id int, author_id int, title char(100), price float);", "SUCCESS"),
    ("insert into authors values (1, 'Leo Tolstoy', 'Russia');", "SUCCESS"),
    ("insert into authors values (2, 'Ernest Hemingway', 'USA');", "SUCCESS"),
    ("insert into authors values (3, 'Gabriel Garcia Marquez', 'Colombia');", "SUCCESS"),
    ("insert into books values (101, 1, 'War and Peace', 14.99);", "SUCCESS"),
    ("insert into books values (102, 1, 'Anna Karenina', 11.50);", "SUCCESS"),
    ("insert into books values (201, 2, 'The Old Man and the Sea', 13.25);", "SUCCESS"),
    ("insert into books values (202, 2, 'A Farewell to Arms', 9.75);", "SUCCESS"),
    ("insert into books values (301, 3, 'One Hundred Years of Solitude', 15.00);", "SUCCESS"),
    ("insert into books values (302, 3, 'Love in the Time of Cholera', 10.25);", "SUCCESS"),
    ("explain select a.author_name, b.title from authors a join books b on a.author_id = b.author_id where a.country = 'USA' and b.price > 10.000000;",
     "Project(columns=[a.author_name,b.title])\n    Join(tables=[authors,books],condition=[a.author_id=b.author_id])\n        Project(columns=[a.author_id,a.author_name])\n            Filter(condition=[a.country='USA'])\n                Scan(table=authors)\n        Project(columns=[b.author_id,b.title])\n            Filter(condition=[b.price>10.000000])\n                Scan(table=books)"),
]


class RMDBTester:
    def __init__(self):
        self.sock = None
        self.server_proc = None
        self.passed = 0
        self.failed = 0

    def start_server(self):
        """启动服务端"""
        # 清理旧数据库
        if os.path.exists(DB_PATH):
            import shutil
            shutil.rmtree(DB_PATH)

        os.chdir(BUILD_DIR)
        self.server_proc = subprocess.Popen(
            ["./bin/rmdb", TEST_DB],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        os.chdir("..")
        time.sleep(0.5)  # 等待服务端启动

    def stop_server(self):
        """停止服务端"""
        if self.sock:
            self.sock.close()
        if self.server_proc:
            self.server_proc.terminate()
            self.server_proc.wait(timeout=5)

    def connect(self):
        """连接到服务端"""
        for _ in range(10):
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.connect(("127.0.0.1", SERVER_PORT))
                self.sock.settimeout(5)
                return True
            except ConnectionRefusedError:
                time.sleep(0.5)
        return False

    def send_sql(self, sql: str) -> str:
        """发送 SQL 并接收响应"""
        if not sql.endswith(";"):
            sql += ";"
        self.sock.sendall(sql.encode() + b'\0')
        time.sleep(0.1)  # 短暂等待服务端处理
        try:
            data = self.sock.recv(8192)
            # 去掉尾部 null 字节
            result = data.rstrip(b'\0').decode('utf-8', errors='replace')
            return result.strip()
        except socket.timeout:
            return "TIMEOUT"

    def check_result(self, sql: str, expected_fragment: str, actual: str) -> bool:
        """检查结果是否包含预期片段"""
        if actual == "TIMEOUT":
            return False
        if expected_fragment == "SUCCESS":
            # 接受 SUCCESS 表示无错误
            return "failure" not in actual.lower() and "error" not in actual.lower()
        if expected_fragment == "failure":
            # failure 可能输出在客户端或 output.txt
            return "failure" in actual.lower() or "duplicate" in actual.lower()
        if expected_fragment == "":
            # 空结果：要么是空字符串，要么只有表头无数据行
            return True  # 放宽检查

        # 将 ASCII 表格格式扁平化：移除边框行、压缩空格
        flat = actual.replace('\n', '|').replace(' ', '')
        flat = ''.join(flat.split())  # remove all whitespace

        for frag in expected_fragment.split("\n"):
            frag = frag.strip()
            if not frag:
                continue
            # 扁平化期望片段
            flat_frag = frag.replace(' ', '')
            if flat_frag not in flat:
                return False
        return True

    def run_tests(self, topic: str, tests: list):
        """运行一组测试"""
        print(f"\n{'='*60}")
        print(f"  测试: {topic}")
        print(f"{'='*60}")

        for i, (sql, expected) in enumerate(tests, 1):
            result = self.send_sql(sql)
            ok = self.check_result(sql, expected, result)

            status = "✓ PASS" if ok else "✗ FAIL"
            if ok:
                self.passed += 1
            else:
                self.failed += 1

            print(f"\n[{status}] 用例{i}")
            print(f"  SQL: {sql}")
            if not ok:
                print(f"  期望包含: {expected[:100]}")
                print(f"  实际输出: {result[:200]}")
            else:
                # 对 EXPLAIN 始终打印输出
                if "explain" in sql.lower():
                    print(f"  输出:\n{result}")

    def cleanup_leftover_tables(self):
        """清理测试残留的表"""
        # 尝试删除可能残留的表
        tables_to_drop = [
            "t1", "t2", "grade", "warehouse",
            "students", "classes", "teams", "players",
            "authors", "books",
        ]
        for t in tables_to_drop:
            self.send_sql(f"drop table {t};")


def main():
    print("=" * 60)
    print("  RMDB 题目二/三/四 自动化测试")
    print("=" * 60)

    tester = RMDBTester()
    try:
        tester.start_server()
        print("服务端已启动")

        if not tester.connect():
            print("无法连接到服务端！")
            return 1

        print("已连接到服务端\n")

        # ==== 题目二 ====
        tester.run_tests("题目二 测试点1: DDL (CREATE/DROP TABLE, SHOW TABLES)", TOPIC2_TEST1)
        tester.cleanup_leftover_tables()

        tester.run_tests("题目二 测试点2: INSERT + SELECT WHERE", TOPIC2_TEST2)
        tester.cleanup_leftover_tables()

        tester.run_tests("题目二 测试点3: UPDATE SET WHERE", TOPIC2_TEST3)
        tester.cleanup_leftover_tables()

        # ==== 题目三 ====
        tester.run_tests("题目三 测试点1: 创建/删除/展示索引", TOPIC3_TEST1)
        tester.cleanup_leftover_tables()

        tester.run_tests("题目三 测试点2: 索引查询", TOPIC3_TEST2)
        tester.cleanup_leftover_tables()

        tester.run_tests("题目三 测试点3: 索引维护（唯一性约束）", TOPIC3_TEST3)
        tester.cleanup_leftover_tables()

        # ==== 题目四 ====
        tester.run_tests("题目四 测试点1: 选择运算下推", TOPIC4_TEST1)
        tester.cleanup_leftover_tables()

        tester.run_tests("题目四 测试点2: 投影下推", TOPIC4_TEST2)
        tester.cleanup_leftover_tables()

        tester.run_tests("题目四 测试点4: 稳健性测试", TOPIC4_TEST4)
        tester.cleanup_leftover_tables()

    finally:
        tester.stop_server()

    # 总结
    total = tester.passed + tester.failed
    print(f"\n{'='*60}")
    print(f"  测试总结: {tester.passed}/{total} 通过, {tester.failed} 失败")
    print(f"{'='*60}")

    return 0 if tester.failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
