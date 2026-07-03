"""题目七：事务控制语句
测试点1: 事务提交(不含索引)
测试点2: 事务回滚(不含索引)
测试点3: 事务提交(含唯一索引)
测试点4: 事务回滚(含唯一索引)
"""

try:
    from .base import RMDBTester
except ImportError:
    from base import RMDBTester

# ============================================================
# 测试数据
# ============================================================

TOPIC7_COMMIT_TEST = [
    ("create table student (id int, name char(8), score float);", "SUCCESS"),
    ("insert into student values (1, 'xiaohong', 90.0);", "SUCCESS"),
    ("begin;", "SUCCESS"),
    ("insert into student values (2, 'xiaoming', 99.0);", "SUCCESS"),
    ("delete from student where id = 2;", "SUCCESS"),
    ("commit;", "SUCCESS"),
    ("select * from student;", "1|xiaohong|90"),
]

TOPIC7_ABORT_TEST = [
    ("create table student (id int, name char(8), score float);", "SUCCESS"),
    ("insert into student values (1, 'xiaohong', 90.0);", "SUCCESS"),
    ("begin;", "SUCCESS"),
    ("insert into student values (2, 'xiaoming', 99.0);", "SUCCESS"),
    ("delete from student where id = 2;", "SUCCESS"),
    ("abort;", "SUCCESS"),
    ("select * from student;", "1|xiaohong|90"),
]

TOPIC7_ABORT_TEST2 = [
    ("create table t (id int, val int);", "SUCCESS"),
    ("insert into t values (1, 100);", "SUCCESS"),
    ("begin;", "SUCCESS"),
    ("insert into t values (2, 200);", "SUCCESS"),
    ("update t set val = 999 where id = 1;", "SUCCESS"),
    ("abort;", "SUCCESS"),
    ("select * from t;", "1|100"),
]

TOPIC7_COMMIT_INDEX_TEST = [
    ("create table student_idx (id int, name char(8), score float);", "SUCCESS"),
    ("create index student_idx(id);", "SUCCESS"),
    ("insert into student_idx values (1, 'xiaohong', 90.0);", "SUCCESS"),
    ("begin;", "SUCCESS"),
    ("insert into student_idx values (2, 'xiaoming', 99.0);", "SUCCESS"),
    ("commit;", "SUCCESS"),
    ("select * from student_idx where id = 1;", "1|xiaohong|90"),
    ("select * from student_idx where id = 2;", "2|xiaoming|99"),
    ("insert into student_idx values (1, 'dup', 50.0);", "failure"),
]

TOPIC7_ABORT_INDEX_TEST = [
    ("create table student_idx2 (id int, name char(8), score float);", "SUCCESS"),
    ("create index student_idx2(id);", "SUCCESS"),
    ("insert into student_idx2 values (1, 'xiaohong', 90.0);", "SUCCESS"),
    ("begin;", "SUCCESS"),
    ("insert into student_idx2 values (2, 'xiaoming', 99.0);", "SUCCESS"),
    ("delete from student_idx2 where id = 1;", "SUCCESS"),
    ("update student_idx2 set name = 'xiaoqin' where id = 2;", "SUCCESS"),
    ("abort;", "SUCCESS"),
    ("select * from student_idx2;", "1|xiaohong|90"),
    ("insert into student_idx2 values (2, 'newstud', 85.0);", "SUCCESS"),
    ("select * from student_idx2 where id = 2;", "2|newstud|85"),
]


def run(tester: RMDBTester):
    """执行题目七全部测试点"""
    tester.run_tests("题目七 测试点1: 事务提交(不含索引)", TOPIC7_COMMIT_TEST)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目七 测试点2: 事务回滚(不含索引)", TOPIC7_ABORT_TEST)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目七 测试点2-扩展: 回滚 INSERT+UPDATE", TOPIC7_ABORT_TEST2)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目七 测试点3: 事务提交(含索引)", TOPIC7_COMMIT_INDEX_TEST)
    tester.run_tests("题目七 测试点4: 事务回滚(含索引)", TOPIC7_ABORT_INDEX_TEST)
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
