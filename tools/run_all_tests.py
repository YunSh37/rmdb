#!/usr/bin/env python3
"""RMDB 题目二~九 自动化测试套件
用法:
  cd /mnt/d/Python_Project/RMDB_proj/rmdb
  python3 tools/run_all_tests.py
"""

import sys
import os

# 确保 tools/ 在 Python 路径中
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from tests.base import RMDBTester
from tests import test_topic2
from tests import test_topic3
from tests import test_topic4
from tests import test_topic5
from tests import test_topic6
from tests import test_topic7
from tests import test_topic8
from tests import test_topic9


def main():
    print("=" * 60)
    print("  RMDB 题目二~九 自动化测试套件")
    print("=" * 60)

    tester = RMDBTester()
    try:
        tester.start_server()
        print("服务端已启动")

        if not tester.connect():
            print("无法连接到服务端！")
            return 1

        print("已连接到服务端\n")

        # 按题目顺序依次执行
        print("\n" + "=" * 60)
        print("  题目二：查询执行")
        print("=" * 60)
        test_topic2.run(tester)

        print("\n" + "=" * 60)
        print("  题目三：唯一索引")
        print("=" * 60)
        test_topic3.run(tester)

        print("\n" + "=" * 60)
        print("  题目四：选择运算下推与投影下推")
        print("=" * 60)
        test_topic4.run(tester)

        print("\n" + "=" * 60)
        print("  题目五：聚合函数与分组统计")
        print("=" * 60)
        test_topic5.run(tester)

        print("\n" + "=" * 60)
        print("  题目六：半连接 Semi Join")
        print("=" * 60)
        test_topic6.run(tester)

        print("\n" + "=" * 60)
        print("  题目七：事务控制语句")
        print("=" * 60)
        test_topic7.run(tester)

        print("\n" + "=" * 60)
        print("  题目八：多版本并发控制（MVCC）")
        print("=" * 60)
        test_topic8.run(tester)

        print("\n" + "=" * 60)
        print("  题目九：基于静态检查点的故障恢复")
        print("=" * 60)
        test_topic9.run(tester)

    finally:
        tester.stop_server()

    # 汇总
    total = tester.passed + tester.failed
    print(f"\n{'='*60}")
    print(f"  测试总结: {tester.passed}/{total} 通过, {tester.failed} 失败")
    print(f"{'='*60}")

    return 0 if tester.failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
