#? stdout: 2
#? stdout: 3
#? stdout: 5
#? stdout: 7
#? stdout: 11
#? stdout: 13
#? stdout: 17
#? stdout: 19
#? stdout: 23
#? stdout: 29
#? stdout: 31
#? stdout: 37
#? stdout: 41
#? stdout: 43
#? stdout: 47
# primes below 50 by trial division (n - n / d * d is n mod d)
n = 2;
while n < 50 {
    d = 2;
    prime = 1;
    while d * d <= n {
        if n - n / d * d == 0 { prime = 0; }
        d = d + 1;
    }
    if prime == 1 { print n; }
    n = n + 1;
}
