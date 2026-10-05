# Writes tests/toy/limits_names.tl and tests/toy/limits_nesting.tl
import sys, os
d = sys.argv[1] if len(sys.argv) > 1 else "tests/toy"
names = ["v%015d" % i for i in range(1, 501)]          # 500 names x 16 bytes
lines = ["#? stdout: 125250", "#? stdout: 499",
         "# exactly 500 distinct variable names, 8000 bytes of names"]
lines += ["%s = %d;" % (n, i) for i, n in enumerate(names, 1)]
lines += ["%s = %s;" % (names[-1], " + ".join(names)),
          "print %s;" % names[-1], "print %s;" % names[-2]]
open(os.path.join(d, "limits_names.tl"), "w").write("\n".join(lines) + "\n")
lines = ["#? stdout: 7", "#? stdout: 7",
         "# nesting depth 200: parentheses, unary minus, blocks",
         "x = " + "(" * 200 + "7" + ")" * 200 + ";",
         "print x;",
         "y = " + "-" * 200 + "x;"]
lines += ["if x == 7 {"] * 200 + ["print y;"] + ["}"] * 200
open(os.path.join(d, "limits_nesting.tl"), "w").write("\n".join(lines) + "\n")
