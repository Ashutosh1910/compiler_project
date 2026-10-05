# Writes tests/semantic/depth_new_roots.txt
import sys, os
d = sys.argv[1] if len(sys.argv) > 1 else "tests/semantic"
E = "1" + " + 1" * 1001                     # 1001 operators deep
M = " %? error: expression is too long or too deeply nested"
lines = ["% The 1000-operator depth limit applies to every new expression root.",
         "_main",
         "\ttype int[2] : b2;",
         "\ttype int : b3;",
         "\tb2[" + E + "] <--- 1;" + M,
         "\tif (b2[" + E + "] < 1) then" + M,
         "\t\twrite(b3);",
         "\tendif",
         "\tread(b2[" + E + "]);" + M,
         "\twrite(b2[" + E + "]);" + M,
         "\treadchar(b2[" + E + "]);" + M,
         "\tprint(\"x\", " + E + ");" + M,
         "\twritechar(" + E + ");" + M,
         "\texit(" + E + ");" + M,
         "\treturn;",
         "end"]
open(os.path.join(d, "depth_new_roots.txt"), "w").write("\n".join(lines) + "\n")
