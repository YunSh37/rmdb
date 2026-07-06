#!/usr/bin/env python3
"""题目十：基于死锁预防的可串行化隔离级别 测试脚本
测试五种数据异常的预防：脏写、脏读、丢失更新、不可重复读、幻读

用法: 先启动服务器 (./bin/rmdb test_db)，然后运行本脚本
"""

import socket
import threading
import time
import sys
import os

SERVER_HOST = '127.0.0.1'
SERVER_PORT = 8765


class RMDBClient:
    """RMDB 客户端连接（注意：服务器不在连接时发送欢迎消息）"""

    def __init__(self, client_id=0):
        self.client_id = client_id
        self.sock = None

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(10)
        self.sock.connect((SERVER_HOST, SERVER_PORT))

    def send(self, sql):
        """发送 SQL 并接收响应"""
        try:
            self.sock.sendall(sql.encode() + b'\n')
            resp = b""
            while True:
                try:
                    chunk = self.sock.recv(4096)
                    if not chunk:
                        break
                    resp += chunk
                    if len(chunk) < 4096:
                        break
                except socket.timeout:
                    break
            return resp.decode('utf-8', errors='replace')
        except Exception as e:
            return f"ERROR: {e}"

    def close(self):
        try:
            self.sock.close()
        except:
            pass


def setup_database(client):
    """创建测试表和数据"""
    client.send("create table t_test (id int, name char(16), score float);")
    client.send("create table t_phantom (id int, name char(16), score float);")
    for i in range(1, 6):
        client.send(f"insert into t_test values ({i}, 'user{i}', {i * 10.0});")
        client.send(f"insert into t_phantom values ({i}, 'user{i}', {i * 10.0});")
    print("[Setup] 测试表创建完成")


def cleanup_database(client):
    """清理测试表"""
    client.send("drop table t_test;")
    client.send("drop table t_phantom;")
    print("[Cleanup] 测试表已删除")


# ===================== 脏写预防测试 =====================
def test_dirty_write():
    """脏写测试:
    T1 写入 X，在 T1 提交前 T2 也写入 X → T2 应被中止
    操作序列: T1:BEGIN, T2:BEGIN, T1:UPDATE, T2:UPDATE (同一行)
    预期: T2 收到 abort
    """
    print("\n[Test 1] 脏写预防 (Dirty Write Prevention)...")

    results = {"t1": [], "t2": []}
    # 同步：确保两个事务都 BEGIN 之后才操作
    barrier = threading.Barrier(2, timeout=10)

    def t1_worker():
        c = RMDBClient(1)
        c.connect()
        results["t1"].append(c.send("BEGIN;"))
        barrier.wait()
        time.sleep(0.1)
        results["t1"].append(c.send("update t_test set score = 99.0 where id = 1;"))
        time.sleep(0.5)
        results["t1"].append(c.send("COMMIT;"))
        results["t1"].append(c.send("select * from t_test where id = 1;"))
        c.close()

    def t2_worker():
        c = RMDBClient(2)
        c.connect()
        results["t2"].append(c.send("BEGIN;"))
        barrier.wait()
        time.sleep(0.2)  # 在 T1 UPDATE 之后
        results["t2"].append(c.send("update t_test set score = 50.0 where id = 1;"))
        c.close()

    t1 = threading.Thread(target=t1_worker)
    t2 = threading.Thread(target=t2_worker)
    t1.start()
    t2.start()
    t1.join()
    t2.join()

    print(f"  T1 结果: {[r[:60] for r in results['t1']]}")
    print(f"  T2 结果: {[r[:60] for r in results['t2']]}")

    # T2 的 UPDATE 应该返回 abort
    t2_aborted = any("abort" in r.lower() for r in results["t2"])
    # T1 最终应该看到 score = 99.0（T1 的值，不是 T2 的 50.0）
    t1_final = results["t1"][-1] if results["t1"] else ""
    t1_correct = "99.0" in t1_final or "99.000000" in t1_final

    if t2_aborted and t1_correct:
        print("  [PASS] 脏写被正确预防")
        return True
    else:
        print(f"  [FAIL] T2_aborted={t2_aborted}, T1_correct={t1_correct}")
        return False


# ===================== 脏读预防测试 =====================
def test_dirty_read():
    """脏读测试（题目要求的标准序列: t1a t2a t1b t2b t1c t1d）:
    T1 修改但未提交，T2 读取同一条 → T2 不应看到 T1 的未提交值
    预期: T2 被中止（NO-WAIT）或看到旧值
    """
    print("\n[Test 2] 脏读预防 (Dirty Read Prevention)...")

    # 恢复数据
    setup_c = RMDBClient(99)
    setup_c.connect()
    setup_c.send("update t_test set score = 95.0 where id = 2;")
    setup_c.close()

    results = {"t1": [], "t2": []}
    barrier = threading.Barrier(2, timeout=10)

    def t1_worker():
        c = RMDBClient(1)
        c.connect()
        results["t1"].append(("t1a", c.send("BEGIN;")))
        barrier.wait()
        results["t1"].append(("t1b", c.send("update t_test set score = 100.0 where id = 2;")))
        time.sleep(0.5)
        results["t1"].append(("t1c", c.send("ABORT;")))
        results["t1"].append(("t1d", c.send("select * from t_test where id = 2;")))
        c.close()

    def t2_worker():
        c = RMDBClient(2)
        c.connect()
        results["t2"].append(("t2a", c.send("BEGIN;")))
        barrier.wait()
        time.sleep(0.2)  # 等待 t1b 完成
        results["t2"].append(("t2b", c.send("select * from t_test where id = 2;")))
        results["t2"].append(("t2c", c.send("COMMIT;")))
        c.close()

    t1 = threading.Thread(target=t1_worker)
    t2 = threading.Thread(target=t2_worker)
    t1.start()
    t2.start()
    t1.join()
    t2.join()

    print(f"  T1: {[(l, r[:50]) for l, r in results['t1']]}")
    print(f"  T2: {[(l, r[:50]) for l, r in results['t2']]}")

    t2_select = ""
    for label, res in results["t2"]:
        if label == "t2b":
            t2_select = res
            break

    t2_aborted = "abort" in t2_select.lower()
    t2_sees_old = "95.0" in t2_select or "95.000000" in t2_select
    t2_sees_new = "100.0" in t2_select or "100.000000" in t2_select

    if t2_aborted or (t2_sees_old and not t2_sees_new):
        print("  [PASS] 脏读被正确预防")
        return True
    else:
        print(f"  [FAIL] aborted={t2_aborted}, old={t2_sees_old}, new={t2_sees_new}")
        return False


# ===================== 丢失更新预防测试 =====================
def test_lost_update():
    """丢失更新测试:
    T1 和 T2 同时读取 X，然后各自修改 → 后 UPDATE 的应被中止
    预期: 至少一个事务的 UPDATE 被中止
    """
    print("\n[Test 3] 丢失更新预防 (Lost Update Prevention)...")

    setup_c = RMDBClient(99)
    setup_c.connect()
    setup_c.send("update t_test set score = 90.0 where id = 3;")
    setup_c.close()

    results = {"t1": [], "t2": []}
    barrier = threading.Barrier(2, timeout=10)

    def t1_worker():
        c = RMDBClient(1)
        c.connect()
        results["t1"].append(c.send("BEGIN;"))
        results["t1"].append(c.send("select * from t_test where id = 3;"))  # S 锁
        barrier.wait()
        time.sleep(0.3)
        results["t1"].append(c.send("update t_test set score = 91.0 where id = 3;"))
        results["t1"].append(c.send("COMMIT;"))
        results["t1"].append(c.send("select * from t_test where id = 3;"))
        c.close()

    def t2_worker():
        c = RMDBClient(2)
        c.connect()
        results["t2"].append(c.send("BEGIN;"))
        results["t2"].append(c.send("select * from t_test where id = 3;"))  # S 锁
        barrier.wait()
        time.sleep(0.4)
        results["t2"].append(c.send("update t_test set score = 92.0 where id = 3;"))
        results["t2"].append(c.send("COMMIT;"))
        results["t2"].append(c.send("select * from t_test where id = 3;"))
        c.close()

    t1 = threading.Thread(target=t1_worker)
    t2 = threading.Thread(target=t2_worker)
    t1.start()
    t2.start()
    t1.join()
    t2.join()

    print(f"  T1: {[r[:60] for r in results['t1']]}")
    print(f"  T2: {[r[:60] for r in results['t2']]}")

    t1_aborted = any("abort" in r.lower() for r in results["t1"])
    t2_aborted = any("abort" in r.lower() for r in results["t2"])

    if t1_aborted or t2_aborted:
        print("  [PASS] 丢失更新被正确预防")
        return True
    else:
        print(f"  [FAIL] 两个事务都成功提交了 (应该至少一个被中止)")
        return False


# ===================== 不可重复读预防测试 =====================
def test_unrepeatable_read():
    """不可重复读测试:
    T1 读取 X，T2 修改 X 并提交，T1 再次读取 X → 应看到相同的值
    预期: T2 被中止（T1 持有的 S 锁阻止 T2 的 X 锁）
    """
    print("\n[Test 4] 不可重复读预防 (Unrepeatable Read Prevention)...")

    setup_c = RMDBClient(99)
    setup_c.connect()
    setup_c.send("update t_test set score = 80.0 where id = 4;")
    setup_c.close()

    results = {"t1": [], "t2": []}

    def t1_worker():
        c = RMDBClient(1)
        c.connect()
        results["t1"].append(c.send("BEGIN;"))
        results["t1"].append(("read1", c.send("select * from t_test where id = 4;")))
        time.sleep(0.6)  # 给 T2 时间去尝试修改
        results["t1"].append(("read2", c.send("select * from t_test where id = 4;")))
        results["t1"].append(c.send("COMMIT;"))
        c.close()

    def t2_worker():
        time.sleep(0.2)  # 等待 T1 先读取
        c = RMDBClient(2)
        c.connect()
        results["t2"].append(c.send("BEGIN;"))
        results["t2"].append(c.send("update t_test set score = 99.0 where id = 4;"))
        results["t2"].append(c.send("COMMIT;"))
        c.close()

    t1 = threading.Thread(target=t1_worker)
    t2 = threading.Thread(target=t2_worker)
    t1.start()
    t2.start()
    t1.join()
    t2.join()

    print(f"  T1: {[(r[0], r[1][:50]) if isinstance(r, tuple) else r[:50] for r in results['t1']]}")
    print(f"  T2: {[r[:60] for r in results['t2']]}")

    t2_aborted = any("abort" in r.lower() for r in results["t2"])

    if t2_aborted:
        print("  [PASS] 不可重复读被正确预防（T2 被中止）")
        return True
    else:
        read1 = ""
        read2 = ""
        for item in results["t1"]:
            if isinstance(item, tuple):
                if item[0] == "read1": read1 = item[1]
                elif item[0] == "read2": read2 = item[1]
        if "80.0" in read1 and "80.0" in read2:
            print("  [PASS] 不可重复读被正确预防（T1 两次读到相同值）")
            return True
        print(f"  [FAIL] read1和read2不同，或T2未被中止")
        return False


# ===================== 幻读预防测试 =====================
def test_phantom_read():
    """幻读测试:
    T1 范围读取，T2 插入一条新记录并提交，T1 再次范围读取 → 应看到相同行数
    预期: T2 的 INSERT 被中止
    """
    print("\n[Test 5] 幻读预防 (Phantom Read Prevention)...")

    results = {"t1": [], "t2": []}

    def t1_worker():
        c = RMDBClient(1)
        c.connect()
        results["t1"].append(c.send("BEGIN;"))
        results["t1"].append(("read1", c.send("select * from t_phantom where id >= 1 and id <= 10;")))
        time.sleep(0.6)  # 给 T2 时间插入
        results["t1"].append(("read2", c.send("select * from t_phantom where id >= 1 and id <= 10;")))
        results["t1"].append(c.send("COMMIT;"))
        c.close()

    def t2_worker():
        time.sleep(0.2)
        c = RMDBClient(2)
        c.connect()
        results["t2"].append(c.send("BEGIN;"))
        results["t2"].append(c.send("insert into t_phantom values (6, 'phantom', 60.0);"))
        results["t2"].append(c.send("COMMIT;"))
        c.close()

    t1 = threading.Thread(target=t1_worker)
    t2 = threading.Thread(target=t2_worker)
    t1.start()
    t2.start()
    t1.join()
    t2.join()

    print(f"  T1: {[(r[0], f'rows={r[1].count(chr(124))}') if isinstance(r, tuple) else r[:50] for r in results['t1']]}")
    print(f"  T2: {[r[:60] for r in results['t2']]}")

    t2_aborted = any("abort" in r.lower() for r in results["t2"])

    read1 = ""
    read2 = ""
    for item in results["t1"]:
        if isinstance(item, tuple):
            if item[0] == "read1": read1 = item[1]
            elif item[0] == "read2": read2 = item[1]

    count1 = read1.count("user") if read1 else -1
    count2 = read2.count("user") if read2 else -1

    if t2_aborted:
        print("  [PASS] 幻读被正确预防（T2 INSERT 被中止）")
        return True
    elif count1 == count2 and count1 > 0:
        print(f"  [PASS] 幻读被正确预防（两次均返回 {count1} 行）")
        return True
    else:
        print(f"  [FAIL] count1={count1}, count2={count2}")
        return False


# ===================== 主函数 =====================
def main():
    print("=" * 60)
    print("  题目十：基于死锁预防的可串行化隔离级别 测试")
    print("=" * 60)

    setup_c = RMDBClient(0)
    setup_c.connect()
    setup_c.send("drop table t_test;")
    setup_c.send("drop table t_phantom;")
    time.sleep(0.1)
    setup_database(setup_c)
    setup_c.close()

    passed = 0
    failed = 0

    for test_func in [test_dirty_write, test_dirty_read, test_lost_update,
                       test_unrepeatable_read, test_phantom_read]:
        if test_func():
            passed += 1
        else:
            failed += 1
        time.sleep(0.3)

    # 清理
    cleanup_c = RMDBClient(99)
    cleanup_c.connect()
    cleanup_database(cleanup_c)
    cleanup_c.close()

    total = passed + failed
    print(f"\n{'=' * 60}")
    print(f"  测试总结: {passed}/{total} 通过, {failed} 失败")
    print(f"{'=' * 60}")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
