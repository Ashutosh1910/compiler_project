#? stdout: 0
# every variable starts at 0; y is defined (textually) but never assigned
while 0 > 1 { y = 5; }
print y;
