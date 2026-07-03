"""诊断脚本：检查 checkpoint 恢复后的数据状态"""
import sys, os, time, shutil, socket
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tests.base import RMDBTester, DB_PATH, BUILD_DIR

tester = RMDBTester()

# 清理并启动新环境
tester.stop_server()
if os.path.exists(DB_PATH):
    shutil.rmtree(DB_PATH)
time.sleep(0.5)

tester.start_server()
if not tester.connect():
    print("FAIL: cannot connect")
    sys.exit(1)

# TPC-C 初始数据
tester._tpcc_setup()
tester._tpcc_run_transactions(num_txns=5, with_checkpoint=True)

print("\n=== Crash前数据 ===")
r = tester.send_sql("select COUNT(*) from warehouse;")
print(f"warehouse: {r}")
r = tester.send_sql("select COUNT(*) from district;")
print(f"district: {r}")
r = tester.send_sql("select * from warehouse;")
print(f"warehouse detail: {r}")

print("\n发送 crash...")
tester.send_crash()

print("\n计时重启...")
ok, t = tester.timed_restart_after_crash()
print(f"恢复: {'成功' if ok else '失败'}, 时间={t:.3f}s")

print("\n=== Crash后数据 ===")
r = tester.send_sql("select COUNT(*) from warehouse;")
print(f"warehouse: >>{r}<<")
r = tester.send_sql("select * from warehouse;")
print(f"warehouse detail: >>{r}<<")
r = tester.send_sql("select COUNT(*) from district;")
print(f"district: >>{r}<<")
r = tester.send_sql("select * from district;")
print(f"district detail: >>{r}<<")

tester.stop_server()
