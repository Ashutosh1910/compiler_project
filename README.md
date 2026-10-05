Made a complete compiler (lexer, LL(1) parser, AST, semantic analysis and
x86-64 NASM code generation) for a toy language (Course Project)
#Group 51
#Ashutosh Desai - 2023A7PS0675P
#Anushka Doshi - 2023A7PS0597P
#Aarya Jain - 2023A7PS0618P
#Devansh Agarwal - 2023A7PS0570P

## Build and run

    make                                  # builds ./compiler
    ./compiler testcase7.txt out.txt      # interactive menu (options 0-8)

Non-interactive flags (exit status 0 = ok, 1 = errors in the source):

    ./compiler --tokens  prog.txt           # token table
    ./compiler --parse   prog.txt tree.txt  # parse tree
    ./compiler --ast     prog.txt           # abstract syntax tree
    ./compiler --symbols prog.txt           # types, variables, offsets
    ./compiler --check   prog.txt           # all lexical/syntax/semantic errors
    ./compiler --asm     prog.txt out.asm   # x86-64 NASM
    ./compiler --build   prog.txt prog      # nasm + gcc -> executable

Running generated programs needs x86-64 Linux with `nasm` and `gcc`.

## Tests

    make test                         # or: python3 tests/run_tests.py
    python3 tests/run_tests.py --asan # with AddressSanitizer/UBSan
    python3 tests/run_tests.py -k run # only tests whose name contains "run"

See the docstring of `tests/run_tests.py` for how test programs declare their
expected errors and output.
