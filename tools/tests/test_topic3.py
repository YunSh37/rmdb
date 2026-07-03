"""题目三：唯一索引
测试点1: 创建/删除/展示索引
测试点2: 索引查询
测试点3: 索引维护（唯一性约束）
测试点4: 单列索引性能验证（500条×500次查询）
测试点5: 多列索引性能验证（500条×500次查询）
"""

try:
    from .base import RMDBTester
except ImportError:
    from base import RMDBTester

# ============================================================
# 测试数据
# ============================================================

TOPIC3_TEST1 = [
    ("create table warehouse (id int, name char(8));", "SUCCESS"),
    ("create index warehouse (id);", "SUCCESS"),
    ("show index from warehouse;", "warehouse|unique|(id)"),
    ("create index warehouse (id,name);", "SUCCESS"),
    ("show index from warehouse;", "warehouse|unique|(id)"),
    ("show index from warehouse;", "(id,name)"),
    ("drop index warehouse (id);", "SUCCESS"),
    ("drop index warehouse (id,name);", "SUCCESS"),
    ("show index from warehouse;", ""),
]

TOPIC3_TEST2 = [
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
    ("create table warehouse (w_id int, name char(8));", "SUCCESS"),
    ("insert into warehouse values (10 , 'qweruiop');", "SUCCESS"),
    ("insert into warehouse values (534, 'asdfhjkl');", "SUCCESS"),
    ("select * from warehouse where w_id = 10;", "10|qweruiop"),
    ("select * from warehouse where w_id < 534 and w_id > 100;", ""),
    ("create index warehouse(w_id);", "SUCCESS"),
    ("insert into warehouse values (500, 'lastdanc');", "SUCCESS"),
    ("insert into warehouse values (10, 'uiopqwer');", "failure"),
    ("update warehouse set w_id = 507 where w_id = 534;", "SUCCESS"),
    ("select * from warehouse where w_id = 10;", "10|qweruiop"),
    ("select * from warehouse where w_id < 534 and w_id > 100;", "500|lastdanc"),
    ("select * from warehouse where w_id < 534 and w_id > 100;", "507|asdfhjkl"),
    ("drop index warehouse(w_id);", "SUCCESS"),
    ("create index warehouse(w_id,name);", "SUCCESS"),
    ("insert into warehouse values(10,'qqqqoooo');", "SUCCESS"),
    ("insert into warehouse values(500,'lastdanc');", "failure"),
]


def run(tester: RMDBTester):
    """执行题目三全部测试点"""
    tester.run_tests("题目三 测试点1: 创建/删除/展示索引", TOPIC3_TEST1)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目三 测试点2: 索引查询", TOPIC3_TEST2)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目三 测试点3: 索引维护（唯一性约束）", TOPIC3_TEST3)
    tester.cleanup_leftover_tables()

    # 测试点4: 单列索引性能验证
    tester.test_topic3_index_perf(is_multi_col=False)

    # 测试点5: 多列索引性能验证
    tester.test_topic3_index_perf(is_multi_col=True)


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
