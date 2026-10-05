#? stdout: -9223372036854775808
#? stdout: -9223372036854775808
#? stdout: 9223372036854775807
#? stdout: -9223372036854775808
#? stdout: -9223372036854775808
#? stdout: -9223372036854775808
#? stdout: -2
# 64-bit two's complement arithmetic wraps; MIN / -1 is MIN
max = 9223372036854775807;
min = -max - 1;
print min;
print max + 1;
print min - 1;
print min / -1;
print min * -1;
print -min;
print max * 2;
