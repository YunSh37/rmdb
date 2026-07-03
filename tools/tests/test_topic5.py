"""题目五：聚合函数与分组统计
测试点1: 单独使用聚合函数 (MAX, MIN, COUNT, SUM)
测试点2: 聚合函数加分组统计 (GROUP BY, HAVING)
测试点3: 健壮性测试
测试点4: ORDER BY 语句测试
"""

try:
    from .base import RMDBTester
except ImportError:
    from base import RMDBTester

# ============================================================
# 测试数据
# ============================================================

TOPIC5_TEST1 = [
    ("create table grade (course char(20),id int,score float);", "SUCCESS"),
    ("insert into grade values('DataStructure',1,95);", "SUCCESS"),
    ("insert into grade values('DataStructure',2,93.5);", "SUCCESS"),
    ("insert into grade values('DataStructure',4,87);", "SUCCESS"),
    ("insert into grade values('DataStructure',3,85);", "SUCCESS"),
    ("insert into grade values('DB',1,94);", "SUCCESS"),
    ("insert into grade values('DB',2,74.5);", "SUCCESS"),
    ("insert into grade values('DB',4,83);", "SUCCESS"),
    ("insert into grade values('DB',3,87);", "SUCCESS"),
    ("select MAX(id) as max_id from grade;", "| 4 |"),
    ("select MIN(score) as min_score from grade where course = 'DB';", "| 74.500000 |"),
    ("select COUNT(course) as course_num from grade;", "| 8 |"),
    ("select COUNT(*) as row_num from grade;", "| 8 |"),
    ("select SUM(score) as sum_score from grade where id = 1;", "| 189.000000 |"),
    ("drop table grade;", "SUCCESS"),
]

TOPIC5_TEST2 = [
    ("create table grade (course char(20),id int,score float);", "SUCCESS"),
    ("insert into grade values('DataStructure',1,95);", "SUCCESS"),
    ("insert into grade values('DataStructure',2,93.5);", "SUCCESS"),
    ("insert into grade values('DataStructure',3,94.5);", "SUCCESS"),
    ("insert into grade values('ComputerNetworks',1,99);", "SUCCESS"),
    ("insert into grade values('ComputerNetworks',2,88.5);", "SUCCESS"),
    ("insert into grade values('ComputerNetworks',3,92.5);", "SUCCESS"),
    ("insert into grade values('C++',1,92);", "SUCCESS"),
    ("insert into grade values('C++',2,89);", "SUCCESS"),
    ("insert into grade values('C++',3,89.5);", "SUCCESS"),
    ("select id,MAX(score) as max_score,MIN(score) as min_score,"
     "SUM(score) as sum_score from grade group by id;", "max_score"),
    ("select id,MAX(score) as max_score from grade group by id having COUNT(*) > 3;", ""),
    ("insert into grade values ('ParallelCompute',1,100);", "SUCCESS"),
    ("select id,MAX(score) as max_score from grade group by id having COUNT(*) > 3;", "100.000000"),
    ("select id,MAX(score) as max_score,MIN(score) as min_score "
     "from grade group by id having COUNT(*) > 1 and MIN(score) > 88;", "max_score"),
    ("select course ,COUNT(*) as row_num , COUNT(id) as student_num , "
     "MAX(score) as top_score, MIN(score) as lowest_score from grade group by course;", "DataStructure"),
    ("drop table grade;", "SUCCESS"),
]

TOPIC5_TEST3 = [
    ("create table grade (course char(20),id int,score float);", "SUCCESS"),
    ("insert into grade values('DataStructure',1,95);", "SUCCESS"),
    ("insert into grade values('DataStructure',2,93.5);", "SUCCESS"),
    ("insert into grade values('DataStructure',3,94.5);", "SUCCESS"),
    ("insert into grade values('ComputerNetworks',1,99);", "SUCCESS"),
    ("insert into grade values('ComputerNetworks',2,88.5);", "SUCCESS"),
    ("insert into grade values('ComputerNetworks',3,92.5);", "SUCCESS"),
    ("select id , score from grade group by course;", "failure"),
    ("select id, MAX(score) as max_score from grade "
     "where MAX(score) > 90 group by id;", "failure"),
]

TOPIC5_TEST4 = [
    ("create table records (vendor char(5), invoice_number int, amount float);", "SUCCESS"),
    ("insert into records values('alpha', 1001, 98.0);", "SUCCESS"),
    ("insert into records values('bravo', 2002, 76.5);", "SUCCESS"),
    ("insert into records values('charl', 3003, 99.0);", "SUCCESS"),
    ("insert into records values('delta', 1001, 98.5);", "SUCCESS"),
    ("insert into records values('echoo', 4004, 88.25);", "SUCCESS"),
    ("insert into records values('foxxx', 4004, 77.0);", "SUCCESS"),
    ("insert into records values('golfy', 5005, 97.75);", "SUCCESS"),
    ("insert into records values('hotel', 5005, 86.75);", "SUCCESS"),
    ("insert into records values('indio', 6006, 76.25);", "SUCCESS"),
    ("insert into records values('julie', 3003, 88.0);", "SUCCESS"),
    ("insert into records values('karen', 5005, 89.25);", "SUCCESS"),
    ("insert into records values('lenny', 2002, 91.125);", "SUCCESS"),
    ("insert into records values('mango', 6006, 98.5);", "SUCCESS"),
    ("insert into records values('nancy', 1001, 89.75);", "SUCCESS"),
    ("insert into records values('oscar', 2002, 90.0);", "SUCCESS"),
    ("insert into records values('peter', 3003, 95.0);", "SUCCESS"),
    ("insert into records values('quack', 6006, 88.625);", "SUCCESS"),
    ("insert into records values('romeo', 4004, 92.0);", "SUCCESS"),
    ("insert into records values('sunny', 1001, 95.25);", "SUCCESS"),
    ("insert into records values('tonny', 7007, 98.125);", "SUCCESS"),
    ("insert into records values('ultra', 4004, 91.5);", "SUCCESS"),
    ("insert into records values('vivid', 7007, 98.3125);", "SUCCESS"),
    ("select * from records order by invoice_number, amount asc limit 2;", "nancy"),
]


def run(tester: RMDBTester):
    """执行题目五全部测试点"""
    tester.run_tests("题目五 测试点1: 单独使用聚合函数", TOPIC5_TEST1)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目五 测试点2: 聚合函数加分組統計", TOPIC5_TEST2)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目五 测试点3: 健壮性测试", TOPIC5_TEST3)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目五 测试点4: ORDER BY 语句测试", TOPIC5_TEST4)
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
