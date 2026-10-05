#? stdout: 1
#? stdout: 0
#? stdout: 1
#? stdout: 0
#? stdout: 1
#? stdout: 0
#? stdout: 1
#? stdout: 0
#? stdout: 1
#? stdout: 0
#? stdout: 1
#? stdout: 0
#? stdout: 7
#? stdout: 8
#? stdout: 10
# every comparison operator, true and false; if with and without else
a = 3; b = 5;
if a == 3 { print 1; } else { print 0; }
if a == b { print 1; } else { print 0; }
if a != b { print 1; } else { print 0; }
if a != 3 { print 1; } else { print 0; }
if a < b { print 1; } else { print 0; }
if b < a { print 1; } else { print 0; }
if a <= 3 { print 1; } else { print 0; }
if b <= a { print 1; } else { print 0; }
if b > a { print 1; } else { print 0; }
if a > a { print 1; } else { print 0; }
if a >= 3 { print 1; } else { print 0; }
if a >= b { print 1; } else { print 0; }
if a - 10 < 0 { print 7; }
if a * 2 == b + 1 { print 8; }
if 0 - 1 < -1 { print 9; }
print 10;
