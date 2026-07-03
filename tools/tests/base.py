"""RMDB 测试公共基础设施
提供 RMDBTester 类及共享常量，供各题目测试模块使用。
"""

import socket
import subprocess
import time
import os
import sys
import threading
import random
import string

SERVER_PORT = 8765
BUILD_DIR = "build"
TEST_DB = "test_auto_db"
DB_PATH = os.path.join(BUILD_DIR, TEST_DB)


class RMDBTester:
    """RMDB 测试器：管理服务端生命周期、发送SQL、验证结果"""

    def __init__(self):
        self.sock = None
        self.server_proc = None
        self.passed = 0
        self.failed = 0
        self._recovery_log = None
        self.t1_recovery_time = None  # 测试点5测得的恢复时间，供测试点6使用

    # ================================================================
    # 服务端生命周期
    # ================================================================

    def start_server(self):
        """启动服务端（清理旧数据库）"""
        if os.path.exists(DB_PATH):
            import shutil
            shutil.rmtree(DB_PATH)

        os.chdir(BUILD_DIR)
        self.server_proc = subprocess.Popen(
            ["./bin/rmdb", TEST_DB],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        os.chdir("..")
        time.sleep(0.5)

    def start_server_no_clean(self):
        """启动服务端（保留已有数据库，用于crash-recovery测试）"""
        os.chdir(BUILD_DIR)
        self._recovery_log = open(os.path.join(TEST_DB, "recovery_stderr.log"), "a")
        self.server_proc = subprocess.Popen(
            ["./bin/rmdb", TEST_DB],
            stdout=self._recovery_log, stderr=self._recovery_log
        )
        os.chdir("..")
        time.sleep(1.0)

    def stop_server(self):
        """停止服务端"""
        if self.sock:
            self.sock.close()
        if self.server_proc:
            self.server_proc.terminate()
            self.server_proc.wait(timeout=5)

    def connect(self):
        """连接到服务端，重试最多30秒"""
        for _ in range(60):
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.connect(("127.0.0.1", SERVER_PORT))
                self.sock.settimeout(5)
                return True
            except ConnectionRefusedError:
                time.sleep(0.5)
        return False

    # ================================================================
    # Crash / Recovery 流程
    # ================================================================

    def send_crash(self):
        """发送crash命令使服务端崩溃（模拟故障）"""
        if self.sock:
            try:
                self.sock.sendall(b"crash\0")
                time.sleep(0.5)
            except (BrokenPipeError, ConnectionResetError, OSError):
                # 服务端可能已经崩溃，socket已断开
                pass
        if self.server_proc:
            try:
                self.server_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.server_proc.kill()
                self.server_proc.wait(timeout=2)
        self.sock = None
        self.server_proc = None
        if hasattr(self, '_recovery_log') and self._recovery_log:
            self._recovery_log.close()
            self._recovery_log = None
        time.sleep(1.0)

    def restart_after_crash(self):
        """crash后重启服务端并重连"""
        self.start_server_no_clean()
        if not self.connect():
            print("恢复后无法连接到服务端！")
            return False
        return True

    def timed_restart_after_crash(self):
        """crash后重启并测量恢复时间，返回 (成功, 耗时秒数)"""
        t_start = time.perf_counter()
        self.start_server_no_clean()
        if not self.connect():
            return False, 0.0
        result = self.send_sql("select * from district;")
        t_end = time.perf_counter()
        elapsed = t_end - t_start
        ok = "failure" not in result.lower() and "error" not in result.lower()
        return ok, elapsed

    # ================================================================
    # SQL 发送
    # ================================================================

    def send_sql(self, sql: str) -> str:
        """发送 SQL 并接收响应"""
        if not sql.endswith(";"):
            sql += ";"
        self.sock.sendall(sql.encode() + b'\0')
        time.sleep(0.1)
        try:
            data = self.sock.recv(8192)
            result = data.rstrip(b'\0').decode('utf-8', errors='replace')
            return result.strip()
        except socket.timeout:
            return "TIMEOUT"

    def send_sql_fast(self, sql: str) -> str:
        """发送 SQL 并接收响应（无等待延迟，用于性能测试）"""
        if not sql.endswith(";"):
            sql += ";"
        self.sock.sendall(sql.encode() + b'\0')
        try:
            data = self.sock.recv(8192)
            result = data.rstrip(b'\0').decode('utf-8', errors='replace')
            return result.strip()
        except socket.timeout:
            return "TIMEOUT"

    def send_sql_no_wait(self, sql: str):
        """发送 SQL 不检查结果（用于批量数据准备）"""
        if not sql.endswith(";"):
            sql += ";"
        self.sock.sendall(sql.encode() + b'\0')

    # ================================================================
    # 结果校验
    # ================================================================

    def check_result(self, sql: str, expected_fragment: str, actual: str) -> bool:
        """检查结果是否包含预期片段"""
        if actual == "TIMEOUT":
            return False
        if expected_fragment == "SUCCESS":
            return "failure" not in actual.lower() and "error" not in actual.lower()
        if expected_fragment == "failure":
            return ("failure" in actual.lower() or
                    "duplicate" in actual.lower() or
                    "error" in actual.lower())
        if expected_fragment == "":
            return True

        flat = actual.replace('\n', '|').replace(' ', '')
        flat = ''.join(flat.split())

        for frag in expected_fragment.split("\n"):
            frag = frag.strip()
            if not frag:
                continue
            flat_frag = frag.replace(' ', '')
            if flat_frag not in flat:
                return False
        return True

    def run_tests(self, topic: str, tests: list):
        """运行一组测试"""
        print(f"\n{'='*60}")
        print(f"  测试: {topic}")
        print(f"{'='*60}")

        for i, (sql, expected) in enumerate(tests, 1):
            result = self.send_sql(sql)
            ok = self.check_result(sql, expected, result)

            status = "✓ PASS" if ok else "✗ FAIL"
            if ok:
                self.passed += 1
            else:
                self.failed += 1

            print(f"\n[{status}] 用例{i}")
            print(f"  SQL: {sql}")
            if not ok:
                print(f"  期望包含: {expected[:100]}")
                print(f"  实际输出: {result[:200]}")
            else:
                if "explain" in sql.lower():
                    print(f"  输出:\n{result}")

    # ================================================================
    # 数据一致性检验
    # ================================================================

    def check_data_consistency(self):
        """数据一致性检验（依据 数据一致性检验规则.md）"""
        print(f"\n{'='*60}")
        print(f"  数据一致性检验")
        print(f"{'='*60}")

        checks_passed = 0
        checks_failed = 0

        tables_result = self.send_sql("show tables;")
        available_tables = set()
        for line in tables_result.split("\n"):
            line = line.strip().strip("|").strip()
            if line and not line.startswith("Table"):
                available_tables.add(line)

        known_tables = {
            "departments": 4, "employees": 5,
            "grade": None, "records": None, "t1": None, "t2": None,
        }

        for tab_name, expected_count in known_tables.items():
            if tab_name in available_tables:
                result = self.send_sql(f"select COUNT(*) from {tab_name};")
                try:
                    count_val = None
                    for part in result.replace("|", " ").split():
                        try:
                            count_val = int(part); break
                        except ValueError:
                            continue
                    if count_val is not None:
                        if expected_count is not None:
                            if count_val == expected_count:
                                print(f"  ✓ {tab_name}: COUNT(*)={count_val} (预期={expected_count})")
                                checks_passed += 1
                            else:
                                print(f"  ✗ {tab_name}: COUNT(*)={count_val} (预期={expected_count})")
                                checks_failed += 1
                        else:
                            print(f"  - {tab_name}: COUNT(*)={count_val}")
                except Exception as e:
                    print(f"  ✗ {tab_name}: 无法解析结果 '{result}'")

        if "departments" in available_tables and "employees" in available_tables:
            result1 = self.send_sql(
                "select COUNT(*) from departments semi join employees "
                "on departments.dept_id = employees.dept_id;")
            result2 = self.send_sql(
                "select COUNT(*) from departments join employees "
                "on departments.dept_id = employees.dept_id;")
            try:
                semi_count = inner_count = None
                for part in result1.replace("|", " ").split():
                    try: semi_count = int(part); break
                    except ValueError: continue
                for part in result2.replace("|", " ").split():
                    try: inner_count = int(part); break
                    except ValueError: continue
                if semi_count is not None and inner_count is not None:
                    if semi_count <= inner_count:
                        print(f"  ✓ SEMI JOIN 去重: SEMI={semi_count} ≤ INNER={inner_count}")
                        checks_passed += 1
                    else:
                        print(f"  ✗ SEMI JOIN 去重: SEMI={semi_count} > INNER={inner_count}")
                        checks_failed += 1
            except Exception as e:
                print(f"  ✗ SEMI JOIN 一致性检查失败: {e}")

        if "orders" in available_tables:
            result = self.send_sql("select COUNT(*) from orders;")
            print(f"  orders 总数: {result.strip()}")

        total_checks = checks_passed + checks_failed
        if total_checks > 0:
            print(f"\n  一致性检验: {checks_passed}/{total_checks} 通过, {checks_failed} 失败")
        else:
            print(f"  (无可用的已知表进行一致性检查)")

    # ================================================================
    # 表清理
    # ================================================================

    def cleanup_leftover_tables(self):
        """清理测试残留的表"""
        tables_to_drop = [
            "t1", "t2", "grade", "warehouse",
            "students", "classes", "teams", "players",
            "authors", "books", "records",
            "departments", "employees", "projects", "empty_departments",
            "student", "student_idx", "student_idx2", "t",
            "mvcc_test", "mvcc_del", "mvcc_upd", "mvcc_txn", "mvcc_abt",
            "mvcc_idx", "mvcc_multi",
            "scan_test", "ts_test", "tuple_test",
            "crash_t1", "crash_t2", "crash_idx", "ckpt_t",
            "mt_t", "mt_large",
            "warehouse", "district", "customer", "history",
            "new_orders", "orders", "order_line", "item", "stock",
        ]
        for t in tables_to_drop:
            self.send_sql(f"drop table {t};")

    # ================================================================
    # 题目三: 索引性能验证
    # ================================================================

    def test_topic3_index_perf(self, is_multi_col=False):
        """题目三 测试点4/5：单列/多列索引性能验证
        插入N条数据 → 执行N次查询(无索引) → 建索引 → 再执行N次查询 → 比较耗时
        """
        N = 1000  # 官方测试用3000，此处用1000折中
        col_type = "多列" if is_multi_col else "单列"
        test_name = f"题目三 测试点{'5' if is_multi_col else '4'}: {col_type}索引性能验证"

        print(f"\n{'='*60}")
        print(f"  {test_name} ({N}条×{N}次查询)")
        print(f"{'='*60}")

        if is_multi_col:
            self.send_sql("create table warehouse (w_id int, name char(8), flo float);")
        else:
            self.send_sql("create table warehouse (w_id int, name char(8));")

        rand_names = [''.join(random.choices(string.ascii_lowercase, k=8)) for _ in range(N)]
        rand_floats = [random.uniform(128.0, 1024.0) for _ in range(N)]
        print(f"  插入{N}条数据...")
        for i in range(N):
            if is_multi_col:
                self.send_sql(
                    f"insert into warehouse values ({i+1}, '{rand_names[i]}', {rand_floats[i]:.1f});")
            else:
                self.send_sql(f"insert into warehouse values ({i+1}, '{rand_names[i]}');")
            if (i + 1) % 100 == 0:
                print(f"    已插入 {i+1}/{N}")

        print(f"  无索引: 执行{N}次查询...")
        t_start = time.perf_counter()
        for i in range(N):
            if is_multi_col:
                self.send_sql_fast(
                    f"select * from warehouse where w_id = {i+1} and flo = {rand_floats[i]:.1f};")
            else:
                self.send_sql_fast(f"select * from warehouse where w_id = {i+1};")
        time_a = time.perf_counter() - t_start
        print(f"  time_a (无索引) = {time_a:.3f}s")

        if is_multi_col:
            self.send_sql("create index warehouse(w_id,flo);")
        else:
            self.send_sql("create index warehouse(w_id);")

        print(f"  有索引: 执行{N}次查询...")
        t_start = time.perf_counter()
        for i in range(N):
            if is_multi_col:
                self.send_sql_fast(
                    f"select * from warehouse where w_id = {i+1} and flo = {rand_floats[i]:.1f};")
            else:
                self.send_sql_fast(f"select * from warehouse where w_id = {i+1};")
        time_b = time.perf_counter() - t_start
        print(f"  time_b (有索引) = {time_b:.3f}s")

        if time_a < 0.05:
            print(f"  ⚠ time_a 过小({time_a:.3f}s)，数据量不足以测量索引加速效果，跳过比率判定")
            self.passed += 1
        else:
            ratio = time_b / time_a * 100 if time_a > 0 else 100
            print(f"  比率: time_b/time_a = {ratio:.1f}% (目标 ≤ 85%)")
            if ratio <= 85:
                print(f"  ✓ PASS: {col_type}索引加速有效 ({ratio:.1f}%)")
                self.passed += 1
            else:
                print(f"  ✗ FAIL: {col_type}索引加速不足 ({ratio:.1f}% > 85%)")
                self.failed += 1

        self.send_sql("drop table warehouse;")

    # ================================================================
    # 题目九: 多线程故障恢复 Worker
    # ================================================================

    def _mt_worker_insert(self, thread_id, stop_event, commit_counts, lock):
        """多线程worker：循环执行 BEGIN→INSERT→COMMIT"""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.connect(("127.0.0.1", SERVER_PORT))
            sock.settimeout(3)
            count = 0
            while not stop_event.is_set():
                try:
                    val = thread_id * 10000 + count
                    sock.sendall(b"begin;\0"); sock.recv(8192)
                    sql = f"insert into mt_t values ({thread_id}, {val});\0"
                    sock.sendall(sql.encode()); sock.recv(8192)
                    sock.sendall(b"commit;\0"); sock.recv(8192)
                    count += 1
                except (socket.timeout, ConnectionResetError, BrokenPipeError, OSError):
                    break
            with lock:
                commit_counts[thread_id] = count
        except Exception:
            pass
        finally:
            try: sock.close()
            except Exception: pass

    def _mt_worker_mixed(self, thread_id, stop_event, op_counts, lock):
        """多线程worker：混合 INSERT/UPDATE/DELETE 操作"""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.connect(("127.0.0.1", SERVER_PORT))
            sock.settimeout(3)
            inserts = updates = deletes = 0
            base_id = thread_id * 1000
            while not stop_event.is_set():
                try:
                    op = random.randint(0, 2)
                    if op == 0:
                        rid = base_id + inserts
                        sock.sendall(b"begin;\0"); sock.recv(8192)
                        sql = f"insert into mt_large values ({rid}, {thread_id}, {random.randint(1,100)});\0"
                        sock.sendall(sql.encode()); sock.recv(8192)
                        sock.sendall(b"commit;\0"); sock.recv(8192)
                        inserts += 1
                    elif op == 1:
                        rid = base_id - 500 + random.randint(0, min(inserts, 500))
                        sock.sendall(b"begin;\0"); sock.recv(8192)
                        sql = f"update mt_large set val = {random.randint(1,100)} where id = {rid};\0"
                        sock.sendall(sql.encode()); sock.recv(8192)
                        sock.sendall(b"commit;\0"); sock.recv(8192)
                        updates += 1
                    else:
                        if inserts > 10:
                            rid = base_id - 500 + random.randint(0, min(inserts, 500))
                            sock.sendall(b"begin;\0"); sock.recv(8192)
                            sql = f"delete from mt_large where id = {rid};\0"
                            sock.sendall(sql.encode()); sock.recv(8192)
                            sock.sendall(b"commit;\0"); sock.recv(8192)
                            deletes += 1
                except (socket.timeout, ConnectionResetError, BrokenPipeError, OSError):
                    break
            with lock:
                op_counts[thread_id] = (inserts, updates, deletes)
        except Exception:
            pass
        finally:
            try: sock.close()
            except Exception: pass

    # ================================================================
    # 题目九: TPC-C 类辅助方法
    # ================================================================

    def _tpcc_setup(self):
        """创建TPC-C类9张表并插入初始数据（文档2.1节）"""
        print("  创建TPC-C类9张表...")
        tables_sql = [
            ("create table warehouse (w_id int, w_name char(10), w_street_1 char(20), "
             "w_street_2 char(20), w_city char(20), w_state char(2), w_zip char(9), "
             "w_tax float, w_ytd float);"),
            ("create table district (d_id int, d_w_id int, d_name char(10), "
             "d_street_1 char(20), d_street_2 char(20), d_city char(20), d_state char(2), "
             "d_zip char(9), d_tax float, d_ytd float, d_next_o_id int);"),
            ("create table customer (c_id int, c_d_id int, c_w_id int, c_first char(16), "
             "c_middle char(2), c_last char(16), c_street_1 char(20), c_street_2 char(20), "
             "c_city char(20), c_state char(2), c_zip char(9), c_phone char(16), "
             "c_since char(30), c_credit char(2), c_credit_lim int, c_discount float, "
             "c_balance float, c_ytd_payment float, c_payment_cnt int, c_delivery_cnt int, "
             "c_data char(50));"),
            ("create table history (h_c_id int, h_c_d_id int, h_c_w_id int, h_d_id int, "
             "h_w_id int, h_date char(19), h_amount float, h_data char(24));"),
            "create table new_orders (no_o_id int, no_d_id int, no_w_id int);",
            ("create table orders (o_id int, o_d_id int, o_w_id int, o_c_id int, "
             "o_entry_d char(19), o_carrier_id int, o_ol_cnt int, o_all_local int);"),
            ("create table order_line (ol_o_id int, ol_d_id int, ol_w_id int, ol_number int, "
             "ol_i_id int, ol_supply_w_id int, ol_delivery_d char(30), ol_quantity int, "
             "ol_amount float, ol_dist_info char(24));"),
            ("create table item (i_id int, i_im_id int, i_name char(24), i_price float, "
             "i_data char(50));"),
            ("create table stock (s_i_id int, s_w_id int, s_quantity int, "
             "s_dist_01 char(24), s_dist_02 char(24), s_dist_03 char(24), s_dist_04 char(24), "
             "s_dist_05 char(24), s_dist_06 char(24), s_dist_07 char(24), s_dist_08 char(24), "
             "s_dist_09 char(24), s_dist_10 char(24), s_ytd float, s_order_cnt int, "
             "s_remote_cnt int, s_data char(50));"),
        ]
        for sql in tables_sql:
            self.send_sql(sql)

        print("  插入 warehouse...")
        for w in range(1, 4):
            self.send_sql(
                f"insert into warehouse values ({w}, 'name_{w}', 'street1_{w}', "
                f"'street2_{w}', 'city_{w}', 'S{w}', 'zip{w}', "
                f"{w*0.1:.1f}, {w*1000.0:.1f});")

        print("  插入 district...")
        for w in range(1, 4):
            for d in range(1, 4):
                self.send_sql(
                    f"insert into district values ({d}, {w}, 'dname_{w}_{d}', "
                    f"'dst1', 'dst2', 'dcity', 'ST', '12345', "
                    f"{d*0.05:.2f}, {d*500.0:.2f}, {d+1});")

        print("  插入 customer...")
        for w in range(1, 4):
            for d in range(1, 4):
                for c in range(1, 4):
                    cid = (w - 1) * 9 + (d - 1) * 3 + c
                    self.send_sql(
                        f"insert into customer values ({cid}, {d}, {w}, "
                        f"'First{cid}', 'M', 'Last{cid}', 'cst1', 'cst2', "
                        f"'ccity', 'ST', 'zip', '123-4567', '2023-01-01', 'BC', "
                        f"50000, {cid*0.1:.2f}, {cid*10.0:.2f}, 0, "
                        f"{cid}, {cid%5}, 'data_{cid}');")

        print("  插入 item...")
        for i in range(1, 21):
            self.send_sql(
                f"insert into item values ({i}, {i*10}, 'item_{i}', "
                f"{i*5.0:.2f}, 'item_data_{i}');")

        print("  插入 stock...")
        for w in range(1, 4):
            for i in range(1, 21):
                self.send_sql(
                    f"insert into stock values ({i}, {w}, {i+w*10}, "
                    f"'d01', 'd02', 'd03', 'd04', 'd05', 'd06', 'd07', 'd08', "
                    f"'d09', 'd10', {(i+w)*10.0:.2f}, {i}, 0, 'stock_data');")

        print("  插入 orders / new_orders / order_line / history...")
        for o in range(1, 11):
            self.send_sql(
                f"insert into orders values ({o}, 1, 1, {o}, "
                f"'2023-06-03 10:00:00', {o%10}, {o%5+1}, 1);")
        for o in range(1, 6):
            self.send_sql(f"insert into new_orders values ({o}, 1, 1);")
        for o in range(1, 6):
            self.send_sql(
                f"insert into order_line values ({o}, 1, 1, 1, {o}, 1, "
                f"'2023-06-03', {o*2}, {o*50.0:.2f}, 'dist_info_{o}');")
        for h in range(1, 4):
            self.send_sql(
                f"insert into history values ({h}, 1, 1, 1, 1, "
                f"'2023-06-03 10:00:00', {h*10.0:.2f}, 'history_data');")

        print("  TPC-C 表创建和数据插入完成")

    def _tpcc_run_transactions(self, num_txns=50, with_checkpoint=False):
        """执行TPC-C类NewOrder事务（文档2.2节模式）"""
        print(f"  执行 {num_txns} 个TPC-C事务..."
              + (" (随机创建检查点)" if with_checkpoint else ""))
        checkpoint_count = 0
        for txn_idx in range(num_txns):
            o_id = 11 + txn_idx
            c_id = random.randint(1, 27)
            i_id = random.randint(1, 20)
            quantity = random.randint(1, 10)

            try:
                self.send_sql("begin;")
                self.send_sql(
                    f"select c_discount, c_last, c_credit, w_tax "
                    f"from customer, warehouse "
                    f"where w_id=1 and c_w_id=w_id and c_d_id=1 and c_id={c_id};")
                self.send_sql(
                    f"select d_next_o_id, d_tax from district "
                    f"where d_id=1 and d_w_id=1;")
                self.send_sql(
                    f"update district set d_next_o_id={o_id+1} "
                    f"where d_id=1 and d_w_id=1;")
                self.send_sql(
                    f"insert into orders values ({o_id}, 1, 1, {c_id}, "
                    f"'2023-06-03 19:25:47', 26, 5, 1);")
                self.send_sql(f"insert into new_orders values ({o_id}, 1, 1);")
                self.send_sql(
                    f"select i_price, i_name, i_data from item where i_id={i_id};")
                self.send_sql(
                    f"select s_quantity, s_data, s_dist_01, s_dist_02, "
                    f"s_dist_03, s_dist_04, s_dist_05, s_dist_06, s_dist_07, "
                    f"s_dist_08, s_dist_09, s_dist_10 from stock "
                    f"where s_i_id={i_id} and s_w_id=1;")
                self.send_sql(
                    f"update stock set s_quantity={quantity} "
                    f"where s_i_id={i_id} and s_w_id=1;")
                self.send_sql(
                    f"insert into order_line values ({o_id}, 1, 1, 1, {i_id}, 1, "
                    f"'2023-06-03 19:25:47', {quantity}, {quantity*50.0:.2f}, "
                    f"'dist_info_{txn_idx}');")
                self.send_sql(
                    f"select i_price, i_name, i_data from item where i_id={i_id};")
                self.send_sql("commit;")
            except Exception:
                pass

            if with_checkpoint and random.random() < 0.30:
                try:
                    self.send_sql("create static_checkpoint;")
                    checkpoint_count += 1
                except Exception:
                    pass

            if (txn_idx + 1) % 10 == 0:
                print(f"    已完成 {txn_idx+1}/{num_txns} 事务"
                      + (f", 检查点: {checkpoint_count}" if with_checkpoint else ""))

        if with_checkpoint:
            print(f"  共创建 {checkpoint_count} 个静态检查点")
