#? stdout: 7
#? stdout: 9
#? stdout: 3
#? stdout: 3
#? stdout: 23
#? stdout: 3
#? stdout: -3
#? stdout: -3
#? stdout: 3
#? stdout: 4
#? stdout: 5
#? stdout: 4
#? stdout: 1
# precedence, left associativity, parentheses, unary minus,
# division truncating toward zero
print 1 + 2 * 3;
print (1 + 2) * 3;
print 10 - 4 - 3;
print 100 / 10 / 3;
print 2 * 3 + 4 * 5 - 6 / 2;
print 7 / 2;
print -7 / 2;
print 7 / -2;
print -7 / -2;
print -(3 - 5) * 2;
print 2 - -3;
print --4;
print ((((1))));
