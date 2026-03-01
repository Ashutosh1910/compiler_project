run:
	gcc-13 -c driver.c lexer.c logging.c parser.c
	gcc-13 driver.o lexer.o logging.o parser.o -o compiler
	./compiler t6.txt
	rm -f *.o