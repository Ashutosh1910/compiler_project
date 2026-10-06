# first 10 Fibonacci numbers and the primes below 30
a = 0;
b = 1;
n = 0;
while n < 10 {
    print a;
    t = a + b;
    a = b;
    b = t;
    n = n + 1;
}
p = 2;
while p < 30 {
    d = 2;
    prime = 1;
    while d * d <= p {
        if p - p / d * d == 0 { prime = 0; }
        d = d + 1;
    }
    if prime == 1 { print p; }
    p = p + 1;
}
