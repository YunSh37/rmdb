"""题目四：选择运算下推与投影下推
测试点1: 选择运算下推 (EXPLAIN)
测试点2: 投影下推 (EXPLAIN)
测试点3: 连接顺序优化 (EXPLAIN)
测试点4: 稳健性测试 (EXPLAIN)
"""

try:
    from .base import RMDBTester
except ImportError:
    from base import RMDBTester

# ============================================================
# 测试数据
# ============================================================

TOPIC4_TEST1 = [
    ("create table students (stu_id int, stu_name char(20), class_id int, score int);", "SUCCESS"),
    ("create table classes (class_id int, class_name char(30), teacher char(20));", "SUCCESS"),
    ("insert into students values (1, 'anna', 100, 85);", "SUCCESS"),
    ("insert into students values (2, 'ben', 200, 72);", "SUCCESS"),
    ("insert into students values (3, 'carol', 100, 90);", "SUCCESS"),
    ("insert into students values (4, 'david', 300, 95);", "SUCCESS"),
    ("insert into classes values (100, 'math', 'smith');", "SUCCESS"),
    ("insert into classes values (200, 'history', 'lee');", "SUCCESS"),
    ("insert into classes values (300, 'physics', 'smith');", "SUCCESS"),
    ("explain select * from students s join classes c "
     "on s.class_id = c.class_id where s.score > 80 and c.teacher = 'smith';",
     "Project(columns=[*])\n"
     "    Join(tables=[classes,students],condition=[s.class_id=c.class_id])\n"
     "        Filter(condition=[c.teacher='smith'])\n"
     "            Scan(table=classes)\n"
     "        Filter(condition=[s.score>80])\n"
     "            Scan(table=students)"),
]

TOPIC4_TEST2 = [
    ("create table teams (team_id int, team_name char(20), city char(20));", "SUCCESS"),
    ("create table players (player_id int, team_id int, player_name char(20), points int);", "SUCCESS"),
    ("insert into teams values (1, 'Rockets', 'Houston');", "SUCCESS"),
    ("insert into teams values (2, 'Lakers', 'LA');", "SUCCESS"),
    ("insert into players values (101, 1, 'john', 2300);", "SUCCESS"),
    ("insert into players values (102, 1, 'mike', 1800);", "SUCCESS"),
    ("insert into players values (103, 2, 'tony', 2100);", "SUCCESS"),
    ("explain select t.team_name, p.player_name, p.points "
     "from teams t join players p on t.team_id = p.team_id;",
     "Project(columns=[p.player_name,p.points,t.team_name])\n"
     "    Join(tables=[players,teams],condition=[t.team_id=p.team_id])\n"
     "        Project(columns=[t.team_id,t.team_name])\n"
     "            Scan(table=teams)\n"
     "        Project(columns=[p.player_name,p.points,p.team_id])\n"
     "            Scan(table=players)"),
]

TOPIC4_TEST3 = [
    ("create table classes (class_id int, class_name char(30));", "SUCCESS"),
    ("create table students (student_id int, class_id int, student_name char(30));", "SUCCESS"),
    ("create table grades (grade_id int, student_id int, subject char(30), score int);", "SUCCESS"),
    # classes (10行)
    ("insert into classes values (1, 'Mathematics');", "SUCCESS"),
    ("insert into classes values (2, 'History');", "SUCCESS"),
    ("insert into classes values (3, 'Physics');", "SUCCESS"),
    ("insert into classes values (4, 'Chemistry');", "SUCCESS"),
    ("insert into classes values (5, 'Biology');", "SUCCESS"),
    ("insert into classes values (6, 'English');", "SUCCESS"),
    ("insert into classes values (7, 'Art');", "SUCCESS"),
    ("insert into classes values (8, 'PE');", "SUCCESS"),
    ("insert into classes values (9, 'Music');", "SUCCESS"),
    ("insert into classes values (10, 'Computer Science');", "SUCCESS"),
    # students (50行)
    ("insert into students values (1001, 1, 'Student_1');", "SUCCESS"),
    ("insert into students values (1002, 1, 'Student_2');", "SUCCESS"),
    ("insert into students values (1003, 1, 'Student_3');", "SUCCESS"),
    ("insert into students values (1004, 1, 'Student_4');", "SUCCESS"),
    ("insert into students values (1005, 1, 'Student_5');", "SUCCESS"),
    ("insert into students values (1006, 2, 'Student_6');", "SUCCESS"),
    ("insert into students values (1007, 2, 'Student_7');", "SUCCESS"),
    ("insert into students values (1008, 2, 'Student_8');", "SUCCESS"),
    ("insert into students values (1009, 2, 'Student_9');", "SUCCESS"),
    ("insert into students values (1010, 2, 'Student_10');", "SUCCESS"),
    ("insert into students values (1011, 3, 'Student_11');", "SUCCESS"),
    ("insert into students values (1012, 3, 'Student_12');", "SUCCESS"),
    ("insert into students values (1013, 3, 'Student_13');", "SUCCESS"),
    ("insert into students values (1014, 3, 'Student_14');", "SUCCESS"),
    ("insert into students values (1015, 3, 'Student_15');", "SUCCESS"),
    ("insert into students values (1016, 4, 'Student_16');", "SUCCESS"),
    ("insert into students values (1017, 4, 'Student_17');", "SUCCESS"),
    ("insert into students values (1018, 4, 'Student_18');", "SUCCESS"),
    ("insert into students values (1019, 4, 'Student_19');", "SUCCESS"),
    ("insert into students values (1020, 4, 'Student_20');", "SUCCESS"),
    ("insert into students values (1021, 5, 'Student_21');", "SUCCESS"),
    ("insert into students values (1022, 5, 'Student_22');", "SUCCESS"),
    ("insert into students values (1023, 5, 'Student_23');", "SUCCESS"),
    ("insert into students values (1024, 5, 'Student_24');", "SUCCESS"),
    ("insert into students values (1025, 5, 'Student_25');", "SUCCESS"),
    ("insert into students values (1026, 6, 'Student_26');", "SUCCESS"),
    ("insert into students values (1027, 6, 'Student_27');", "SUCCESS"),
    ("insert into students values (1028, 6, 'Student_28');", "SUCCESS"),
    ("insert into students values (1029, 6, 'Student_29');", "SUCCESS"),
    ("insert into students values (1030, 6, 'Student_30');", "SUCCESS"),
    ("insert into students values (1031, 7, 'Student_31');", "SUCCESS"),
    ("insert into students values (1032, 7, 'Student_32');", "SUCCESS"),
    ("insert into students values (1033, 7, 'Student_33');", "SUCCESS"),
    ("insert into students values (1034, 7, 'Student_34');", "SUCCESS"),
    ("insert into students values (1035, 7, 'Student_35');", "SUCCESS"),
    ("insert into students values (1036, 8, 'Student_36');", "SUCCESS"),
    ("insert into students values (1037, 8, 'Student_37');", "SUCCESS"),
    ("insert into students values (1038, 8, 'Student_38');", "SUCCESS"),
    ("insert into students values (1039, 8, 'Student_39');", "SUCCESS"),
    ("insert into students values (1040, 8, 'Student_40');", "SUCCESS"),
    ("insert into students values (1041, 9, 'Student_41');", "SUCCESS"),
    ("insert into students values (1042, 9, 'Student_42');", "SUCCESS"),
    ("insert into students values (1043, 9, 'Student_43');", "SUCCESS"),
    ("insert into students values (1044, 9, 'Student_44');", "SUCCESS"),
    ("insert into students values (1045, 9, 'Student_45');", "SUCCESS"),
    ("insert into students values (1046, 10, 'Student_46');", "SUCCESS"),
    ("insert into students values (1047, 10, 'Student_47');", "SUCCESS"),
    ("insert into students values (1048, 10, 'Student_48');", "SUCCESS"),
    ("insert into students values (1049, 10, 'Student_49');", "SUCCESS"),
    ("insert into students values (1050, 10, 'Student_50');", "SUCCESS"),
    # grades (代表性30条，覆盖不同的student)
    ("insert into grades values (5001, 1001, 'Subject_A', 85);", "SUCCESS"),
    ("insert into grades values (5002, 1001, 'Subject_B', 90);", "SUCCESS"),
    ("insert into grades values (5003, 1001, 'Subject_C', 78);", "SUCCESS"),
    ("insert into grades values (5004, 1002, 'Subject_A', 82);", "SUCCESS"),
    ("insert into grades values (5005, 1002, 'Subject_B', 88);", "SUCCESS"),
    ("insert into grades values (5006, 1002, 'Subject_C', 91);", "SUCCESS"),
    ("insert into grades values (5007, 1003, 'Subject_A', 75);", "SUCCESS"),
    ("insert into grades values (5008, 1003, 'Subject_B', 80);", "SUCCESS"),
    ("insert into grades values (5009, 1003, 'Subject_C', 84);", "SUCCESS"),
    ("insert into grades values (5010, 1004, 'Subject_A', 92);", "SUCCESS"),
    ("insert into grades values (5011, 1004, 'Subject_B', 87);", "SUCCESS"),
    ("insert into grades values (5012, 1004, 'Subject_C', 89);", "SUCCESS"),
    ("insert into grades values (5013, 1005, 'Subject_A', 81);", "SUCCESS"),
    ("insert into grades values (5014, 1005, 'Subject_B', 76);", "SUCCESS"),
    ("insert into grades values (5015, 1005, 'Subject_C', 94);", "SUCCESS"),
    ("insert into grades values (5016, 1006, 'Subject_A', 88);", "SUCCESS"),
    ("insert into grades values (5017, 1006, 'Subject_B', 83);", "SUCCESS"),
    ("insert into grades values (5018, 1006, 'Subject_C', 79);", "SUCCESS"),
    ("insert into grades values (5019, 1007, 'Subject_A', 95);", "SUCCESS"),
    ("insert into grades values (5020, 1007, 'Subject_B', 90);", "SUCCESS"),
    ("insert into grades values (5021, 1007, 'Subject_C', 86);", "SUCCESS"),
    ("insert into grades values (5022, 1008, 'Subject_A', 73);", "SUCCESS"),
    ("insert into grades values (5023, 1008, 'Subject_B', 78);", "SUCCESS"),
    ("insert into grades values (5024, 1008, 'Subject_C', 82);", "SUCCESS"),
    ("insert into grades values (5025, 1009, 'Subject_A', 89);", "SUCCESS"),
    ("insert into grades values (5026, 1009, 'Subject_B', 85);", "SUCCESS"),
    ("insert into grades values (5027, 1009, 'Subject_C', 91);", "SUCCESS"),
    ("insert into grades values (5028, 1010, 'Subject_A', 77);", "SUCCESS"),
    ("insert into grades values (5029, 1010, 'Subject_B', 84);", "SUCCESS"),
    ("insert into grades values (5030, 1010, 'Subject_C', 80);", "SUCCESS"),
    # EXPLAIN 验证连接顺序优化（小表优先：classes(10) < students(50) < grades(150)）
    ("explain select g.grade_id, s.student_name, c.class_name, g.subject, g.score "
     "from grades g join students s on g.student_id = s.student_id "
     "join classes c on s.class_id = c.class_id;",
     "Join(tables=[classes,grades,students],condition=[s.class_id=c.class_id])"),
]

TOPIC4_TEST4 = [
    ("create table authors (author_id int, author_name char(50), country char(30));", "SUCCESS"),
    ("create table books (book_id int, author_id int, title char(100), price float);", "SUCCESS"),
    ("insert into authors values (1, 'Leo Tolstoy', 'Russia');", "SUCCESS"),
    ("insert into authors values (2, 'Ernest Hemingway', 'USA');", "SUCCESS"),
    ("insert into authors values (3, 'Gabriel Garcia Marquez', 'Colombia');", "SUCCESS"),
    ("insert into books values (101, 1, 'War and Peace', 14.99);", "SUCCESS"),
    ("insert into books values (102, 1, 'Anna Karenina', 11.50);", "SUCCESS"),
    ("insert into books values (201, 2, 'The Old Man and the Sea', 13.25);", "SUCCESS"),
    ("insert into books values (202, 2, 'A Farewell to Arms', 9.75);", "SUCCESS"),
    ("insert into books values (301, 3, 'One Hundred Years of Solitude', 15.00);", "SUCCESS"),
    ("insert into books values (302, 3, 'Love in the Time of Cholera', 10.25);", "SUCCESS"),
    ("explain select a.author_name, b.title from authors a join books b "
     "on a.author_id = b.author_id where a.country = 'USA' and b.price > 10.000000;",
     "Project(columns=[a.author_name,b.title])\n"
     "    Join(tables=[authors,books],condition=[a.author_id=b.author_id])\n"
     "        Project(columns=[a.author_id,a.author_name])\n"
     "            Filter(condition=[a.country='USA'])\n"
     "                Scan(table=authors)\n"
     "        Project(columns=[b.author_id,b.title])\n"
     "            Filter(condition=[b.price>10.000000])\n"
     "                Scan(table=books)"),
]


def run(tester: RMDBTester):
    """执行题目四全部测试点"""
    tester.run_tests("题目四 测试点1: 选择运算下推", TOPIC4_TEST1)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目四 测试点2: 投影下推", TOPIC4_TEST2)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目四 测试点3: 连接顺序优化", TOPIC4_TEST3)
    tester.cleanup_leftover_tables()

    tester.run_tests("题目四 测试点4: 稳健性测试", TOPIC4_TEST4)
    tester.cleanup_leftover_tables()


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
