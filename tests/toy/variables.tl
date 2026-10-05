#? stdout: 20
#? stdout: 25
#? stdout: 46
#? stdout: 42
#? stdout: -1
# assignment, reassignment, identifiers up to 32 characters
x = 5;
y = x * x;
x = y - x;
print x;
print y;
counter_2 = x + y + 1;
print counter_2;
ThisIsAVeryLongIdentifierName_32 = 7;
print ThisIsAVeryLongIdentifierName_32 * 6;
_a = 1; _A = 2;
print _a - _A;
