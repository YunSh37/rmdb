"""题目八：多版本并发控制（MVCC）
测试点1: MVCC基础 (INSERT+SELECT 可见性)
测试点2: MVCC软删除 (DELETE设置xmax)
测试点3: MVCC更新 (UPDATE更新xmin)
测试点4: 事务提交可见性
测试点5: 事务回滚恢复(含软删除)
测试点6: MVCC+唯一索引
测试点7: 多轮删除插入
测试点8: Scan基础功能
测试点9: 时间戳分配与管理
测试点10: 元组重建(版本链)
注意：并发测试(脏读/死锁等)需C++测试框架，Python脚本无法替代。
"""

try:
    from .base import RMDBTester
except ImportError:
    from base import RMDBTester

# ============================================================
# 测试数据
# ============================================================

TOPIC8_MVCC_BASIC = [
    ("create table mvcc_test (id int, name char(8), score float);", "SUCCESS"),
    ("insert into mvcc_test values (1, 'alice', 90.0);", "SUCCESS"),
    ("insert into mvcc_test values (2, 'bob', 85.0);", "SUCCESS"),
    ("select * from mvcc_test;", "1|alice|90"),
    ("select * from mvcc_test;", "2|bob|85"),
]

TOPIC8_MVCC_DELETE = [
    ("create table mvcc_del (id int, val int);", "SUCCESS"),
    ("insert into mvcc_del values (1, 100);", "SUCCESS"),
    ("insert into mvcc_del values (2, 200);", "SUCCESS"),
    ("insert into mvcc_del values (3, 300);", "SUCCESS"),
    ("delete from mvcc_del where id = 2;", "SUCCESS"),
    ("select * from mvcc_del;", "1|100"),
    ("select * from mvcc_del;", "3|300"),
    ("select * from mvcc_del where id = 2;", ""),
]

TOPIC8_MVCC_UPDATE = [
    ("create table mvcc_upd (id int, val int);", "SUCCESS"),
    ("insert into mvcc_upd values (1, 100);", "SUCCESS"),
    ("insert into mvcc_upd values (2, 200);", "SUCCESS"),
    ("update mvcc_upd set val = 999 where id = 1;", "SUCCESS"),
    ("select * from mvcc_upd where id = 1;", "1|999"),
]

TOPIC8_MVCC_TXN_COMMIT = [
    ("create table mvcc_txn (id int, val int);", "SUCCESS"),
    ("insert into mvcc_txn values (1, 10);", "SUCCESS"),
    ("begin;", "SUCCESS"),
    ("insert into mvcc_txn values (2, 20);", "SUCCESS"),
    ("commit;", "SUCCESS"),
    ("select * from mvcc_txn;", "1|10"),
    ("select * from mvcc_txn;", "2|20"),
]

TOPIC8_MVCC_TXN_ABORT = [
    ("create table mvcc_abt (id int, val int);", "SUCCESS"),
    ("insert into mvcc_abt values (1, 10);", "SUCCESS"),
    ("insert into mvcc_abt values (2, 20);", "SUCCESS"),
    ("begin;", "SUCCESS"),
    ("delete from mvcc_abt where id = 1;", "SUCCESS"),
    ("update mvcc_abt set val = 888 where id = 2;", "SUCCESS"),
    ("abort;", "SUCCESS"),
    ("select * from mvcc_abt;", "1|10"),
    ("select * from mvcc_abt;", "2|20"),
]

TOPIC8_MVCC_INDEX = [
    ("create table mvcc_idx (id int, name char(8));", "SUCCESS"),
    ("create index mvcc_idx(id);", "SUCCESS"),
    ("insert into mvcc_idx values (1, 'first');", "SUCCESS"),
    ("begin;", "SUCCESS"),
    ("insert into mvcc_idx values (2, 'second');", "SUCCESS"),
    ("commit;", "SUCCESS"),
    ("select * from mvcc_idx where id = 1;", "1|first"),
    ("select * from mvcc_idx where id = 2;", "2|second"),
    ("begin;", "SUCCESS"),
    ("delete from mvcc_idx where id = 1;", "SUCCESS"),
    ("abort;", "SUCCESS"),
    ("select * from mvcc_idx where id = 1;", "1|first"),
]

TOPIC8_MVCC_MULTI_DELETE_INSERT = [
    ("create table mvcc_multi (id int, val int);", "SUCCESS"),
    ("insert into mvcc_multi values (1, 100);", "SUCCESS"),
    ("delete from mvcc_multi where id = 1;", "SUCCESS"),
    ("insert into mvcc_multi values (2, 200);", "SUCCESS"),
    ("select * from mvcc_multi;", "2|200"),
    ("select * from mvcc_multi where id = 1;", ""),
]

TOPIC8_SCAN_TEST = [
    ("create table scan_test (id int, val int);", "SUCCESS"),
    ("insert into scan_test values (1, 10);", "SUCCESS"),
    ("insert into scan_test values (2, 20);", "SUCCESS"),
    ("insert into scan_test values (3, 30);", "SUCCESS"),
    ("select * from scan_test;", "1|10"),
    ("select * from scan_test;", "2|20"),
    ("select * from scan_test;", "3|30"),
    ("select * from scan_test where id > 1;", "2|20"),
    ("select * from scan_test where id > 1;", "3|30"),
    ("begin;", "SUCCESS"),
    ("delete from scan_test where id = 2;", "SUCCESS"),
    ("select * from scan_test;", "1|10"),
    ("select * from scan_test;", "3|30"),
    ("abort;", "SUCCESS"),
    ("select * from scan_test;", "2|20"),
]

TOPIC8_TIMESTAMP_TEST = [
    ("create table ts_test (id int, val int);", "SUCCESS"),
    ("insert into ts_test values (1, 100);", "SUCCESS"),
    ("begin;", "SUCCESS"),
    ("insert into ts_test values (2, 200);", "SUCCESS"),
    ("commit;", "SUCCESS"),
    ("select * from ts_test;", "1|100"),
    ("select * from ts_test;", "2|200"),
    ("begin;", "SUCCESS"),
    ("insert into ts_test values (3, 300);", "SUCCESS"),
    ("select * from ts_test;", "3|300"),
    ("commit;", "SUCCESS"),
    ("select * from ts_test;", "1|100"),
    ("select * from ts_test;", "2|200"),
    ("select * from ts_test;", "3|300"),
]

TOPIC8_TUPLE_RECONSTRUCT_TEST = [
    ("create table tuple_test (id int, val int);", "SUCCESS"),
    ("insert into tuple_test values (1, 100);", "SUCCESS"),
    ("begin;", "SUCCESS"),
    ("update tuple_test set val = 200 where id = 1;", "SUCCESS"),
    ("select * from tuple_test where id = 1;", "1|200"),
    ("commit;", "SUCCESS"),
    ("select * from tuple_test where id = 1;", "1|200"),
    ("begin;", "SUCCESS"),
    ("update tuple_test set val = 300 where id = 1;", "SUCCESS"),
    ("abort;", "SUCCESS"),
    ("select * from tuple_test where id = 1;", "1|200"),
    ("begin;", "SUCCESS"),
    ("update tuple_test set val = 400 where id = 1;", "SUCCESS"),
    ("update tuple_test set val = 500 where id = 1;", "SUCCESS"),
    ("select * from tuple_test where id = 1;", "1|500"),
    ("commit;", "SUCCESS"),
    ("select * from tuple_test where id = 1;", "1|500"),
]


def run(tester: RMDBTester):
    """执行题目八全部测试点"""
    tester.run_tests("题目八 测试点1: MVCC基础(INSERT+SELECT)", TOPIC8_MVCC_BASIC)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目八 测试点2: MVCC软删除", TOPIC8_MVCC_DELETE)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目八 测试点3: MVCC更新", TOPIC8_MVCC_UPDATE)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目八 测试点4: 事务提交可见性", TOPIC8_MVCC_TXN_COMMIT)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目八 测试点5: 事务回滚恢复(含软删除)", TOPIC8_MVCC_TXN_ABORT)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目八 测试点6: MVCC+索引", TOPIC8_MVCC_INDEX)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目八 测试点7: 多轮删除插入", TOPIC8_MVCC_MULTI_DELETE_INSERT)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目八 测试点8: Scan基础", TOPIC8_SCAN_TEST)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目八 测试点9: 时间戳管理", TOPIC8_TIMESTAMP_TEST)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目八 测试点10: 元组重建", TOPIC8_TUPLE_RECONSTRUCT_TEST)
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
