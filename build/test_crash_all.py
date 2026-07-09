"""
题目十一故障恢复 综合测试套件
测试场景：基本恢复、索引恢复、大量数据恢复、检查点恢复、二次崩溃
"""
import socket, time, os, sys, subprocess, re

BUILD = '/mnt/d/Python_Project/RMDB_proj/rmdb/build'
BIN = f'{BUILD}/bin/rmdb'
HOST, PORT = '127.0.0.1', 8765

# ============== 工具函数 ==============

def has_col(resp, val):
    """检查管道符分隔的输出中是否存在某个列值"""
    return bool(re.search(r'\|\s*' + str(val) + r'\s*\|', resp))

def has_data(resp):
    """检查是否有数据行（非header）"""
    lines = resp.split('\n')
    for line in lines:
        if re.match(r'^\|\s*\d+', line):
            return True
    return False

def kill_server():
    os.system('pkill -9 rmdb 2>/dev/null; sleep 0.5')

def start_fresh(db_name):
    kill_server()
    os.system(f'rm -rf {BUILD}/{db_name} 2>/dev/null')
    proc = subprocess.Popen([BIN, db_name], cwd=BUILD,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    return proc

def restart_server(db_name):
    kill_server()
    proc = subprocess.Popen([BIN, db_name], cwd=BUILD,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2.0)
    return proc

def connect(timeout=10):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    sock.connect((HOST, PORT))
    return sock

def cmd(sock, sql, wait=0.3):
    print(f'  > {sql[:80]}{"..." if len(sql) > 80 else ""}')
    sock.send((sql + '\0').encode())
    time.sleep(wait)
    try:
        return sock.recv(8192).decode('utf-8', errors='replace')
    except socket.timeout:
        return '(timeout)'

def crash(sock):
    print('  >>> CRASH <<<')
    sock.send(b'crash\0')
    sock.close()

def V(cond, msg):
    if cond: print(f'  [PASS] {msg}')
    else: print(f'  [FAIL] {msg}')
    return cond

# ============== 测试用例 ==============

def test_basic_commit():
    print('\n' + '='*60)
    print('测试1: 已提交INSERT在crash后保留')
    print('='*60)
    proc = start_fresh('t1_db')
    s = connect()
    cmd(s, 'CREATE TABLE t1 (id INT, val INT);')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(1, 100);')
    cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(2, 200);')
    cmd(s, 'COMMIT;')
    crash(s); proc.wait(timeout=5)
    proc2 = restart_server('t1_db')
    s2 = connect()
    r = cmd(s2, 'SELECT * FROM t1;')
    ok = V(has_col(r, 1) and has_col(r, 100), '已提交 id=1 存在')
    ok &= V(has_col(r, 2) and has_col(r, 200), '已提交 id=2 存在')
    cmd(s2, 'exit'); s2.close(); proc2.terminate(); kill_server()
    return ok

def test_basic_rollback():
    print('\n' + '='*60)
    print('测试2: 未提交INSERT在crash后回滚')
    print('='*60)
    proc = start_fresh('t2_db')
    s = connect()
    cmd(s, 'CREATE TABLE t1 (id INT, val INT);')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(1, 100);')
    cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(2, 200);')
    crash(s); proc.wait(timeout=5)
    proc2 = restart_server('t2_db')
    s2 = connect()
    r = cmd(s2, 'SELECT * FROM t1;')
    ok = V(has_col(r, 1) and has_col(r, 100), '已提交 id=1 保留')
    ok &= V(not has_col(r, 2), '未提交 id=2 已回滚')
    cmd(s2, 'INSERT INTO t1 VALUES(2, 999);')
    r2 = cmd(s2, 'SELECT * FROM t1;')
    ok &= V(has_col(r2, 999), '可重新插入id=2')
    cmd(s2, 'exit'); s2.close(); proc2.terminate(); kill_server()
    return ok

def test_mixed_transactions():
    print('\n' + '='*60)
    print('测试3: 混合已提交和未提交事务')
    print('='*60)
    proc = start_fresh('t3_db')
    s = connect()
    cmd(s, 'CREATE TABLE t1 (id INT, val INT);')
    cmd(s, 'CREATE TABLE t2 (id INT, name CHAR(20));')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(1, 1);')
    cmd(s, "INSERT INTO t2 VALUES(1, 'committed');")
    cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(3, 3);')
    cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(4, 4);')
    cmd(s, "INSERT INTO t2 VALUES(4, 'uncommitted');")
    crash(s); proc.wait(timeout=5)
    proc2 = restart_server('t3_db')
    s2 = connect()
    r1 = cmd(s2, 'SELECT * FROM t1;')
    ok = V(has_col(r1, 1), '已提交t1(id=1)保留')
    ok &= V(has_col(r1, 3), '已提交t1(id=3)保留')
    ok &= V(not has_col(r1, 4), '未提交t1(id=4)已回滚')
    r2 = cmd(s2, 'SELECT * FROM t2;')
    ok &= V('committed' in r2, '已提交t2保留')
    ok &= V('uncommitted' not in r2, '未提交t2已回滚')
    cmd(s2, 'exit'); s2.close(); proc2.terminate(); kill_server()
    return ok

def test_index_recovery():
    print('\n' + '='*60)
    print('测试4: 带索引表的crash恢复')
    print('='*60)
    proc = start_fresh('t4_db')
    s = connect()
    cmd(s, 'CREATE TABLE t1 (id INT, val INT);')
    cmd(s, 'CREATE INDEX t1 (id);')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(1, 100);')
    cmd(s, 'INSERT INTO t1 VALUES(2, 200);')
    cmd(s, 'INSERT INTO t1 VALUES(3, 300);')
    cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(4, 400);')
    crash(s); proc.wait(timeout=5)
    proc2 = restart_server('t4_db')
    s2 = connect()
    r = cmd(s2, 'SELECT * FROM t1 WHERE id = 2;')
    ok = V(has_col(r, 200), '索引点查id=2返回正确')
    r = cmd(s2, 'SELECT * FROM t1 WHERE id = 4;')
    ok &= V(not has_data(r), '未提交id=4不可查询')
    cmd(s2, 'INSERT INTO t1 VALUES(4, 444);')
    r = cmd(s2, 'SELECT * FROM t1;')
    ok &= V(has_col(r, 444), '可重新插入id=4')
    r = cmd(s2, 'INSERT INTO t1 VALUES(1, 999);')
    ok &= V('failure' in r.lower() or 'duplicate' in r.lower(), '唯一索引阻止重复id=1')
    r = cmd(s2, 'SHOW INDEX FROM t1;')
    ok &= V('unique' in r.lower(), 'SHOW INDEX 显示索引存在')
    cmd(s2, 'exit'); s2.close(); proc2.terminate(); kill_server()
    return ok

def test_delete_recovery():
    print('\n' + '='*60)
    print('测试5: DELETE操作的crash恢复')
    print('='*60)
    proc = start_fresh('t5_db')
    s = connect()
    cmd(s, 'CREATE TABLE t1 (id INT, val INT);')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(1, 100);')
    cmd(s, 'INSERT INTO t1 VALUES(2, 200);')
    cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;')
    cmd(s, 'DELETE FROM t1 WHERE id = 1;')
    cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;')
    cmd(s, 'DELETE FROM t1 WHERE id = 2;')
    crash(s); proc.wait(timeout=5)
    proc2 = restart_server('t5_db')
    s2 = connect()
    r = cmd(s2, 'SELECT * FROM t1;')
    ok = V(not has_col(r, 100), '已提交DELETE id=1 仍被删除')
    ok &= V(has_col(r, 2) and has_col(r, 200), '未提交DELETE id=2 已恢复')
    cmd(s2, 'exit'); s2.close(); proc2.terminate(); kill_server()
    return ok

def test_update_recovery():
    print('\n' + '='*60)
    print('测试6: UPDATE操作的crash恢复')
    print('='*60)
    proc = start_fresh('t6_db')
    s = connect()
    cmd(s, 'CREATE TABLE t1 (id INT, val INT);')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(1, 100);')
    cmd(s, 'INSERT INTO t1 VALUES(2, 200);')
    cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;')
    cmd(s, 'UPDATE t1 SET val = 111 WHERE id = 1;')
    cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;')
    cmd(s, 'UPDATE t1 SET val = 999 WHERE id = 2;')
    crash(s); proc.wait(timeout=5)
    proc2 = restart_server('t6_db')
    s2 = connect()
    r = cmd(s2, 'SELECT * FROM t1;')
    ok = V(has_col(r, 1) and has_col(r, 111), '已提交UPDATE id=1 val=111保留')
    ok &= V(has_col(r, 2) and has_col(r, 200), '未提交UPDATE id=2 val=200已恢复')
    cmd(s2, 'exit'); s2.close(); proc2.terminate(); kill_server()
    return ok

def test_large_data():
    print('\n' + '='*60)
    print('测试7: 大量数据(100条)crash恢复')
    print('='*60)
    proc = start_fresh('t7_db')
    s = connect()
    cmd(s, 'CREATE TABLE t1 (id INT, val INT);')
    cmd(s, 'BEGIN;')
    for i in range(1, 101):
        cmd(s, f'INSERT INTO t1 VALUES({i}, {i*10});', wait=0.05)
    cmd(s, 'COMMIT;', wait=1.5)
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(200, 2000);')
    cmd(s, 'INSERT INTO t1 VALUES(201, 2010);')
    crash(s); proc.wait(timeout=5)
    proc2 = restart_server('t7_db')
    s2 = connect()
    r = cmd(s2, 'SELECT * FROM t1;', wait=2.0)
    ok = V(has_col(r, 1) and has_col(r, 10), '第1条记录存在')
    ok &= V(has_col(r, 100) and has_col(r, 1000), '第100条记录存在')
    ok &= V(not has_col(r, 2000), '未提交id=200已回滚')
    ok &= V(not has_col(r, 2010), '未提交id=201已回滚')
    cmd(s2, 'exit'); s2.close(); proc2.terminate(); kill_server()
    return ok

def test_large_index():
    print('\n' + '='*60)
    print('测试8: 大量数据(50条)+唯一索引crash恢复')
    print('='*60)
    proc = start_fresh('t8_db')
    s = connect()
    cmd(s, 'CREATE TABLE t1 (id INT, val INT);')
    cmd(s, 'CREATE INDEX t1 (id);')
    cmd(s, 'BEGIN;')
    for i in range(1, 51):
        cmd(s, f'INSERT INTO t1 VALUES({i}, {i*10});', wait=0.05)
    cmd(s, 'COMMIT;', wait=1.5)
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(100, 1000);')
    crash(s); proc.wait(timeout=5)
    proc2 = restart_server('t8_db')
    s2 = connect()
    r = cmd(s2, 'SELECT * FROM t1 WHERE id = 25;')
    ok = V(has_col(r, 250), '索引点查id=25返回正确')
    r = cmd(s2, 'SELECT * FROM t1 WHERE id = 50;')
    ok &= V(has_col(r, 500), '索引点查id=50返回正确')
    r = cmd(s2, 'SELECT * FROM t1 WHERE id = 100;')
    ok &= V(not has_data(r), '未提交id=100不可查询')
    r = cmd(s2, 'INSERT INTO t1 VALUES(1, 999);')
    ok &= V('failure' in r.lower() or 'duplicate' in r.lower(), '唯一索引阻止重复id=1')
    cmd(s2, 'exit'); s2.close(); proc2.terminate(); kill_server()
    return ok

def test_double_crash():
    print('\n' + '='*60)
    print('测试9: 二次崩溃恢复')
    print('='*60)
    proc = start_fresh('t9_db')
    s = connect()
    cmd(s, 'CREATE TABLE t1 (id INT, val INT);')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(1, 100);')
    cmd(s, 'INSERT INTO t1 VALUES(2, 200);')
    cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(3, 300);')
    crash(s); proc.wait(timeout=5)
    proc2 = restart_server('t9_db')
    s2 = connect()
    r = cmd(s2, 'SELECT * FROM t1;')
    print(f'  第一次恢复后: {r.strip()[:200]}')
    crash(s2); proc2.wait(timeout=5)
    proc3 = restart_server('t9_db')
    s3 = connect()
    r = cmd(s3, 'SELECT * FROM t1;')
    ok = V(has_col(r, 1) and has_col(r, 100), '二次崩溃后 id=1 保留')
    ok &= V(has_col(r, 2) and has_col(r, 200), '二次崩溃后 id=2 保留')
    ok &= V(not has_col(r, 3), '二次崩溃后 id=3 仍回滚')
    cmd(s3, 'exit'); s3.close(); proc3.terminate(); kill_server()
    return ok

def test_checkpoint_basic():
    print('\n' + '='*60)
    print('测试10: 带检查点的crash恢复')
    print('='*60)
    proc = start_fresh('t10_db')
    s = connect()
    cmd(s, 'CREATE TABLE t1 (id INT, val INT);')
    cmd(s, 'CREATE INDEX t1 (id);')
    cmd(s, 'BEGIN;')
    for i in range(1, 21):
        cmd(s, f'INSERT INTO t1 VALUES({i}, {i*10});', wait=0.05)
    cmd(s, 'COMMIT;', wait=1.0)
    cmd(s, 'create static_checkpoint;')
    cmd(s, 'BEGIN;')
    for i in range(21, 41):
        cmd(s, f'INSERT INTO t1 VALUES({i}, {i*10});', wait=0.05)
    cmd(s, 'COMMIT;', wait=1.0)
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(99, 990);')
    crash(s); proc.wait(timeout=5)
    proc2 = restart_server('t10_db')
    s2 = connect()
    r = cmd(s2, 'SELECT * FROM t1 WHERE id = 1;')
    ok = V(has_col(r, 10), '检查点前已提交id=1存在')
    r = cmd(s2, 'SELECT * FROM t1 WHERE id = 40;')
    ok &= V(has_col(r, 400), '检查点后已提交id=40存在')
    r = cmd(s2, 'SELECT * FROM t1 WHERE id = 99;')
    ok &= V(not has_data(r), '未提交id=99已回滚')
    cmd(s2, 'exit'); s2.close(); proc2.terminate(); kill_server()
    return ok

def test_checkpoint_multi():
    print('\n' + '='*60)
    print('测试11: 多次检查点+crash恢复')
    print('='*60)
    proc = start_fresh('t11_db')
    s = connect()
    cmd(s, 'CREATE TABLE t1 (id INT, val INT);')
    cmd(s, 'BEGIN;'); cmd(s, 'INSERT INTO t1 VALUES(1, 1);'); cmd(s, 'COMMIT;')
    cmd(s, 'create static_checkpoint;')
    cmd(s, 'BEGIN;'); cmd(s, 'INSERT INTO t1 VALUES(2, 2);'); cmd(s, 'COMMIT;')
    cmd(s, 'create static_checkpoint;')
    cmd(s, 'BEGIN;'); cmd(s, 'INSERT INTO t1 VALUES(3, 3);'); cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;'); cmd(s, 'INSERT INTO t1 VALUES(4, 4);')
    crash(s); proc.wait(timeout=5)
    proc2 = restart_server('t11_db')
    s2 = connect()
    r = cmd(s2, 'SELECT * FROM t1;')
    ok = V(has_col(r, 1), 'id=1保留')
    ok &= V(has_col(r, 2), 'id=2保留')
    ok &= V(has_col(r, 3), 'id=3保留')
    ok &= V(not has_col(r, 4), 'id=4回滚')
    cmd(s2, 'exit'); s2.close(); proc2.terminate(); kill_server()
    return ok

def test_update_with_index():
    print('\n' + '='*60)
    print('测试12: 带索引的UPDATE crash恢复')
    print('='*60)
    proc = start_fresh('t12_db')
    s = connect()
    cmd(s, 'CREATE TABLE t1 (id INT, val INT);')
    cmd(s, 'CREATE INDEX t1 (id);')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(1, 100);')
    cmd(s, 'INSERT INTO t1 VALUES(2, 200);')
    cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;'); cmd(s, 'UPDATE t1 SET val = 111 WHERE id = 1;'); cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;'); cmd(s, 'UPDATE t1 SET val = 999 WHERE id = 2;')
    crash(s); proc.wait(timeout=5)
    proc2 = restart_server('t12_db')
    s2 = connect()
    r = cmd(s2, 'SELECT * FROM t1;')
    ok = V(has_col(r, 1) and has_col(r, 111), '已提交UPDATE保留')
    ok &= V(has_col(r, 2) and has_col(r, 200), '未提交UPDATE恢复旧值')
    r = cmd(s2, 'SELECT * FROM t1 WHERE id = 1;')
    ok &= V(has_col(r, 111), '索引点查id=1返回新值')
    cmd(s2, 'exit'); s2.close(); proc2.terminate(); kill_server()
    return ok

def test_delete_with_index():
    print('\n' + '='*60)
    print('测试13: 带索引的DELETE crash恢复')
    print('='*60)
    proc = start_fresh('t13_db')
    s = connect()
    cmd(s, 'CREATE TABLE t1 (id INT, val INT);')
    cmd(s, 'CREATE INDEX t1 (id);')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(1, 100);')
    cmd(s, 'INSERT INTO t1 VALUES(2, 200);')
    cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;'); cmd(s, 'DELETE FROM t1 WHERE id = 1;'); cmd(s, 'COMMIT;')
    cmd(s, 'BEGIN;'); cmd(s, 'DELETE FROM t1 WHERE id = 2;')
    crash(s); proc.wait(timeout=5)
    proc2 = restart_server('t13_db')
    s2 = connect()
    r = cmd(s2, 'SELECT * FROM t1;')
    ok = V(not has_col(r, 100), '已提交DELETE id=1不存在')
    ok &= V(has_col(r, 2) and has_col(r, 200), '未提交DELETE id=2恢复')
    r = cmd(s2, 'SELECT * FROM t1 WHERE id = 2;')
    ok &= V(has_col(r, 200), '索引点查恢复的id=2')
    cmd(s2, 'exit'); s2.close(); proc2.terminate(); kill_server()
    return ok

def test_no_phantom():
    print('\n' + '='*60)
    print('测试14: 恢复后唯一索引约束有效')
    print('='*60)
    proc = start_fresh('t14_db')
    s = connect()
    cmd(s, 'CREATE TABLE t1 (id INT, val INT);')
    cmd(s, 'CREATE INDEX t1 (id);')
    cmd(s, 'BEGIN;')
    cmd(s, 'INSERT INTO t1 VALUES(1, 100);')
    cmd(s, 'INSERT INTO t1 VALUES(2, 200);')
    cmd(s, 'COMMIT;')
    crash(s); proc.wait(timeout=5)
    proc2 = restart_server('t14_db')
    s2 = connect()
    r = cmd(s2, 'INSERT INTO t1 VALUES(1, 999);')
    ok = V('failure' in r.lower() or 'duplicate' in r.lower(), '唯一索引阻止重复键')
    cmd(s2, 'INSERT INTO t1 VALUES(3, 300);')
    r = cmd(s2, 'SELECT * FROM t1 WHERE id = 3;')
    ok &= V(has_col(r, 300), '新键id=3可插入')
    cmd(s2, 'exit'); s2.close(); proc2.terminate(); kill_server()
    return ok


# ============== 主测试套件 ==============

def run_all():
    kill_server()
    time.sleep(1)
    results = {}
    tests = [
        ('基本已提交恢复', test_basic_commit),
        ('基本未提交回滚', test_basic_rollback),
        ('混合事务恢复', test_mixed_transactions),
        ('索引恢复', test_index_recovery),
        ('DELETE恢复', test_delete_recovery),
        ('UPDATE恢复', test_update_recovery),
        ('大量数据恢复', test_large_data),
        ('大量数据+索引恢复', test_large_index),
        ('二次崩溃恢复', test_double_crash),
        ('检查点恢复', test_checkpoint_basic),
        ('多次检查点恢复', test_checkpoint_multi),
        ('UPDATE+索引恢复', test_update_with_index),
        ('DELETE+索引恢复', test_delete_with_index),
        ('唯一索引约束', test_no_phantom),
    ]
    for name, test_func in tests:
        try:
            results[name] = test_func()
        except Exception as e:
            print(f'  [ERROR] {name}: {e}')
            import traceback; traceback.print_exc()
            results[name] = False
        kill_server(); time.sleep(0.5)
    print('\n' + '='*60)
    print('测试结果汇总')
    print('='*60)
    passed = sum(1 for v in results.values() if v)
    for name, ok in results.items():
        print(f'  [{"PASS" if ok else "FAIL"}] {name}')
    print(f'\n通过: {passed}/{len(results)}')
    return passed == len(results)

if __name__ == '__main__':
    success = run_all()
    sys.exit(0 if success else 1)
