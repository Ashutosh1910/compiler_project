#? stdout: 20
#? stdout: 1
#? stdout: 3
# a variable assigned inside a loop body is defined after it (the rule is
# textual); nested if/else; empty blocks
i = 0;
while i < 3 {
    j = i * 10;
    i = i + 1;
}
print j;
if i == 3 {
    if j == 20 { print 1; } else { print 2; }
} else {
    print 3;
}
while i < 3 { }
if i > 100 { } else { }
print i;
