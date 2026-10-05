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

## Language extensions

Beyond the course language (see `docs/EXTENSIONS_SPEC.md`): the statements
`readchar(b2);` (next input byte, -1 at end of input), `writechar(<expr>);`
(one byte), `print("text", <expr>, ...);` (strings, ints and reals without a
newline) and `exit(<expr>);`; character literals such as `'a'` and `'\n'`
(int constants) and string literals (only as `print` items); one-dimensional
`int`/`real` arrays (`type int[100] : c2;`, locals or globals, elements
`c2[<expr>]` checked against their bounds at run time); recursion and calls
to functions defined later, with a runtime stack guard; and a call statement
in a loop body counts as an update of every global. `toy/` holds a small
compiler written in the extended language (Part B of the specification).

## Tests

    make test                         # or: python3 tests/run_tests.py
    python3 tests/run_tests.py --asan # with AddressSanitizer/UBSan
    python3 tests/run_tests.py -k run # only tests whose name contains "run"

See the docstring of `tests/run_tests.py` for how test programs declare their
expected errors and output.
