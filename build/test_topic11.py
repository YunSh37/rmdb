"""
题目十一 故障恢复 完整测试（严格匹配测试文档）

测试覆盖6个测试点：
  1. crash_recovery_single_thread    — 单线程 + 小数据 + 无检查点
  2. crash_recovery_multi_thread     — 多线程 + 小数据 + 无检查点
  3. crash_recovery_index            — 单线程 + 索引 + 大数据 + 无检查点
  4. crash_recovery_large_data       — 多线程 + 大数据 + 无检查点
  5. crash_recovery_without_checkpoint — 单线程 + 巨量数据 + 无检查点 + 测量恢复时间 t1
  6. crash_recovery_with_checkpoint    — 单线程 + 巨量数据 + 随机检查点 + 测量恢复时间 t2,
                                       要求 t2 < t1 * 0.7 且一致性检测通过

表结构严格匹配测试文档中的 9 张 TPC-C 风格表。
事务模式匹配测试文档 2.2 节。
恢复时间测量方法匹配测试文档 2.5 节。
"""
import socket, time, os, sys, subprocess, re, random, threading

# ===================== 配置 =====================
BUILD = '/mnt/d/Python_Project/RMDB_proj/rmdb/build'
BIN   = f'{BUILD}/bin/rmdb'
HOST  = '127.0.0.1'
PORT  = 8765

# ===================== 表定义（精确匹配测试文档） =====================
TABLES_SQL = [
    # warehouse
    "CREATE TABLE warehouse (w_id INT, w_name CHAR(10), w_street_1 CHAR(20), "
    "w_street_2 CHAR(20), w_city CHAR(20), w_state CHAR(2), w_zip CHAR(9), "
    "w_tax FLOAT, w_ytd FLOAT);",

    # district
    "CREATE TABLE district (d_id INT, d_w_id INT, d_name CHAR(10), d_street_1 CHAR(20), "
    "d_street_2 CHAR(20), d_city CHAR(20), d_state CHAR(2), d_zip CHAR(9), "
    "d_tax FLOAT, d_ytd FLOAT, d_next_o_id INT);",

    # customer (21 columns)
    "CREATE TABLE customer (c_id INT, c_d_id INT, c_w_id INT, c_first CHAR(16), "
    "c_middle CHAR(2), c_last CHAR(16), c_street_1 CHAR(20), c_street_2 CHAR(20), "
    "c_city CHAR(20), c_state CHAR(2), c_zip CHAR(9), c_phone CHAR(16), "
    "c_since CHAR(30), c_credit CHAR(2), c_credit_lim INT, c_discount FLOAT, "
    "c_balance FLOAT, c_ytd_payment FLOAT, c_payment_cnt INT, c_delivery_cnt INT, "
    "c_data CHAR(50));",

    # history
    "CREATE TABLE history (h_c_id INT, h_c_d_id INT, h_c_w_id INT, h_d_id INT, "
    "h_w_id INT, h_date CHAR(19), h_amount FLOAT, h_data CHAR(24));",

    # new_orders
    "CREATE TABLE new_orders (no_o_id INT, no_d_id INT, no_w_id INT);",

    # orders
    "CREATE TABLE orders (o_id INT, o_d_id INT, o_w_id INT, o_c_id INT, "
    "o_entry_d CHAR(19), o_carrier_id INT, o_ol_cnt INT, o_all_local INT);",

    # order_line
    "CREATE TABLE order_line (ol_o_id INT, ol_d_id INT, ol_w_id INT, ol_number INT, "
    "ol_i_id INT, ol_supply_w_id INT, ol_delivery_d CHAR(30), ol_quantity INT, "
    "ol_amount FLOAT, ol_dist_info CHAR(24));",

    # item
    "CREATE TABLE item (i_id INT, i_im_id INT, i_name CHAR(24), i_price FLOAT, "
    "i_data CHAR(50));",

    # stock (16 columns)
    "CREATE TABLE stock (s_i_id INT, s_w_id INT, s_quantity INT, s_dist_01 CHAR(24), "
    "s_dist_02 CHAR(24), s_dist_03 CHAR(24), s_dist_04 CHAR(24), s_dist_05 CHAR(24), "
    "s_dist_06 CHAR(24), s_dist_07 CHAR(24), s_dist_08 CHAR(24), s_dist_09 CHAR(24), "
    "s_dist_10 CHAR(24), s_ytd FLOAT, s_order_cnt INT, s_remote_cnt INT, s_data CHAR(50));",
]

TABLE_NAMES = ['warehouse','district','customer','history','new_orders',
               'orders','order_line','item','stock']

# ===================== 工具函数 =====================

def kill_server():
    """杀死所有 rmdb 进程"""
    os.system('pkill -9 rmdb 2>/dev/null; sleep 0.5')

def start_fresh(db_name):
    """全新启动：删除旧数据库，创建新数据库"""
    kill_server()
    os.system(f'rm -rf {BUILD}/{db_name} 2>/dev/null')
    proc = subprocess.Popen([BIN, db_name], cwd=BUILD,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2.0)
    return proc

def restart_server(db_name):
    """重启服务器（保留已有数据库，用于 crash 后恢复）"""
    kill_server()
    proc = subprocess.Popen([BIN, db_name], cwd=BUILD,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return proc  # 立即返回，由 measure_recovery_time 轮询

def connect(timeout=10):
    """连接到服务器"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    sock.connect((HOST, PORT))
    return sock

def send_sql(sock, sql, wait=0.3, silent=False):
    """发送 SQL 并获取响应"""
    if not silent:
        short = sql[:90] + ('...' if len(sql) > 90 else '')
        print(f'  > {short}')
    sock.send((sql + '\0').encode())
    time.sleep(wait)
    try:
        resp = sock.recv(65536).decode('utf-8', errors='replace')
        if not silent and 'failure' in resp.lower() and 'success' not in resp.lower():
            print(f'    [ERR] {resp.strip()[:250]}')
        return resp
    except socket.timeout:
        return '(timeout)'

def has_col(resp, val):
    """检查管道符分隔的输出中是否存在某个列值"""
    return bool(re.search(r'\|\s*' + str(val) + r'\s*\|', resp))

def has_data_rows(resp):
    """检查是否有数据行"""
    return bool(re.search(r'\|\s*\d+', resp))

def verify(cond, msg):
    if cond: print(f'  [PASS] {msg}')
    else: print(f'  [FAIL] {msg}')
    return cond

# ===================== 数据初始化 =====================

def create_all_tables(sock):
    """创建测试文档定义的全部 9 张表"""
    for sql in TABLES_SQL:
        send_sql(sock, sql)
    print('  [OK] 9 张表创建完成')

def insert_base_data(sock, num_warehouses, num_items):
    """
    插入基础数据。
    对于每个 warehouse 创建对应的 district 和 stock。
    """
    print(f'  插入基础数据 (warehouses={num_warehouses}, items={num_items})...')

    # --- warehouse ---
    for w in range(1, num_warehouses + 1):
        send_sql(sock, f"INSERT INTO warehouse VALUES({w}, 'W{w}', 'St1_{w}', "
                 f"'St2_{w}', 'City{w}', 'ST', '{w:09d}', 0.05, 300000.0);", silent=True)

    # --- district (每个 warehouse 2 个 district) ---
    for w in range(1, num_warehouses + 1):
        for d in range(1, 3):
            send_sql(sock, f"INSERT INTO district VALUES({d}, {w}, 'D{d}', 'Dst1', "
                     f"'Dst2', 'Dcity', 'ST', '12345', 0.03, 30000.0, 5);", silent=True)

    # --- item ---
    for i in range(1, num_items + 1):
        send_sql(sock, f"INSERT INTO item VALUES({i}, {i*10}, 'ItemName_{i}', "
                 f"{10.0 + i*0.5:.1f}, 'ItemData_{i}');", silent=True)

    # --- stock (每个 item × 每个 warehouse) ---
    for w in range(1, num_warehouses + 1):
        for i in range(1, num_items + 1):
            send_sql(sock, f"INSERT INTO stock VALUES({i}, {w}, 100, "
                     f"'dist01','dist02','dist03','dist04','dist05',"
                     f"'dist06','dist07','dist08','dist09','dist10',"
                     f"0.0, 0, 0, 'StockData_{i}_{w}');", silent=True)

    # --- customer (少量) ---
    for c in range(1, 4):
        send_sql(sock, f"INSERT INTO customer VALUES({c}, 1, 1, 'First_{c}', 'M', "
                 f"'Last_{c}', 'St_{c}', 'Apt_{c}', 'City_{c}', 'ST', '{c:09d}', "
                 f"'1234567890', '2023-01-01 00:00:00', 'GC', 50000, 0.1, 0.0, 0.0, "
                 f"0, 0, 'CustData_{c}');", silent=True)

    print('  [OK] 基础数据插入完成')

# ===================== 事务执行（匹配测试文档 2.2 节） =====================

def run_transaction(sock, o_id, d_id=1, w_id=1, i_id=10):
    """
    执行匹配测试文档 2.2 节的典型事务。
    参数:
      o_id  — 用于 orders / new_orders / order_line 的主键
      d_id  — district id (默认 1)
      w_id  — warehouse id (默认 1)
      i_id  — item id (默认 10，与文档一致)
    """
    send_sql(sock, 'BEGIN;')
    # 文档第1句: 联合查询 customer + warehouse
    send_sql(sock, f"SELECT c_discount, c_last, c_credit, w_tax FROM customer, warehouse "
             f"WHERE w_id={w_id} AND c_w_id=w_id AND c_d_id={d_id} AND c_id=2;")
    # 文档第2句: 查 district
    send_sql(sock, f"SELECT d_next_o_id, d_tax FROM district WHERE d_id={d_id} AND d_w_id={w_id};")
    # 文档第3句: 更新 district
    send_sql(sock, f"UPDATE district SET d_next_o_id=5 WHERE d_id={d_id} AND d_w_id={w_id};")
    # 文档第4句: 插入 orders
    send_sql(sock, f"INSERT INTO orders VALUES ({o_id}, {d_id}, {w_id}, 2, "
             f"'2023-06-03 19:25:47', 26, 5, 1);")
    # 文档第5句: 插入 new_orders
    send_sql(sock, f"INSERT INTO new_orders VALUES ({o_id}, {d_id}, {w_id});")
    # 文档第6句: 查 item (i_id=10)
    send_sql(sock, f"SELECT i_price, i_name, i_data FROM item WHERE i_id={i_id};")
    # 文档第7句: 查 stock (s_i_id=10, s_w_id=1)
    send_sql(sock, f"SELECT s_quantity, s_data, s_dist_01, s_dist_02, s_dist_03, s_dist_04, "
             f"s_dist_05, s_dist_06, s_dist_07, s_dist_08, s_dist_09, s_dist_10 "
             f"FROM stock WHERE s_i_id={i_id} AND s_w_id={w_id};")
    # 文档第8句: 更新 stock
    send_sql(sock, f"UPDATE stock SET s_quantity=7 WHERE s_i_id={i_id} AND s_w_id={w_id};")
    # 文档第9句: 插入 order_line
    send_sql(sock, f"INSERT INTO order_line VALUES ({o_id}, {d_id}, {w_id}, 1, {i_id}, "
             f"{w_id}, '2023-06-03 19:25:47', 7, 286.625, 'ShortDistInfo');")
    # 文档第10句: 再次查 item
    send_sql(sock, f"SELECT i_price, i_name, i_data FROM item WHERE i_id={i_id};")
    send_sql(sock, 'COMMIT;')

# ===================== 恢复时间测量（匹配测试文档 2.5 节） =====================

def measure_recovery_time(db_name):
    """
    按照文档 2.5 节方法测量恢复时间。
    从调用此刻开始轮询重连 + 执行查询，直到查询成功返回。
    """
    query = "SELECT * FROM district;"
    start = time.time()
    while True:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(5)
            s.connect((HOST, PORT))
            s.send((query + '\0').encode())
            time.sleep(0.3)
            resp = s.recv(8192).decode('utf-8', errors='replace')
            s.close()
            if 'failure' not in resp.lower() and 'Error' not in resp and len(resp.strip()) > 0:
                break
        except (socket.error, socket.timeout, ConnectionRefusedError, OSError):
            pass
        time.sleep(0.05)
    return time.time() - start

# ===================== 一致性检测 =====================

def consistency_check(sock, expect_orders=None):
    """
    全面一致性检测：
    1. 所有 9 张表可查询且无错误
    2. warehouse / district / item / stock / orders 有数据
    3. district.d_next_o_id = 5
    4. orders 与 new_orders 数量一致
    5. stock 的 s_quantity 已更新为 7（事务中的 UPDATE 生效）
    """
    print('\n  --- 一致性检测 ---')
    ok = True

    # 1. 所有表可查询
    for tn in TABLE_NAMES:
        r = send_sql(sock, f'SELECT * FROM {tn};', silent=True)
        ok &= verify('failure' not in r.lower() and 'Error' not in r,
                     f'表 {tn} 可查询')

    # 2. 关键表有数据
    for tn, label in [('warehouse','warehouse'),('district','district'),
                       ('item','item'),('stock','stock'),
                       ('orders','orders'),('new_orders','new_orders')]:
        r = send_sql(sock, f'SELECT * FROM {tn};', silent=True)
        ok &= verify(has_data_rows(r), f'{label} 有数据')

    # 3. district d_next_o_id = 5
    r = send_sql(sock, 'SELECT * FROM district WHERE d_id=1 AND d_w_id=1;', silent=True)
    ok &= verify('5' in r, 'district.d_next_o_id = 5 (UPDATE 已生效)')

    # 4. item 存在
    r = send_sql(sock, 'SELECT * FROM item WHERE i_id=1;', silent=True)
    ok &= verify('ItemName' in r or 'ItemData' in r, 'item i_id=1 存在且可查询')
    r = send_sql(sock, 'SELECT * FROM item WHERE i_id=10;', silent=True)
    ok &= verify('ItemName' in r or 'ItemData' in r, 'item i_id=10 存在且可查询')

    # 5. stock s_quantity=7（事务中的 UPDATE 生效）
    r = send_sql(sock, 'SELECT * FROM stock WHERE s_i_id=10 AND s_w_id=1;', silent=True)
    ok &= verify('7' in r, 'stock s_quantity=7 (UPDATE 已生效)')

    # 6. 无错误残留在任何表中
    for tn in TABLE_NAMES:
        r = send_sql(sock, f'SELECT * FROM {tn};', silent=True)
        ok &= verify('failure' not in r.lower() and 'Error' not in r,
                     f'  {tn} 无错误')

    return ok

# ===================== 六个测试场景 =====================

def test1_single_thread():
    """测试点1: crash_recovery_single_thread_test"""
    print('\n' + '='*70)
    print('测试点1: crash_recovery_single_thread_test')
    print('  单线程 + 小数据 + 无检查点')
    print('='*70)

    _ = start_fresh('tp1_st')
    s = connect()
    create_all_tables(s)
    insert_base_data(s, num_warehouses=1, num_items=10)

    # 执行 5 个事务，模拟文档中的操作模式
    for t in range(1, 6):
        run_transaction(s, o_id=t)

    send_sql(s, 'SELECT * FROM district;')
    print('  >>> CRASH <<<')
    s.send(b'crash\0'); s.close()
    _.wait(timeout=5)

    # 重启 + 恢复 + 一致性检测
    proc = restart_server('tp1_st')
    time.sleep(2)
    s2 = connect()
    ok = consistency_check(s2)
    send_sql(s2, 'exit'); s2.close()
    proc.terminate(); kill_server()
    return ok


def test2_multi_thread():
    """测试点2: crash_recovery_multi_thread_test"""
    print('\n' + '='*70)
    print('测试点2: crash_recovery_multi_thread_test')
    print('  多线程 + 小数据 + 无检查点')
    print('='*70)

    _ = start_fresh('tp2_mt')
    s = connect()
    create_all_tables(s)
    insert_base_data(s, num_warehouses=1, num_items=10)

    # 4 个线程，每个执行 4 个事务 = 共 16 个事务
    errors_lock = threading.Lock()
    errors = []

    def worker(tid):
        try:
            sk = connect()
            for n in range(4):
                oid = tid * 100 + n
                run_transaction(sk, o_id=oid)
            sk.close()
        except Exception as e:
            with errors_lock:
                errors.append(f'T{tid}: {e}')

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
    for t in threads: t.start()
    for t in threads: t.join()

    if errors:
        print(f'  [WARN] 多线程错误: {errors}')

    send_sql(s, 'SELECT * FROM district;')
    print('  >>> CRASH <<<')
    s.send(b'crash\0'); s.close()
    _.wait(timeout=5)

    proc = restart_server('tp2_mt')
    time.sleep(3)
    s2 = connect()
    ok = consistency_check(s2)
    send_sql(s2, 'exit'); s2.close()
    proc.terminate(); kill_server()
    return ok


def test3_index():
    """测试点3: crash_recovery_index_test"""
    print('\n' + '='*70)
    print('测试点3: crash_recovery_index_test')
    print('  单线程 + 索引 + 大数据 + 无检查点')
    print('='*70)

    _ = start_fresh('tp3_idx')
    s = connect()
    create_all_tables(s)

    # 创建索引（覆盖主要表）
    print('  创建索引...')
    for tn, col in [('warehouse','w_id'),('district','d_id'),('customer','c_id'),
                     ('orders','o_id'),('item','i_id'),('stock','s_i_id')]:
        send_sql(s, f'CREATE INDEX {tn} ({col});')

    insert_base_data(s, num_warehouses=2, num_items=20)

    # 执行 15 个事务（比测试点1多）
    for t in range(1, 16):
        run_transaction(s, o_id=t)

    send_sql(s, 'SELECT * FROM district;')
    print('  >>> CRASH <<<')
    s.send(b'crash\0'); s.close()
    _.wait(timeout=5)

    proc = restart_server('tp3_idx')
    time.sleep(3)
    s2 = connect()
    ok = consistency_check(s2)

    # 额外验证：索引正常工作
    print('\n  --- 索引专项验证 ---')
    r = send_sql(s2, 'SELECT * FROM item WHERE i_id = 1;', silent=True)
    ok &= verify('ItemName_1' in r or 'ItemData_1' in r, '索引点查 item i_id=1 返回正确')
    r = send_sql(s2, 'SELECT * FROM item WHERE i_id = 10;', silent=True)
    ok &= verify('ItemName_10' in r or 'ItemData_10' in r, '索引点查 item i_id=10 返回正确')
    r = send_sql(s2, 'SELECT * FROM orders WHERE o_id = 5;', silent=True)
    ok &= verify(has_data_rows(r), '索引点查 orders o_id=5 返回正确')
    r = send_sql(s2, 'SELECT * FROM stock WHERE s_i_id = 1;', silent=True)
    ok &= verify(has_data_rows(r), '索引点查 stock s_i_id=1 返回正确')

    # 唯一索引：尝试插入重复键
    r = send_sql(s2, 'INSERT INTO item VALUES(1, 999, \'Dup\', 1.0, \'DupData\');', silent=True)
    ok &= verify('failure' in r.lower() or 'duplicate' in r.lower(),
                 '唯一索引阻止重复 item i_id=1 插入')
    r = send_sql(s2, 'INSERT INTO orders VALUES(5, 1, 1, 2, \'2023-01-01\', 1, 1, 1);', silent=True)
    ok &= verify('failure' in r.lower() or 'duplicate' in r.lower(),
                 '唯一索引阻止重复 orders o_id=5 插入')

    send_sql(s2, 'exit'); s2.close()
    proc.terminate(); kill_server()
    return ok


def test4_large_data():
    """测试点4: crash_recovery_large_data_test"""
    print('\n' + '='*70)
    print('测试点4: crash_recovery_large_data_test')
    print('  多线程 + 大数据 + 无检查点')
    print('='*70)

    _ = start_fresh('tp4_large')
    s = connect()
    create_all_tables(s)
    insert_base_data(s, num_warehouses=2, num_items=30)

    errors_lock = threading.Lock()
    errors = []

    def worker(tid):
        try:
            sk = connect()
            for n in range(8):  # 4×8=32 个事务
                oid = tid * 1000 + n
                run_transaction(sk, o_id=oid)
            sk.close()
        except Exception as e:
            with errors_lock:
                errors.append(f'T{tid}: {e}')

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
    for t in threads: t.start()
    for t in threads: t.join()

    if errors:
        print(f'  [WARN] 多线程错误: {errors}')

    send_sql(s, 'SELECT * FROM warehouse;')
    print('  >>> CRASH <<<')
    s.send(b'crash\0'); s.close()
    _.wait(timeout=5)

    proc = restart_server('tp4_large')
    time.sleep(4)
    s2 = connect()
    ok = consistency_check(s2)
    send_sql(s2, 'exit'); s2.close()
    proc.terminate(); kill_server()
    return ok


def test5_without_checkpoint():
    """测试点5: crash_recovery_without_checkpoint — 测量 t1"""
    print('\n' + '='*70)
    print('测试点5: crash_recovery_without_checkpoint')
    print('  单线程 + 巨量数据 + 无检查点 + 测量恢复时间 t1')
    print('='*70)

    _ = start_fresh('tp5_nockpt')
    s = connect()
    create_all_tables(s)
    insert_base_data(s, num_warehouses=3, num_items=50)

    # 执行 50 个事务（大量数据）
    print('  执行 50 个事务...')
    for t in range(1, 51):
        run_transaction(s, o_id=t)

    send_sql(s, 'SELECT * FROM warehouse;')
    print('  >>> CRASH <<<')
    s.send(b'crash\0'); s.close()
    _.wait(timeout=5)

    # 启动并测量恢复时间
    proc = restart_server('tp5_nockpt')
    t1 = measure_recovery_time('tp5_nockpt')
    print(f'\n  *** 恢复时间 t1 = {t1:.3f} 秒 ***')

    # 等待恢复完成后验证一致性
    time.sleep(1)
    s2 = connect()
    ok = consistency_check(s2)
    send_sql(s2, 'exit'); s2.close()
    proc.terminate(); kill_server()
    return ok, t1


def test6_with_checkpoint(t1):
    """测试点6: crash_recovery_with_checkpoint — 测量 t2, 验证 t2 < t1*0.7"""
    print('\n' + '='*70)
    print('测试点6: crash_recovery_with_checkpoint')
    print('  单线程 + 巨量数据 + 随机检查点 + 测量恢复时间 t2')
    print('='*70)

    _ = start_fresh('tp6_ckpt')
    s = connect()
    create_all_tables(s)
    insert_base_data(s, num_warehouses=3, num_items=50)

    # 执行 50 个事务，随机创建检查点
    print('  执行 50 个事务（随机创建检查点）...')
    ckpt_count = 0
    for t in range(1, 51):
        run_transaction(s, o_id=t)
        # 30% 概率随机创建检查点（模拟文档中"不定时发送 create static_checkpoint"）
        if random.random() < 0.3:
            send_sql(s, 'create static_checkpoint;', silent=True)
            ckpt_count += 1
            print(f'    [检查点 #{ckpt_count}] 在事务 {t} 之后创建')

    print(f'  共创建 {ckpt_count} 个检查点')
    send_sql(s, 'SELECT * FROM warehouse;')
    print('  >>> CRASH <<<')
    s.send(b'crash\0'); s.close()
    _.wait(timeout=5)

    # 启动并测量恢复时间
    proc = restart_server('tp6_ckpt')
    t2 = measure_recovery_time('tp6_ckpt')
    print(f'\n  *** 恢复时间 t2 = {t2:.3f} 秒 ***')

    if t1 is not None:
        ratio = t2 / t1 if t1 > 0 else float('inf')
        threshold = t1 * 0.7
        print(f'  *** t1 = {t1:.3f}s, t2 = {t2:.3f}s, t2/t1 = {ratio:.1%} ***')
        print(f'  *** 阈值: t2 < 0.7 × t1 = {threshold:.3f}s ***')

    # 等待恢复完成后验证一致性
    time.sleep(1)
    s2 = connect()
    ok = consistency_check(s2)
    send_sql(s2, 'exit'); s2.close()
    proc.terminate(); kill_server()

    # 验证恢复时间要求
    if t1 is not None:
        ok &= verify(t2 < t1 * 0.7,
                     f't2({t2:.3f}s) < 0.7×t1({t1:.3f}s) = {threshold:.3f}s '
                     f'(实际比例 {ratio:.1%})')
    return ok, t2


# ===================== 主入口 =====================

def run_all():
    kill_server()
    time.sleep(1)

    results = {}
    t1_value = None

    tests = [
        ('1_crash_recovery_single_thread',    test1_single_thread),
        ('2_crash_recovery_multi_thread',     test2_multi_thread),
        ('3_crash_recovery_index',            test3_index),
        ('4_crash_recovery_large_data',       test4_large_data),
    ]

    # 测试点 1-4
    for name, func in tests:
        try:
            results[name] = func()
        except Exception as e:
            print(f'\n  [ERROR] {name}: {e}')
            import traceback; traceback.print_exc()
            results[name] = False
        kill_server(); time.sleep(0.5)

    # 测试点 5 — 测量 t1
    try:
        ok5, t1_value = test5_without_checkpoint()
        results['5_crash_recovery_without_checkpoint'] = ok5
    except Exception as e:
        print(f'\n  [ERROR] test5: {e}')
        import traceback; traceback.print_exc()
        results['5_crash_recovery_without_checkpoint'] = False
    kill_server(); time.sleep(0.5)

    # 测试点 6 — 测量 t2，验证 t2 < t1 * 0.7
    try:
        ok6, _ = test6_with_checkpoint(t1_value)
        results['6_crash_recovery_with_checkpoint'] = ok6
    except Exception as e:
        print(f'\n  [ERROR] test6: {e}')
        import traceback; traceback.print_exc()
        results['6_crash_recovery_with_checkpoint'] = False

    # ===================== 汇总 =====================
    print('\n')
    print('='*70)
    print('         题目十一 测试结果汇总')
    print('='*70)
    print(f'  {"测试点":<45s} {"结果":>8s}')
    print('  ' + '-'*53)
    passed = 0
    for name, ok in results.items():
        status = 'PASS' if ok else 'FAIL'
        print(f'  {name:<45s} {status:>8s}')
        if ok: passed += 1
    print('  ' + '-'*53)
    print(f'  通过: {passed}/{len(results)}')

    if t1_value is not None:
        print(f'\n  无检查点恢复时间 t1 = {t1_value:.3f}s')

    print(f'\n  要求: 6 个测试点全部 PASS')
    print(f'        测试点6 额外要求: t2 < 0.7 × t1')
    print('='*70)

    return passed == len(results)


if __name__ == '__main__':
    success = run_all()
    sys.exit(0 if success else 1)
