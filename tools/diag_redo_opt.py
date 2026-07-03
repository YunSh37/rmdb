import sys, os, time, socket, subprocess, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tests.base import BUILD_DIR

DB = "test_redo_db"
db_path = os.path.join(BUILD_DIR, DB)
if os.path.exists(db_path): shutil.rmtree(db_path)

# Start server
os.chdir(BUILD_DIR)
proc = subprocess.Popen(["./bin/rmdb", DB], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
os.chdir("..")
time.sleep(0.5)

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(10); s.connect(("127.0.0.1", 8765))
def send(sql):
    s.sendall((sql+";").encode()+b'\0'); time.sleep(0.08)
    return s.recv(8192).rstrip(b'\0').decode()

send("create table dt (id int, val int)")
for i in range(1, 10): send(f"insert into dt values ({i}, {i*100})")
for txn in range(10):
    send("begin;")
    send(f"update dt set val = {txn*1000} where id = {txn%9+1}")
    send("commit;")
    if txn == 4:
        send("create static_checkpoint;")
        time.sleep(0.15)

print("PRE-CRASH count:", send("select COUNT(*) from dt;").split("\n")[-2].strip())
s.close()
proc.kill()
proc.wait(timeout=3)
time.sleep(0.3)

# Read server output
server_out = proc.stdout.read().decode(errors='replace')
for line in server_out.split("\n"):
    if "Analyze" in line or "Redo" in line or "REDO" in line:
        print("S1:", line.rstrip())

# Recover
os.chdir(BUILD_DIR)
proc2 = subprocess.Popen(["./bin/rmdb", DB], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
os.chdir("..")
time.sleep(1.0)

s2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s2.settimeout(5); s2.connect(("127.0.0.1", 8765))
time.sleep(0.1)
r = s2.recv(8192).decode()
print("POST count:", send("select COUNT(*) from dt;").split("\n")[-2].strip())
s2.close()

server_out2 = proc2.stdout.read().decode(errors='replace')
for line in server_out2.split("\n"):
    if "Analyze" in line or "Redo" in line or "REDO" in line:
        print("S2:", line.rstrip())

proc2.kill()
proc2.wait(timeout=3)
