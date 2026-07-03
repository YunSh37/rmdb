"""题目二：查询执行
测试点1: DDL (CREATE/DROP TABLE, SHOW TABLES)
测试点2: INSERT + SELECT WHERE
测试点3: UPDATE SET WHERE
"""

try:
    from .base import RMDBTester
except ImportError:
    from base import RMDBTester

# ============================================================
# 测试数据
# ============================================================

TOPIC2_TEST1 = [
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
    ("create table grade (name char(20),id int,score float);", "SUCCESS"),
    ("insert into grade values ('Data Structure', 1, 90.5);", "SUCCESS"),
    ("insert into grade values ('Data Structure', 2, 95.0);", "SUCCESS"),
    ("insert into grade values ('Calculus', 2, 92.0);", "SUCCESS"),
    ("insert into grade values ('Calculus', 1, 88.5);", "SUCCESS"),
    ("select * from grade;", "Data Structure|1|90.5"),
    ("select * from grade;", "Calculus|2|92.0"),
    ("select score,name,id from grade where score > 90;", "90.5"),
    ("select id from grade where name = 'Data Structure';", "1"),
    ("select name from grade where id = 2 and score > 90;", "Data Structure"),
]

TOPIC2_TEST3 = [
    ("create table grade (name char(20),id int,score float);", "SUCCESS"),
    ("insert into grade values ('Data Structure', 1, 90.5);", "SUCCESS"),
    ("insert into grade values ('Data Structure', 2, 95.0);", "SUCCESS"),
    ("insert into grade values ('Calculus', 2, 92.0);", "SUCCESS"),
    ("insert into grade values ('Calculus', 1, 88.5);", "SUCCESS"),
    ("update grade set score = 90 where name = 'Calculus' ;", "SUCCESS"),
    ("select * from grade;", "Calculus|2|90.0"),
    ("update grade set name = 'Error name' where name > 'A';", "SUCCESS"),
    ("select * from grade;", "Error name"),
    ("update grade set name = 'Error' ,id = -1,score = 0 "
     "where name = 'Error name' and score >= 90;", "SUCCESS"),
    ("select * from grade;", "Error|-1|0"),
]


def run(tester: RMDBTester):
    """执行题目二全部测试点"""
    tester.run_tests("题目二 测试点1: DDL (CREATE/DROP TABLE, SHOW TABLES)", TOPIC2_TEST1)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目二 测试点2: INSERT + SELECT WHERE", TOPIC2_TEST2)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目二 测试点3: UPDATE SET WHERE", TOPIC2_TEST3)
    tester.cleanup_leftover_tables()


if __name__ == "__main__":
    import sys, os
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from base import RMDBTester

    tester = RMDBTester()
    try:
        tester.start_server()
        print("服务端已启动")
        if not tester.connect():
            print("无法连接到服务端！"); sys.exit(1)
        print("已连接到服务端\n")
        run(tester)
    finally:
        tester.stop_server()

    total = tester.passed + tester.failed
    print(f"\n测试总结: {tester.passed}/{total} 通过, {tester.failed} 失败")
    sys.exit(0 if tester.failed == 0 else 1)
