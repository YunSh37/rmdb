"""题目六：半连接 Semi Join
测试点1: 基本的 Semi Join（查询有员工的部门）
测试点2: Semi Join 结果不受右表重复匹配影响
测试点3: Semi Join 右表为空或无匹配
测试点4: 健壮性测试 - 选择右表列
测试点5: 健壮性测试 - 左表为空
"""

try:
    from .base import RMDBTester
except ImportError:
    from base import RMDBTester

# ============================================================
# 测试数据
# ============================================================

TOPIC6_TEST1 = [
    ("create table departments (dept_id int, dept_name char(20));", "SUCCESS"),
    ("create table employees (emp_id int, emp_name char(20), dept_id int, salary int);", "SUCCESS"),
    ("insert into departments values(1, 'HR');", "SUCCESS"),
    ("insert into departments values(2, 'Engineering');", "SUCCESS"),
    ("insert into departments values(3, 'Sales');", "SUCCESS"),
    ("insert into departments values(4, 'Marketing');", "SUCCESS"),
    ("insert into employees values(101, 'Alice', 1, 70000);", "SUCCESS"),
    ("insert into employees values(102, 'Bob', 2, 80000);", "SUCCESS"),
    ("insert into employees values(103, 'Charlie', 2, 90000);", "SUCCESS"),
    ("insert into employees values(104, 'David', 1, 75000);", "SUCCESS"),
    ("select dept_id, dept_name from departments semi join employees "
     "on departments.dept_id = employees.dept_id;", "1|HR"),
    ("select dept_id, dept_name from departments semi join employees "
     "on departments.dept_id = employees.dept_id;", "2|Engineering"),
    ("select d.dept_name from departments d semi join employees e "
     "on d.dept_id = e.dept_id;", "HR"),
]

TOPIC6_TEST2 = [
    ("select dept_id from departments semi join employees "
     "on departments.dept_id = employees.dept_id order by dept_id;", "1"),
    ("select dept_id from departments semi join employees "
     "on departments.dept_id = employees.dept_id order by dept_id;", "2"),
    ("insert into employees values(105, 'Eve', 1, 80000);", "SUCCESS"),
    ("select dept_id from departments semi join employees "
     "on departments.dept_id = employees.dept_id order by dept_id;", "record"),
]

TOPIC6_TEST3 = [
    ("create table projects (proj_id int, dept_id_assigned int);", "SUCCESS"),
    ("select dept_name from departments semi join projects "
     "on departments.dept_id = projects.dept_id_assigned;", ""),
    ("insert into projects values(1001, 99);", "SUCCESS"),
    ("select dept_name from departments semi join projects "
     "on departments.dept_id = projects.dept_id_assigned;", ""),
]

TOPIC6_TEST4 = [
    ("select dept_name, emp_name from departments semi join employees "
     "on departments.dept_id = employees.dept_id;", "failure"),
    ("select emp_name from departments semi join employees "
     "on departments.dept_id = employees.dept_id;", "failure"),
    ("select dept_id from departments semi join employees "
     "on departments.dept_id = employees.dept_id order by dept_id;", "1"),
]

TOPIC6_TEST5 = [
    ("create table empty_departments (dept_id int, dept_name char(20));", "SUCCESS"),
    ("select dept_name from empty_departments semi join employees "
     "on empty_departments.dept_id = employees.dept_id;", ""),
]


def run(tester: RMDBTester):
    """执行题目六全部测试点（共享表结构，不中间清理）"""
    tester.run_tests("题目六 测试点1: 基本的 Semi Join", TOPIC6_TEST1)
    tester.run_tests("题目六 测试点2: 右表重复匹配不影响", TOPIC6_TEST2)
    tester.run_tests("题目六 测试点3: 右表为空或无匹配", TOPIC6_TEST3)
    tester.run_tests("题目六 测试点4: 健壮性-选择右表列", TOPIC6_TEST4)
    tester.run_tests("题目六 测试点5: 健壮性-左表为空", TOPIC6_TEST5)

    tester.check_data_consistency()
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
