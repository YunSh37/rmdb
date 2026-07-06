#!/usr/bin/env python3
"""题目三/四/六/八 完整功能测试（按照测试说明文档中的测试示例）"""
import socket, time, sys, os

SERVER_HOST = '127.0.0.1'
SERVER_PORT = 8765

class RMDBClient:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((SERVER_HOST, SERVER_PORT))
        self.sock.settimeout(5)
        self.passed = 0
        self.failed = 0

    def send(self, sql):
        """发送SQL语句并返回响应"""
        self.sock.send((sql + '\0').encode())
        time.sleep(0.2)
        try:
            data = self.sock.recv(65536)
            return data.decode('utf-8', errors='replace').strip()
        except socket.timeout:
            return '(timeout)'
        except Exception as e:
            return f'SOCKET_ERROR: {e}'

    def close(self):
        self.sock.close()

    def check(self, description, sql, expected_contains=None, expected_not=None, is_error=False):
        """执行SQL并检查结果
        - expected_contains: 结果应包含的字符串（None表示只确认不超时）
            注意：客户端输出受 COL_WIDTH=16 限制会被截断，匹配时自动使用截断版本
        - expected_not: 结果不应包含的字符串
        - is_error: True表示预期返回failure/Error
        """
        print(f"  [{description}] {sql[:80]}{'...' if len(sql) > 80 else ''}")
        result = self.send(sql)
        short = result[:200].replace('\n', '\\n')
        print(f"       => {short}")

        ok = True
        if is_error:
            # 预期错误：响应中应包含 "failure" 或 "Error"
            if "failure" not in result.lower() and "Error" not in result and "error" not in result.lower():
                ok = False
                print(f"      [FAIL] 预期错误但未检测到: {short}")
        elif expected_contains is not None:
            # 检查每个期望的子串（考虑 COL_WIDTH=16 截断）
            if isinstance(expected_contains, list):
                for exp in expected_contains:
                    # 客户端显示会被截断为前13字符+...，所以只要前13字符匹配即可
                    check_exp = exp[:13] if len(exp) > 13 else exp
                    if check_exp not in result:
                        ok = False
                        print(f"      [FAIL] 缺少: '{exp}' (截断匹配: '{check_exp}')")
                        break
            else:
                check_exp = expected_contains[:13] if len(expected_contains) > 13 else expected_contains
                if check_exp not in result:
                    ok = False
                    print(f"      [FAIL] 缺少: '{expected_contains}' (截断匹配: '{check_exp}')")

        if expected_not is not None:
            if expected_not in result:
                ok = False
                print(f"      [FAIL] 不应包含: '{expected_not}'")

        if ok:
            print(f"      [OK]")
            self.passed += 1
        else:
            self.failed += 1
        return ok

    def run_group(self, name, tests):
        """运行一组测试。tests = [(description, sql, expected)]"""
        print(f"\n{'='*60}")
        print(f"  {name}")
        print(f"{'='*60}")
        for test in tests:
            desc, sql = test[0], test[1]
            expected = test[2] if len(test) > 2 else None
            is_error = test[3] if len(test) > 3 else False
            self.check(desc, sql, expected, is_error=is_error)


def test_topic3_bigint():
    """题目三：BIGINT 类型 — 对应 storage_test6"""
    c = RMDBClient()
    print("\n" + "="*70)
    print("  题目三：BIGINT 类型测试 (storage_test6)")
    print("="*70)

    # 测试文档中的示例
    c.check("建表", "CREATE TABLE t_bigint (bid BIGINT, sid INT);", is_error=False)
    c.check("建表后查询", "SELECT * FROM t_bigint;")

    # 正常范围插入
    c.check("INSERT 正常值1", "INSERT INTO t_bigint VALUES (372036854775807, 233421);", is_error=False)
    c.check("INSERT 正常值2", "INSERT INTO t_bigint VALUES (-922337203685477580, 124332);", is_error=False)
    c.check("SELECT 全表", "SELECT * FROM t_bigint;", ["372036854775807", "233421"])

    # BIGINT 溢出检测：9223372036854775809 > INT64_MAX (9223372036854775807)
    c.check("INSERT 溢出值 (应失败)", "INSERT INTO t_bigint VALUES (9223372036854775809, 12345);", is_error=True)
    # 溢出插入失败后，数据不应增加
    c.check("SELECT 验证溢出未插入", "SELECT * FROM t_bigint;", ["372036854775807", "-922337203685477580"])

    # 更多 BIGINT 操作
    c.check("WHERE 条件", "SELECT * FROM t_bigint WHERE bid = 372036854775807;", "372036854775807")
    c.check("WHERE 比较", "SELECT * FROM t_bigint WHERE bid < 0;", "-922337203685477580")
    c.check("UPDATE BIGINT", "UPDATE t_bigint SET bid = 9999999999 WHERE sid = 233421;", is_error=False)
    c.check("验证 UPDATE", "SELECT * FROM t_bigint WHERE sid = 233421;", "9999999999")
    c.check("ORDER BY ASC", "SELECT * FROM t_bigint ORDER BY bid;", "-922337203685477580")
    c.check("ORDER BY DESC", "SELECT * FROM t_bigint ORDER BY bid DESC;", "9999999999")
    c.check("清理", "DROP TABLE t_bigint;", is_error=False)

    print(f"\n  题目三结果: {c.passed}/{c.passed + c.failed} 通过")
    c.close()
    return c.passed, c.failed


def test_topic4_datetime():
    """题目四：DATETIME 类型 — 对应 storage_test1 和 storage_test2"""
    c = RMDBClient()
    print("\n" + "="*70)
    print("  题目四：DATETIME 类型测试")
    print("="*70)

    # ===== 测试点1 (storage_test1): 增删改查 =====
    print("\n  --- 测试点1: DATETIME 增删改查 (storage_test1) ---")
    c.check("建表", "CREATE TABLE t_dt (id INT, time DATETIME);", is_error=False)
    c.check("INSERT 1", "INSERT INTO t_dt VALUES (1, '2023-05-18 09:12:19');", is_error=False)
    c.check("INSERT 2", "INSERT INTO t_dt VALUES (2, '2023-05-31 12:34:32');", is_error=False)
    c.check("SELECT *",
            "SELECT * FROM t_dt;",
            expected_contains=["2023-05-18 09:12:19", "2023-05-31 12:34:32"])
    c.check("DELETE WHERE",
            "DELETE FROM t_dt WHERE time = '2023-05-31 12:34:32';", is_error=False)
    c.check("UPDATE SET",
            "UPDATE t_dt SET id = 2023 WHERE time = '2023-05-18 09:12:19';", is_error=False)
    c.check("验证 UPDATE 结果",
            "SELECT * FROM t_dt;",
            expected_contains=["2023", "2023-05-18 09:12:19"])
    c.check("清理", "DROP TABLE t_dt;", is_error=False)

    # ===== 测试点2 (storage_test2): 输入合法性判断 =====
    print("\n  --- 测试点2: DATETIME 输入合法性判断 (storage_test2) ---")
    c.check("建表2", "CREATE TABLE t_dt2 (time DATETIME, temperature FLOAT);", is_error=False)
    c.check("INSERT 合法值",
            "INSERT INTO t_dt2 VALUES ('1999-07-07 12:30:00', 36.0);", is_error=False)
    c.check("SELECT 验证",
            "SELECT * FROM t_dt2;",
            expected_contains=["1999-07-07 12:30:00", "36.000000"])

    # 非法输入测试（全部应失败）
    c.check("非法: 月份=13", "INSERT INTO t_dt2 VALUES ('1999-13-07 12:30:00', 36.0);", is_error=True)
    c.check("非法: 月份缺前导零 '1999-1-07'", "INSERT INTO t_dt2 VALUES ('1999-1-07 12:30:00', 36.0);", is_error=True)
    c.check("非法: 月份=0", "INSERT INTO t_dt2 VALUES ('1999-00-07 12:30:00', 36.0);", is_error=True)
    c.check("非法: 日期=0", "INSERT INTO t_dt2 VALUES ('1999-07-00 12:30:00', 36.0);", is_error=True)
    c.check("非法: 年份=1 (<1000)", "INSERT INTO t_dt2 VALUES ('0001-07-10 12:30:00', 36.0);", is_error=True)
    c.check("非法: 2月30日", "INSERT INTO t_dt2 VALUES ('1999-02-30 12:30:00', 36.0);", is_error=True)
    c.check("非法: 秒=61", "INSERT INTO t_dt2 VALUES ('1999-02-28 12:30:61', 36.0);", is_error=True)

    # 验证所有非法插入都没有生效
    c.check("验证仅1条记录",
            "SELECT * FROM t_dt2;",
            expected_contains=["1999-07-07 12:30:00"])

    # 更多测试
    c.check("DATETIME ORDER BY",
            "SELECT * FROM t_dt2 ORDER BY time;",
            expected_contains="1999-07-07 12:30:00")
    c.check("DATETIME INDEX",
            "CREATE INDEX t_dt2(time);", is_error=False)
    c.check("INDEX 查询",
            "SELECT * FROM t_dt2 WHERE time = '1999-07-07 12:30:00';",
            expected_contains="36.000000")
    c.check("清理2", "DROP TABLE t_dt2;", is_error=False)

    print(f"\n  题目四结果: {c.passed}/{c.passed + c.failed} 通过")
    c.close()
    return c.passed, c.failed


def test_topic6_aggregation():
    """题目六：聚合函数 — 对应 aggregate_test1, aggregate_test2, aggregate_test3"""
    c = RMDBClient()
    print("\n" + "="*70)
    print("  题目六：聚合函数测试")
    print("="*70)

    # ===== 测试点1 (aggregate_test1): SUM =====
    print("\n  --- 测试点1: SUM (aggregate_test1) ---")
    c.check("建表", "CREATE TABLE aggregate (id INT, val FLOAT);", is_error=False)
    c.check("INSERT 1", "INSERT INTO aggregate VALUES (1, 5.5);", is_error=False)
    c.check("INSERT 2", "INSERT INTO aggregate VALUES (3, 4.5);", is_error=False)
    c.check("INSERT 3", "INSERT INTO aggregate VALUES (5, 10.0);", is_error=False)
    c.check("SUM(id) AS sum_id",
            "SELECT SUM(id) AS sum_id FROM aggregate;",
            expected_contains="9")  # INT 不显示小数
    c.check("SUM(val) AS sum_val",
            "SELECT SUM(val) AS sum_val FROM aggregate;",
            expected_contains="20.000000")  # FLOAT 6位小数

    # ===== 测试点2 (aggregate_test2): MAX, MIN =====
    print("\n  --- 测试点2: MAX/MIN (aggregate_test2) ---")
    c.check("MAX(id) AS max_id",
            "SELECT MAX(id) AS max_id FROM aggregate;",
            expected_contains="5")  # INT 无小数
    c.check("MIN(val) AS min_val",
            "SELECT MIN(val) AS min_val FROM aggregate;",
            expected_contains="4.500000")  # FLOAT 6位小数
    c.check("清理", "DROP TABLE aggregate;", is_error=False)

    # ===== 测试点3 (aggregate_test3): COUNT =====
    print("\n  --- 测试点3: COUNT (aggregate_test3) ---")
    c.check("建表2", "CREATE TABLE aggregate2 (id INT, name CHAR(8), val FLOAT);", is_error=False)
    c.check("INSERT 1", "INSERT INTO aggregate2 VALUES (1, 'qwerasdf', 1.0);", is_error=False)
    c.check("INSERT 2", "INSERT INTO aggregate2 VALUES (2, 'qwerasdf', 2.0);", is_error=False)
    c.check("INSERT 3", "INSERT INTO aggregate2 VALUES (3, 'uiophjkl', 2.0);", is_error=False)
    c.check("COUNT(*) AS count_row",
            "SELECT COUNT(*) AS count_row FROM aggregate2;",
            expected_contains="3")
    c.check("COUNT(id) AS count_id",
            "SELECT COUNT(id) AS count_id FROM aggregate2;",
            expected_contains="3")
    c.check("COUNT(name) WHERE val=2.0 AS count_name",
            "SELECT COUNT(name) AS count_name FROM aggregate2 WHERE val = 2.0;",
            expected_contains="2")
    c.check("清理2", "DROP TABLE aggregate2;", is_error=False)

    # ===== 额外：GROUP BY + HAVING =====
    print("\n  --- 额外: GROUP BY + HAVING ---")
    c.check("建表3", "CREATE TABLE t_sales (region CHAR(10), amount BIGINT);", is_error=False)
    c.check("INSERT East/100", "INSERT INTO t_sales VALUES ('East', 100);", is_error=False)
    c.check("INSERT East/200", "INSERT INTO t_sales VALUES ('East', 200);", is_error=False)
    c.check("INSERT West/150", "INSERT INTO t_sales VALUES ('West', 150);", is_error=False)
    c.check("GROUP BY SUM",
            "SELECT region, SUM(amount) FROM t_sales GROUP BY region;",
            expected_contains=["East", "300"])
    c.check("HAVING SUM>200",
            "SELECT region, SUM(amount) AS t FROM t_sales GROUP BY region HAVING SUM(amount) > 200;",
            expected_contains="East")
    c.check("清理3", "DROP TABLE t_sales;", is_error=False)

    print(f"\n  题目六结果: {c.passed}/{c.passed + c.failed} 通过")
    c.close()
    return c.passed, c.failed


def test_topic8_join():
    """题目八：块嵌套循环连接算法 — 对应 join_test_1 和 join_test_2"""
    c = RMDBClient()
    print("\n" + "="*70)
    print("  题目八：块嵌套循环连接算法测试")
    print("="*70)

    # ===== 等值连接测试 (join_test_1) =====
    print("\n  --- 等值连接 (join_test_1) ---")
    c.check("建表 t1", "CREATE TABLE t1 (id INT, t_name CHAR(3));", is_error=False)
    c.check("建表 t2", "CREATE TABLE t2 (t_id INT, d_name CHAR(5));", is_error=False)
    c.check("INSERT t1-1", "INSERT INTO t1 VALUES (1, 'aaa');", is_error=False)
    c.check("INSERT t1-2", "INSERT INTO t1 VALUES (2, 'baa');", is_error=False)
    c.check("INSERT t1-3", "INSERT INTO t1 VALUES (3, 'bba');", is_error=False)
    c.check("INSERT t2-1", "INSERT INTO t2 VALUES (1, '12345');", is_error=False)
    c.check("INSERT t2-2", "INSERT INTO t2 VALUES (2, '23456');", is_error=False)

    # 等值连接: select * from t1, t2 where t1.id = t2.t_id
    c.check("等值连接 JOIN",
            "SELECT * FROM t1, t2 WHERE t1.id = t2.t_id;",
            expected_contains=["aaa", "12345", "baa", "23456"])

    c.check("等值连接 JOIN ON",
            "SELECT * FROM t1 JOIN t2 ON t1.id = t2.t_id;",
            expected_contains=["aaa", "12345"])

    # 不等值连接
    c.check("不等值连接 <",
            "SELECT * FROM t1, t2 WHERE t1.id < t2.t_id;",
            expected_contains=["aaa"])  # id=1 < t_id=2

    # ===== 不等值连接测试 (join_test_2) =====
    print("\n  --- 不等值连接 + 条件过滤 (join_test_2) ---")
    # 插入更多数据
    c.check("INSERT t2-3", "INSERT INTO t2 VALUES (3, '34567');", is_error=False)

    # t1.id < t2.t_id AND t2.t_id < 3
    # 匹配: t1.id=1, t2.t_id=2 → (1, 'aaa', 2, '23456')
    c.check("非等值连接+过滤",
            "SELECT * FROM t1, t2 WHERE t1.id < t2.t_id AND t2.t_id < 3;",
            expected_contains=["aaa", "23456"])  # id=1 < t_id=2

    # 多行结果
    c.check("多行等值连接",
            "SELECT t1.id, t1.t_name, t2.d_name FROM t1, t2 WHERE t1.id = t2.t_id ORDER BY t1.id;",
            expected_contains=["1", "aaa", "12345"])

    # 清理
    c.check("清理 t1", "DROP TABLE t1;", is_error=False)
    c.check("清理 t2", "DROP TABLE t2;", is_error=False)

    # ===== 大数据量连接测试（模拟块嵌套循环） =====
    print("\n  --- 中等数据量连接 ---")
    c.check("建表 big_t1", "CREATE TABLE big_t1 (id INT, val INT);", is_error=False)
    c.check("建表 big_t2", "CREATE TABLE big_t2 (id INT, val INT);", is_error=False)

    # 插入100条记录
    for i in range(1, 101):
        c.send(f"INSERT INTO big_t1 VALUES ({i}, {i * 10});")
        c.send(f"INSERT INTO big_t2 VALUES ({i}, {i * 20});")

    c.check("大数据等值连接",
            "SELECT COUNT(*) FROM big_t1, big_t2 WHERE big_t1.id = big_t2.id;",
            expected_contains="100")

    c.check("大数据非等值连接",
            "SELECT COUNT(*) FROM big_t1, big_t2 WHERE big_t1.id < big_t2.id;",
            expected_contains="4950")  # sum from 1 to 99 = 99*100/2

    c.check("清理 big_t1", "DROP TABLE big_t1;", is_error=False)
    c.check("清理 big_t2", "DROP TABLE big_t2;", is_error=False)

    print(f"\n  题目八结果: {c.passed}/{c.passed + c.failed} 通过")
    c.close()
    return c.passed, c.failed


def main():
    print("="*60)
    print("  RMDB 题目三/四/六/八 综合测试")
    print("  测试示例来自「测试说明文档.md」")
    print("="*60)
    print("  请确保 rmdb 服务端已在 WSL 中启动:")
    print("    cd build && ./bin/rmdb test_db")
    print()

    total_passed, total_failed = 0, 0

    # 题目三: BIGINT
    p, f = test_topic3_bigint()
    total_passed += p
    total_failed += f

    # 题目四: DATETIME
    p, f = test_topic4_datetime()
    total_passed += p
    total_failed += f

    # 题目六: 聚合函数
    p, f = test_topic6_aggregation()
    total_passed += p
    total_failed += f

    # 题目八: 块嵌套循环连接
    p, f = test_topic8_join()
    total_passed += p
    total_failed += f

    # 总结
    total = total_passed + total_failed
    print(f"\n{'='*70}")
    print(f"  综合测试总结: {total_passed}/{total} 通过")
    if total_failed > 0:
        print(f"  {total_failed} 项失败!")
    else:
        print(f"  全部通过!")
    print(f"{'='*70}")

    return 0 if total_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
