run:
	gcc -c driver.c lexer.c logging.c
	gcc driver.o lexer.o logging.o
	./a.out no-error.txt
	rm *.o *.out 