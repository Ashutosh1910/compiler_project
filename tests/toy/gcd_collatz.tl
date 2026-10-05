#? stdout: 21
#? stdout: 111
#? stdout: 9232
# Euclid's algorithm, then the Collatz sequence of 27 (steps, peak)
a = 1071; b = 462;
while b != 0 {
    t = a - a / b * b;
    a = b;
    b = t;
}
print a;
n = 27; steps = 0; peak = n;
while n != 1 {
    if n - n / 2 * 2 == 0 { n = n / 2; } else { n = 3 * n + 1; }
    if n > peak { peak = n; }
    steps = steps + 1;
}
print steps;
print peak;
