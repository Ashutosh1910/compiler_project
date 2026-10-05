# Group 51
# Ashutosh Desai - 2023A7PS0675P
# Anushka Doshi - 2023A7PS0597P
# Aarya Jain - 2023A7PS0618P
# Devansh Agarwal - 2023A7PS0570P

# gcc-13 is the course compiler; fall back to gcc where it is not installed
CC := $(shell command -v gcc-13 >/dev/null 2>&1 && echo gcc-13 || echo gcc)
CFLAGS ?= -Wall -Wextra -O2
SRCS = driver.c lexer.c logging.c parser.c ast.c symbolTable.c semantic.c \
       codegen.c compiler.c
OBJS = $(SRCS:.c=.o)

build: compiler
	@echo "use ./compiler <inputfile> <outputfile>"

compiler: $(OBJS)
	$(CC) $(OBJS) -o compiler

%.o: %.c *.h
	$(CC) $(CFLAGS) -c $< -o $@

run: compiler
	./compiler testcase3.txt parseTreeOutput.txt

# full test suite (unit tests, lexer, parser, semantic and end-to-end)
test:
	python3 tests/run_tests.py

clean:
	rm -f *.o compiler
	rm -rf tests/build

.PHONY: build run test clean
