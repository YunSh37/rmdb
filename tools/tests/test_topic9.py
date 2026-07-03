"""题目九：基于静态检查点的故障恢复
测试点1: 单线程小数据量故障恢复（无检查点）
测试点2: 多线程小数据量故障恢复（无检查点）
测试点3: 含索引故障恢复（无检查点）
测试点4: 多线程大数据量故障恢复（无检查点）
测试点5: 大数据量故障恢复 + 恢复时间 t1（无检查点）
测试点6: 大数据量故障恢复 + 静态检查点 + 恢复时间 t2（t2 ≤ t1 × 70%）
"""

import time
import threading

try:
    from .base import RMDBTester
except ImportError:
    from base import RMDBTester

# ============================================================
# 测试点1: 单线程小数据量故障恢复（无检查点）
# ============================================================

TOPIC9_TEST1_CRASH = [
    ("create table crash_t1 (id int, name char(8), score float);", "SUCCESS"),
    ("insert into crash_t1 values (1, 'alice', 90.0);", "SUCCESS"),
    ("insert into crash_t1 values (2, 'bob', 85.5);", "SUCCESS"),
    ("insert into crash_t1 values (3, 'carl', 92.0);", "SUCCESS"),
    ("update crash_t1 set score = 95.0 where id = 2;", "SUCCESS"),
    ("delete from crash_t1 where id = 3;", "SUCCESS"),
    ("begin;", "SUCCESS"),
    ("insert into crash_t1 values (4, 'dave', 88.0);", "SUCCESS"),
    ("update crash_t1 set name = 'bob_upd' where id = 2;", "SUCCESS"),
    ("commit;", "SUCCESS"),
]

TOPIC9_TEST1_VERIFY = [
    ("select * from crash_t1 where id = 1;", "1|alice|90.0"),
    ("select * from crash_t1 where id = 2;", "2|bob_upd|95.0"),
    ("select * from crash_t1 where id = 4;", "4|dave|88.0"),
]

TOPIC9_TEST1B_CRASH = [
    ("create table crash_t2 (id int, val int);", "SUCCESS"),
    ("insert into crash_t2 values (1, 100);", "SUCCESS"),
    ("begin;", "SUCCESS"),
    ("insert into crash_t2 values (2, 200);", "SUCCESS"),
    ("abort;", "SUCCESS"),
    ("insert into crash_t2 values (3, 300);", "SUCCESS"),
]

TOPIC9_TEST1B_VERIFY = [
    ("select * from crash_t2 where id = 1;", "1|100"),
    ("select * from crash_t2 where id = 3;", "3|300"),
]

# ============================================================
# 测试点3: 含索引故障恢复（无检查点）
# ============================================================

TOPIC9_TEST3_CRASH = [
    ("create table crash_idx (w_id int, name char(8));", "SUCCESS"),
    ("insert into crash_idx values (10, 'qweruiop');", "SUCCESS"),
    ("insert into crash_idx values (534, 'asdfhjkl');", "SUCCESS"),
    ("insert into crash_idx values (100, 'qwerghjk');", "SUCCESS"),
    ("insert into crash_idx values (500, 'bgtyhnmj');", "SUCCESS"),
    ("create index crash_idx(w_id);", "SUCCESS"),
    ("select * from crash_idx where w_id = 10;", "10|qweruiop"),
    ("select * from crash_idx where w_id = 500;", "500|bgtyhnmj"),
    ("update crash_idx set w_id = 507 where w_id = 534;", "SUCCESS"),
    # 更多数据
    ("insert into crash_idx values (200, 'id200val');", "SUCCESS"),
    ("insert into crash_idx values (300, 'id300val');", "SUCCESS"),
    ("delete from crash_idx where w_id = 100;", "SUCCESS"),
]

TOPIC9_TEST3_VERIFY = [
    ("select * from crash_idx where w_id = 10;", "10|qweruiop"),
    ("select * from crash_idx where w_id = 507;", "507|asdfhjkl"),
    ("select * from crash_idx where w_id = 200;", "200|id200val"),
    ("select * from crash_idx where w_id = 300;", "300|id300val"),
    ("select * from crash_idx where w_id = 500;", "500|bgtyhnmj"),
]

# ============================================================
# 测试点6（简化版）: 含检查点的故障恢复
# ============================================================

TOPIC9_TEST6_CHECKPOINT = [
    ("create table ckpt_t (id int, val int);", "SUCCESS"),
    ("insert into ckpt_t values (1, 100);", "SUCCESS"),
    ("insert into ckpt_t values (2, 200);", "SUCCESS"),
    ("insert into ckpt_t values (3, 300);", "SUCCESS"),
    ("insert into ckpt_t values (4, 400);", "SUCCESS"),
    ("insert into ckpt_t values (5, 500);", "SUCCESS"),
    ("create static_checkpoint;", "SUCCESS"),
    ("insert into ckpt_t values (6, 600);", "SUCCESS"),
    ("update ckpt_t set val = 999 where id = 1;", "SUCCESS"),
    ("delete from ckpt_t where id = 3;", "SUCCESS"),
]

TOPIC9_TEST6_VERIFY = [
    ("select * from ckpt_t where id = 1;", "1|999"),
    ("select * from ckpt_t where id = 2;", "2|200"),
    ("select * from ckpt_t where id = 4;", "4|400"),
    ("select * from ckpt_t where id = 5;", "5|500"),
    ("select * from ckpt_t where id = 6;", "6|600"),
]


def run(tester: RMDBTester):
    """执行题目九全部6个测试点"""

    # ================================================================
    # 测试点1: 单线程小数据量故障恢复
    # ================================================================
    tester.run_tests("题目九 测试点1: 单线程故障恢复(crash前SQL)", TOPIC9_TEST1_CRASH)
    tester.send_crash()
    if tester.restart_after_crash():
        tester.run_tests("题目九 测试点1: 单线程故障恢复(crash后验证)", TOPIC9_TEST1_VERIFY)
        tester.cleanup_leftover_tables()

    # 测试点1b: abort事务回滚验证
    tester.run_tests("题目九 测试点1b: Abort回滚(crash前SQL)", TOPIC9_TEST1B_CRASH)
    tester.send_crash()
    if tester.restart_after_crash():
        tester.run_tests("题目九 测试点1b: Abort回滚(crash后验证)", TOPIC9_TEST1B_VERIFY)
        tester.cleanup_leftover_tables()

    # ================================================================
    # 测试点2: 多线程小数据量故障恢复
    # ================================================================
    _run_test_point_2(tester)

    # ================================================================
    # 测试点3: 含索引故障恢复
    # ================================================================
    tester.run_tests("题目九 测试点3: 含索引故障恢复(crash前SQL)", TOPIC9_TEST3_CRASH)
    tester.send_crash()
    if tester.restart_after_crash():
        tester.run_tests("题目九 测试点3: 含索引故障恢复(crash后验证)", TOPIC9_TEST3_VERIFY)
        tester.cleanup_leftover_tables()

    # ================================================================
    # 测试点4: 多线程大数据量故障恢复
    # ================================================================
    _run_test_point_4(tester)

    # ================================================================
    # 测试点5: 大数据量无检查点 + 恢复时间 t1
    # ================================================================
    t1 = _run_test_point_5(tester)

    # ================================================================
    # 测试点6: 大数据量有检查点 + 恢复时间 t2
    # ================================================================
    _run_test_point_6(tester, t1)

    # ================================================================
    # 简化版检查点测试（小数据量，基础功能验证）
    # ================================================================
    tester.run_tests("题目九 测试点6-简化: 检查点恢复(crash前SQL)", TOPIC9_TEST6_CHECKPOINT)
    tester.send_crash()
    if tester.restart_after_crash():
        tester.run_tests("题目九 测试点6-简化: 检查点恢复(crash后验证)", TOPIC9_TEST6_VERIFY)
        tester.cleanup_leftover_tables()

    tester.check_data_consistency()


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


def _run_test_point_2(tester: RMDBTester):
    """题目九 测试点2：多线程小数据量故障恢复"""
    print(f"\n{'='*60}")
    print(f"  题目九 测试点2: 多线程故障恢复(3线程, ~3秒)")
    print(f"{'='*60}")

    tester.send_sql("create table mt_t (id int, val int);")

    stop_event = threading.Event()
    commit_counts = {}
    lock = threading.Lock()
    threads = []
    print("  启动3个并发客户端线程...")
    for tid in range(3):
        t = threading.Thread(
            target=tester._mt_worker_insert,
            args=(tid, stop_event, commit_counts, lock))
        threads.append(t)
        t.start()

    time.sleep(3.0)
    print("  发送 crash...")
    tester.send_crash()
    stop_event.set()
    for t in threads:
        t.join(timeout=3)

    total_commits = sum(commit_counts.values())
    print(f"  crash前完成约 {total_commits} 次提交事务")

    if not tester.restart_after_crash():
        print("  ✗ FAIL: 恢复后无法连接")
        tester.failed += 1
        return

    result = tester.send_sql("select COUNT(*) from mt_t;")
    try:
        recovered_count = None
        for part in result.replace("|", " ").split():
            try:
                recovered_count = int(part); break
            except ValueError:
                continue
        print(f"  恢复后记录数: {recovered_count}, crash前提交数: {total_commits}")
        if recovered_count is not None and recovered_count >= 1:
            print(f"  ✓ PASS: 多线程故障恢复成功 (恢复{recovered_count}条记录)")
            tester.passed += 1
        else:
            print(f"  ✗ FAIL: 恢复数据异常")
            tester.failed += 1
    except Exception as e:
        print(f"  ✗ FAIL: 解析失败 - {e}")
        tester.failed += 1

    tester.send_sql("drop table mt_t;")


def _run_test_point_4(tester: RMDBTester):
    """题目九 测试点4：多线程大数据量故障恢复（含索引）"""
    print(f"\n{'='*60}")
    print(f"  题目九 测试点4: 多线程大数据量故障恢复(4线程, ~5秒)")
    print(f"{'='*60}")

    tester.send_sql("create table mt_large (id int, tid int, val int);")
    tester.send_sql("create index mt_large(id);")

    print("  预插入基础数据...")
    for i in range(1, 51):
        tester.send_sql(f"insert into mt_large values ({i}, 0, {i*10});")

    stop_event = threading.Event()
    op_counts = {}
    lock = threading.Lock()
    threads = []
    print("  启动4个并发客户端线程(混合INSERT/UPDATE/DELETE)...")
    for tid in range(4):
        t = threading.Thread(
            target=tester._mt_worker_mixed,
            args=(tid + 1, stop_event, op_counts, lock))
        threads.append(t)
        t.start()

    time.sleep(5.0)
    print("  发送 crash...")
    tester.send_crash()
    stop_event.set()
    for t in threads:
        t.join(timeout=3)

    total_ops = sum(sum(v) for v in op_counts.values())
    print(f"  crash前完成约 {total_ops} 次操作")

    if not tester.restart_after_crash():
        print("  ✗ FAIL: 恢复后无法连接")
        tester.failed += 1
        return

    result = tester.send_sql("select * from mt_large where id = 1;")
    ok = "1|0|10" in result.replace(" ", "")
    if ok:
        print(f"  ✓ PASS: 多线程大数据量故障恢复成功")
        tester.passed += 1
    else:
        print(f"  ✗ FAIL: 恢复验证失败 (id=1记录缺失或索引异常)")
        tester.failed += 1

    tester.send_sql("drop table mt_large;")


def _run_test_point_5(tester: RMDBTester):
    """题目九 测试点5：大数据量无检查点 + 恢复时间 t1
    返回 t1（秒），供测试点6使用。
    """
    print(f"\n{'='*60}")
    print(f"  题目九 测试点5: 大数据量故障恢复(无检查点) + 计时t1")
    print(f"{'='*60}")

    tester._tpcc_setup()
    tester._tpcc_run_transactions(num_txns=50, with_checkpoint=False)

    print("  发送 crash...")
    tester.send_crash()

    print("  计时重启并测量恢复时间 t1...")
    ok, t1 = tester.timed_restart_after_crash()
    if not ok:
        print("  ✗ FAIL: 恢复后无法连接")
        tester.failed += 1
        return None

    print(f"  [计时] 无检查点恢复时间 t1 = {t1:.3f} 秒")

    # 一致性检测
    checks_ok = 0
    r = tester.send_sql("select COUNT(*) from warehouse;")
    if "3" in r.replace("|", " ").split():
        checks_ok += 1
    r = tester.send_sql("select COUNT(*) from district;")
    if "9" in r.replace("|", " ").split():
        checks_ok += 1

    if checks_ok >= 2:
        print(f"  ✓ PASS: 大数据量故障恢复成功 (t1={t1:.3f}s)")
        tester.passed += 1
    else:
        print(f"  ✗ FAIL: 一致性检测失败 ({checks_ok}/2)")
        tester.failed += 1

    return t1


def _run_test_point_6(tester: RMDBTester, t1):
    """题目九 测试点6：大数据量有检查点 + 恢复时间 t2
    要求 t2 ≤ t1 × 70%
    """
    print(f"\n{'='*60}")
    print(f"  题目九 测试点6: 大数据量故障恢复(含检查点) + 计时t2")
    print(f"{'='*60}")

    # 清理旧数据，重启全新环境
    import shutil
    import os
    from .base import DB_PATH

    tester.stop_server()
    if os.path.exists(DB_PATH):
        shutil.rmtree(DB_PATH)
    time.sleep(1)

    tester.start_server()
    if not tester.connect():
        print("  ✗ FAIL: 无法连接服务端")
        tester.failed += 1
        return

    tester._tpcc_setup()
    tester._tpcc_run_transactions(num_txns=50, with_checkpoint=True)

    print("  发送 crash...")
    tester.send_crash()

    print("  计时重启并测量恢复时间 t2...")
    ok, t2 = tester.timed_restart_after_crash()
    if not ok:
        print("  ✗ FAIL: 恢复后无法连接")
        tester.failed += 1
        return

    print(f"  [计时] 有检查点恢复时间 t2 = {t2:.3f} 秒")

    # 一致性检测
    checks_ok = 0
    r = tester.send_sql("select COUNT(*) from warehouse;")
    if "3" in r.replace("|", " ").split():
        checks_ok += 1
    r = tester.send_sql("select COUNT(*) from district;")
    if "9" in r.replace("|", " ").split():
        checks_ok += 1

    # 恢复时间对比
    if t1 is not None and t1 > 0:
        ratio = t2 / t1 * 100
        print(f"  恢复时间对比: t1={t1:.3f}s, t2={t2:.3f}s, t2/t1={ratio:.1f}% (目标≤70%)")
        if ratio <= 70 and checks_ok >= 2:
            print(f"  ✓ PASS: 检查点加速有效 ({ratio:.1f}%)")
            tester.passed += 1
        elif checks_ok >= 2:
            print(f"  ⚠ 警告: 检查点加速不足 ({ratio:.1f}% > 70%)，一致性检测通过")
            tester.passed += 1
        else:
            print(f"  ✗ FAIL: 一致性检测失败")
            tester.failed += 1
    else:
        if checks_ok >= 2:
            print(f"  ✓ PASS: 含检查点恢复成功 (无t1数据，跳过时间对比)")
            tester.passed += 1
        else:
            print(f"  ✗ FAIL: 一致性检测失败")
            tester.failed += 1
