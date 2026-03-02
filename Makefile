#Ashutosh Desai - 2023A7PS0675P
#Anushka Doshi - 2023A7PS0597P
#Aarya Jain - 2023A7PS0618P
#Devansh Agarwal - 2023A7PS0570P
build:
	gcc-13 -c driver.c lexer.c logging.c parser.c
	gcc-13 driver.o lexer.o logging.o parser.o -o compiler
	echo "use ./compiler <inputfile> <outputfile>"

run:
	gcc-13 -c driver.c lexer.c logging.c parser.c
	gcc-13 driver.o lexer.o logging.o parser.o -o compiler
	./compiler t6.txt parseTreeOutput.txt
	rm -f *.o

clean:
	rm -f *.o
