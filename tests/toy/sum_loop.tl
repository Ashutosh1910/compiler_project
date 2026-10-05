#? stdout: 5050
#? stdout: 101
# sum of 1..100 with a while loop
i = 1;
sum = 0;
while i <= 100 {
    sum = sum + i;
    i = i + 1;
}
print sum;
print i;
