#? stdout: 120
#? stdout: 3628800
#? stdout: 2432902008176640000
# 20! is the largest factorial that fits in 64 bits
n = 1;
f = 1;
while n <= 20 {
    f = f * n;
    if n == 5 { print f; }
    if n == 10 { print f; }
    n = n + 1;
}
print f;
