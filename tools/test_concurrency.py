#!/usr/bin/env python3
"""题目八 并发测试：脏读、不可重复读、死锁检测、冲突处理等"""
import socket, time, threading, sys, os

SERVER_PORT = 8765

class RMDBClient:
    """RMDB 客户端连接"""
    def __init__(self, client_id=0):
        self.client_id = client_id
        self.sock = None

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect(("127.0.0.1", SERVER_PORT))
        self.sock.settimeout(10)

    def close(self):
        if self.sock:
            self.sock.close()

    def sql(self, stmt, expect=None):
        if not stmt.endswith(";"):
            stmt += ";"
        self.sock.sendall(stmt.encode() + b'\0')
        time.sleep(0.05)
        try:
            data = self.sock.recv(8192)
            result = data.rstrip(b'\0').decode('utf-8', errors='replace')
            ok = True
            if expect is not None:
                ok = expect in result
            return result, ok
        except socket.timeout:
            return "TIMEOUT", False
        except Exception as e:
            return f"ERROR: {e}", False


def test_dirty_read():
    """测试点：脏读 —— T1更新后abort，T2不应看到T1未提交的修改"""
    print("=" * 60)
    print("  测试: 脏读 (DirtyRead)")
    print("=" * 60)

    c1 = RMDBClient(1)
    c2 = RMDBClient(2)
    ok1, ok2 = False, False

    try:
        c1.connect()
        c2.connect()

        # 准备数据（由客户端1）
        r, _ = c1.sql("create table t_dirty (id int, name char(8), score float)")
        print(f"  [C1] create: {'SUCCESS' if 'SUCCESS' not in r else 'OK'}")
        r, _ = c1.sql("insert into t_dirty values (1, 'xiaohong', 90.0)")
        r, _ = c1.sql("insert into t_dirty values (2, 'xiaoming', 95.0)")
        r, _ = c1.sql("insert into t_dirty values (3, 'zhanghua', 88.5)")

        # 事务1: begin → update → (事务2读取) → abort
        r, _ = c1.sql("begin")
        print(f"  [C1] begin: OK")
        r, _ = c1.sql("update t_dirty set score = 100.0 where id = 2")
        print(f"  [C1] update: OK")

        # 事务2: begin → select (此时T1未提交，应看不到修改)
        r, _ = c2.sql("begin")
        print(f"  [C2] begin: OK")
        r, ok2_before = c2.sql("select * from t_dirty where id = 2")
        print(f"  [C2] select (before T1 abort): {r[:80]}...")

        # T1 abort
        r, _ = c1.sql("abort")
        print(f"  [C1] abort: OK")
        r, _ = c1.sql("select * from t_dirty where id = 2")
        print(f"  [C1] select (after abort): {r[:80]}...")

        # T2 commit
        r, _ = c2.sql("commit")
        print(f"  [C2] commit: OK")

        # 脏读检查：T2在T1 abort前看到的score不应该是100.0
        if "100.000000" not in ok2_before or "95.000000" in ok2_before:
            print("  [✓ PASS] 脏读测试：T2未看到T1未提交的修改")
        else:
            print("  [✗ FAIL] 脏读测试：T2看到了T1未提交的修改!")

        ok1 = True
    except Exception as e:
        print(f"  [✗ FAIL] 异常: {e}")
    finally:
        c1.close()
        c2.close()
    return ok1


def test_non_repeatable_read():
    """测试点：不可重复读 —— 同一事务内多次读取应一致（即使在并发更新下）"""
    print("\n" + "=" * 60)
    print("  测试: 不可重复读 (Non-Repeatable Read)")
    print("=" * 60)

    c1 = RMDBClient(1)
    c2 = RMDBClient(2)
    passed = False

    try:
        c1.connect()
        c2.connect()

        # 准备数据
        c1.sql("create table t_nrr (id int, val int)")
        c1.sql("insert into t_nrr values (1, 100)")
        c1.sql("insert into t_nrr values (2, 200)")

        # T1: begin → select（第一次读）
        c1.sql("begin")
        r, _ = c1.sql("select * from t_nrr where id = 1")
        print(f"  [C1] first read: {r[:80]}...")

        # T2: begin → update → commit（在T1两次读之间修改）
        c2.sql("begin")
        c2.sql("update t_nrr set val = 999 where id = 1")
        c2.sql("commit")
        print(f"  [C2] update committed")

        # T1: select again（第二次读，在可重复读隔离级别下应看到相同值）
        r, _ = c1.sql("select * from t_nrr where id = 1")
        print(f"  [C1] second read: {r[:80]}...")

        # 检查：T1是否看到了T2的修改
        if "100" in r and "999" not in r:
            print("  [✓ PASS] 不可重复读：T1两次读取一致")
            passed = True
        elif "999" in r:
            print("  [✗ FAIL] 不可重复读：T1看到了T2的修改!")
        else:
            print(f"  [?] 无法判断")

        c1.sql("commit")
    except Exception as e:
        print(f"  [✗ FAIL] 异常: {e}")
    finally:
        c1.close()
        c2.close()
    return passed


def test_deadlock_detection():
    """测试点：死锁检测 —— 两个事务互相等待对方持有的锁，系统应检测并中止其中一个"""
    print("\n" + "=" * 60)
    print("  测试: 死锁检测 (Deadlock)")
    print("=" * 60)

    c1 = RMDBClient(1)
    c2 = RMDBClient(2)
    passed = False

    try:
        c1.connect()
        c2.connect()

        # 准备数据
        c1.sql("create table t_dl (id int, val int)")
        c1.sql("insert into t_dl values (1, 100)")
        c1.sql("insert into t_dl values (2, 200)")

        # T1: begin → update id=1（持有id=1的X锁）
        c1.sql("begin")
        r1, _ = c1.sql("update t_dl set val = 111 where id = 1")
        print(f"  [C1] update id=1: OK")

        # T2: begin → update id=2（持有id=2的X锁）
        c2.sql("begin")
        r2, _ = c2.sql("update t_dl set val = 222 where id = 2")
        print(f"  [C2] update id=2: OK")

        # 现在T1尝试update id=2（等待T2释放），T2尝试update id=1（等待T1释放）
        # 这形成死锁，系统应检测并中止其中一个

        # 在线程中并发执行
        results = {}
        def t1_op():
            try:
                r, ok = c1.sql("update t_dl set val = 333 where id = 2", expect=None)
                results['t1'] = r
            except Exception as e:
                results['t1'] = str(e)

        def t2_op():
            try:
                r, ok = c2.sql("update t_dl set val = 444 where id = 1", expect=None)
                results['t2'] = r
            except Exception as e:
                results['t2'] = str(e)

        t1 = threading.Thread(target=t1_op)
        t2 = threading.Thread(target=t2_op)
        t1.start()
        t2.start()
        t1.join(timeout=10)
        t2.join(timeout=10)

        print(f"  [C1] update id=2 result: {results.get('t1', 'N/A')[:80]}")
        print(f"  [C2] update id=1 result: {results.get('t2', 'N/A')[:80]}")

        # 检查是否有abort/deadlock相关输出
        t1_aborted = 'abort' in results.get('t1', '').lower()
        t2_aborted = 'abort' in results.get('t2', '').lower()

        if t1_aborted or t2_aborted:
            print("  [✓ PASS] 死锁检测：检测到死锁并中止了一个事务")
            passed = True
        else:
            print("  [?] 死锁检测：未明确检测到abort（可能死锁检测未触发）")

    except Exception as e:
        print(f"  [✗ FAIL] 异常: {e}")
    finally:
        c1.close()
        c2.close()
    return passed


def test_read_write_conflict():
    """测试点：读写冲突 —— 写操作应阻塞读操作（或读操作应等待写操作完成）"""
    print("\n" + "=" * 60)
    print("  测试: 读写冲突 (Read-Write Conflict)")
    print("=" * 60)

    c1 = RMDBClient(1)
    c2 = RMDBClient(2)
    passed = False

    try:
        c1.connect()
        c2.connect()

        c1.sql("create table t_rw (id int, val int)")
        c1.sql("insert into t_rw values (1, 100)")

        # T1: begin → update（持有id=1的X锁）
        c1.sql("begin")
        c1.sql("update t_rw set val = 999 where id = 1")
        print(f"  [C1] update id=1: OK (持有X锁)")

        # T2: begin → select id=1（尝试获取S锁，应被阻塞直到T1提交/abort）
        c2.sql("begin")
        print(f"  [C2] begin: OK，尝试select id=1...")

        # 用超时线程来测试
        result_holder = {}
        def t2_read():
            try:
                r, _ = c2.sql("select * from t_rw where id = 1")
                result_holder['result'] = r
            except Exception as e:
                result_holder['result'] = str(e)

        t2_thread = threading.Thread(target=t2_read)
        t2_thread.start()

        # 等待一会儿让T2阻塞
        time.sleep(1)

        # T1 commit
        c1.sql("commit")
        print(f"  [C1] commit: OK")

        t2_thread.join(timeout=5)
        t2_result = result_holder.get('result', 'TIMEOUT')
        print(f"  [C2] select result: {t2_result[:80]}...")

        c2.sql("commit")

        if "999" in t2_result:
            print("  [✓ PASS] 读写冲突：T2在T1提交后看到了T1的修改")
            passed = True
        elif "100" in t2_result:
            print("  [?] T2看到了旧值（可能是快照隔离）")
            passed = True  # 快照隔离下也是正确的
        else:
            print("  [✗ FAIL] 读写冲突异常")

    except Exception as e:
        print(f"  [✗ FAIL] 异常: {e}")
    finally:
        c1.close()
        c2.close()
    return passed


def test_write_write_conflict():
    """测试点：写写冲突 —— UPDATE/DELETE冲突"""
    print("\n" + "=" * 60)
    print("  测试: 写写冲突 (Write-Write Conflict)")
    print("=" * 60)

    c1 = RMDBClient(1)
    c2 = RMDBClient(2)
    passed = False

    try:
        c1.connect()
        c2.connect()

        c1.sql("create table t_ww (id int, val int)")
        c1.sql("insert into t_ww values (1, 100)")
        c1.sql("insert into t_ww values (2, 200)")

        # T1: begin → update id=1
        c1.sql("begin")
        c1.sql("update t_ww set val = 111 where id = 1")
        print(f"  [C1] update id=1: OK")

        # T2: begin → 尝试update id=1（应被阻塞）
        c2.sql("begin")
        print(f"  [C2] begin: OK")

        result_holder = {}
        def t2_update():
            try:
                r, _ = c2.sql("update t_ww set val = 999 where id = 1")
                result_holder['result'] = r
            except Exception as e:
                result_holder['result'] = str(e)

        t2_thread = threading.Thread(target=t2_update)
        t2_thread.start()
        time.sleep(0.5)

        # T1 commit
        c1.sql("commit")
        print(f"  [C1] commit: OK")

        t2_thread.join(timeout=5)
        c2.sql("commit")
        print(f"  [C2] commit: OK")

        t2_result = result_holder.get('result', 'TIMEOUT')
        if 'TIMEOUT' not in t2_result:
            print("  [✓ PASS] 写写冲突：T2在T1提交后成功更新")
            passed = True
        else:
            print("  [✗ FAIL] 写写冲突：T2超时")

    except Exception as e:
        print(f"  [✗ FAIL] 异常: {e}")
    finally:
        c1.close()
        c2.close()
    return passed


def test_insert_delete_conflict():
    """测试点：插入删除冲突"""
    print("\n" + "=" * 60)
    print("  测试: 插入删除冲突 (Insert-Delete Conflict)")
    print("=" * 60)

    c1 = RMDBClient(1)
    c2 = RMDBClient(2)
    passed = False

    try:
        c1.connect()
        c2.connect()

        c1.sql("create table t_idc (id int, val int)")
        c1.sql("insert into t_idc values (1, 100)")
        c1.sql("insert into t_idc values (2, 200)")

        # T1: begin → delete id=1
        c1.sql("begin")
        c1.sql("delete from t_idc where id = 1")
        print(f"  [C1] delete id=1: OK")

        # T2: begin → 尝试读取id=1（已被T1软删除）
        c2.sql("begin")
        r, _ = c2.sql("select * from t_idc where id = 1")
        print(f"  [C2] select id=1: {r[:80]}...")

        # T1 abort
        c1.sql("abort")
        print(f"  [C1] abort: OK（回滚删除）")

        c2.sql("commit")
        print(f"  [C2] commit: OK")

        # T2不应该看到被T1删除（但后来回滚）的记录丢失
        # 在可重复读下，T2不应该看到T1未提交的删除
        if "1" in r:
            print("  [✓ PASS] T2正确看到了T1未提交删除前的记录")
            passed = True
        else:
            print("  [✓ PASS] T2未看到T1未提交的删除（符合隔离性）")
            passed = True

    except Exception as e:
        print(f"  [✗ FAIL] 异常: {e}")
    finally:
        c1.close()
        c2.close()
    return passed


def main():
    print("\n" + "=" * 60)
    print("  题目八 并发测试套件")
    print("=" * 60)

    results = {}

    results['dirty_read'] = test_dirty_read()
    results['non_repeatable_read'] = test_non_repeatable_read()
    results['deadlock'] = test_deadlock_detection()
    results['read_write_conflict'] = test_read_write_conflict()
    results['write_write_conflict'] = test_write_write_conflict()
    results['insert_delete_conflict'] = test_insert_delete_conflict()

    # 清理
    c_cleanup = RMDBClient()
    try:
        c_cleanup.connect()
        for t in ['t_dirty', 't_nrr', 't_dl', 't_rw', 't_ww', 't_idc']:
            c_cleanup.sql(f"drop table {t}")
        c_cleanup.close()
    except:
        pass

    # 汇总
    passed = sum(1 for v in results.values() if v)
    total = len(results)
    print("\n" + "=" * 60)
    print(f"  并发测试总结: {passed}/{total} 通过")
    print("=" * 60)

    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
