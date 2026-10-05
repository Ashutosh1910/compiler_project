#? stdout: 3
#? stdout: runtime error: division by zero
#? exit: 1
# division by zero stops the compiled program
x = 10;
print x / 3;
y = x - 10;
print x / y;
print 5;
