"""诊断脚本：TPC-C 完整流程 + 检查点 + crash + 恢复"""
import sys, os, time, shutil, socket, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from tests.base import RMDBTester, DB_PATH, BUILD_DIR, SERVER_PORT

tester = RMDBTester()
tester.stop_server()
if os.path.exists(DB_PATH):
    shutil.rmtree(DB_PATH)
time.sleep(0.5)

tester.start_server()
tester.connect()
tester._tpcc_setup()
tester._tpcc_run_transactions(num_txns=50, with_checkpoint=True)

print("Pre-crash district:")
r = tester.send_sql("select COUNT(*) from district;")
lines = r.replace("|", " ").split("\n")
for l in lines:
    l = l.strip()
    if l and "Total" not in l:
        print(f"  {l}")

tester.send_crash()

# Now recover and read the log
log_path = os.path.join(BUILD_DIR, DB_PATH, "recovery_stderr.log")
ok, t = tester.timed_restart_after_crash()
print(f"Recovery: ok={ok}, t={t:.3f}s")

print("Post-crash district:")
r = tester.send_sql("select COUNT(*) from district;")
lines = r.replace("|", " ").split("\n")
for l in lines:
    l = l.strip()
    if l and "Total" not in l:
        print(f"  {l}")

# Print recovery log
if os.path.exists(log_path):
    print("\n=== Recovery Log ===")
    with open(log_path) as f:
        for line in f:
            if "Analyze" in line or "Redo" in line or "Undo" in line or "skip" in line or "extend" in line:
                print(line.rstrip())

tester.stop_server()
