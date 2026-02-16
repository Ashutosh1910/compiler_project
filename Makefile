run:
	gcc -c driver.c lexer.c
	gcc driver.o lexer.o
	./a.out
	rm *.o *.out 