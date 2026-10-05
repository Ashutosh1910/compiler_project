#? stdout: 0
#? stdout: 1
#? stdout: 1
#? stdout: 2
#? stdout: 3
#? stdout: 5
#? stdout: 8
#? stdout: 13
#? stdout: 21
#? stdout: 34
#? stdout: 2880067194370816120
# the first ten Fibonacci numbers, then F(90)
a = 0;
b = 1;
k = 0;
while k < 10 {
    print a;
    t = a + b;
    a = b;
    b = t;
    k = k + 1;
}
while k < 90 {
    t = a + b;
    a = b;
    b = t;
    k = k + 1;
}
print a;
