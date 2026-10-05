#!/usr/bin/env python3
"""Test suite for the Group 51 compiler (lexer, parser, AST, semantic analysis,
code generation and the driver).

    python3 tests/run_tests.py            # everything
    python3 tests/run_tests.py -k lexer   # only tests whose name contains "lexer"
    python3 tests/run_tests.py --asan     # use an AddressSanitizer build
    python3 tests/run_tests.py -v         # list every test

Test programs carry their expectations in comments that start with "%?"
(the lexer skips comments, so they never affect compilation):

    %? error: <text>      a semantic error containing <text> is reported on
                          THIS line (several "%?" may share a line)
    %? syntax-error       a syntax error is reported on this line
    %? lex-error: <text>  a lexical error containing <text> on this line
    %? ok                 the program has no errors at all
    %? stdin: <text>      a line fed to the compiled program
    %? stdout: <text>     an expected line of program output, in order
    %? exit: <n>          expected exit status of the program (default 0)

Directories:
    tests/unit/       C unit tests of internal functions
    tests/syntax/     programs with lexical / syntax errors
    tests/semantic/   programs with semantic errors (or "%? ok")
    tests/programs/   programs that are compiled to x86-64, run and checked
"""
import argparse
import concurrent.futures
import os
import platform
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = os.path.join(ROOT, "tests")
BUILD = os.path.join(TESTS, "build")
SOURCES = ["lexer.c", "logging.c", "parser.c", "ast.c", "symbolTable.c",
           "semantic.c", "codegen.c", "compiler.c"]
CC = "gcc-13" if shutil.which("gcc-13") else "gcc"
COMPILER = os.path.join(ROOT, "compiler")
SANITIZE = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]

# Native execution needs x86-64 Linux with nasm and gcc.
CAN_RUN = (platform.system() == "Linux" and platform.machine() in
           ("x86_64", "AMD64") and shutil.which("nasm") is not None)


# --------------------------------------------------------------- framework

class Failure(Exception):
    pass


def expect(cond, message):
    if not cond:
        raise Failure(message)


class Suite:
    def __init__(self, pattern, verbose):
        self.pattern = pattern
        self.verbose = verbose
        self.passed = 0
        self.failed = []
        self.skipped = []
        self.pending = []  # (name, callable) collected, then run

    def add(self, name, fn):
        if self.pattern and self.pattern not in name:
            return
        self.pending.append((name, fn))

    def skip(self, name, reason):
        if self.pattern and self.pattern not in name:
            return
        self.skipped.append((name, reason))

    def run(self, jobs):
        def one(item):
            name, fn = item
            try:
                fn()
                return name, None
            except Failure as e:
                return name, str(e)
            except subprocess.TimeoutExpired as e:
                return name, "timed out: %s" % (e.cmd,)
            except Exception as e:  # a bug in the test itself
                return name, "%s: %s" % (type(e).__name__, e)

        with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
            for name, err in pool.map(one, self.pending):
                if err is None:
                    self.passed += 1
                    if self.verbose:
                        print("  ok    %s" % name)
                else:
                    self.failed.append((name, err))
                    print("  FAIL  %s\n        %s" %
                          (name, err.replace("\n", "\n        ")))


def run(args, stdin=None, timeout=20, cwd=ROOT):
    p = subprocess.run(args, input=stdin, capture_output=True, text=True,
                       timeout=timeout, cwd=cwd, errors="replace")
    if "Sanitizer" in p.stderr or "runtime error:" in p.stderr:
        raise Failure("sanitizer report from %s:\n%s" %
                      (" ".join(args), p.stderr[-2000:]))
    return p


def compiler(*args, stdin=None, timeout=20, cwd=ROOT):
    return run([COMPILER] + list(args), stdin=stdin, timeout=timeout, cwd=cwd)


_counter = [0]


def scratch(name, text):
    """Writes text to a fresh file under tests/build and returns its path."""
    _counter[0] += 1
    path = os.path.join(BUILD, "src", "%d_%s.txt" % (_counter[0], name))
    with open(path, "w") as f:
        f.write(text)
    return path


# ----------------------------------------------------------- output parsing

TOKEN_ROW = re.compile(r"^\| (\d+)\s+\| (TK_\w+)\s+\| (.*?)\s*\|$")
LEX_ERR = re.compile(r"^\[LEXER-ERROR\] at line (\d+): (.*)$")
SYN_ERR = re.compile(r"^\[SYNTAX-ERROR\] at line (-?\d+): (.*)$")
SEM_ERR = re.compile(r"^\[SEMANTIC-ERROR\] at line (\d+): (.*)$")


def matches(regex, text):
    return [m.groups() for m in map(regex.match, text.splitlines()) if m]


def tokens_of(text):
    """Returns ([(line, type, lexeme)], [(line, message)]) for source text."""
    out = compiler("--tokens", scratch("lex", text)).stdout
    toks = [(int(l), t, "" if lx == "-" else lx)
            for l, t, lx in matches(TOKEN_ROW, out)]
    errs = [(int(l), m) for l, m in matches(LEX_ERR, out)]
    return toks, errs


def annotations(path):
    """Parses the %? directives of a test program."""
    ann = {"error": [], "syntax-error": [], "lex-error": [], "stdin": [],
           "stdout": [], "exit": 0, "ok": False}
    with open(path) as f:
        for lineno, line in enumerate(f, 1):
            for part in line.split("%?")[1:]:
                key, _, value = part.strip().partition(":")
                key, value = key.strip(), value.strip()
                if key in ("error", "lex-error"):
                    ann[key].append((lineno, value))
                elif key == "syntax-error":
                    ann[key].append(lineno)
                elif key == "stdin":
                    ann["stdin"].append(value)
                elif key == "stdout":
                    ann["stdout"].append(value)
                elif key == "exit":
                    ann["exit"] = int(value)
                elif key == "ok":
                    ann["ok"] = True
                else:
                    raise Failure("%s:%d: unknown directive %r" %
                                  (path, lineno, key))
    return ann


def match_errors(kind, expected, actual):
    """Every expected (line, text) must match a distinct reported error and
    no reported error may be left over."""
    left = list(actual)
    missing = []
    for line, text in expected:
        for i, (aline, amsg) in enumerate(left):
            if aline == line and text in amsg:
                del left[i]
                break
        else:
            missing.append((line, text))
    msg = []
    if missing:
        msg.append("expected %s not reported:\n" % kind + "\n".join(
            "  line %d: %s" % m for m in missing))
    if left:
        msg.append("unexpected %s:\n" % kind + "\n".join(
            "  line %d: %s" % a for a in left))
    expect(not msg, "\n".join(msg))


# ------------------------------------------------------------------- build

def build(asan):
    os.makedirs(os.path.join(BUILD, "src"), exist_ok=True)
    os.makedirs(os.path.join(BUILD, "bin"), exist_ok=True)
    global COMPILER
    flags = ["-Wall", "-Wextra"] + (SANITIZE if asan else ["-O2"])
    COMPILER = os.path.join(BUILD, "compiler-asan" if asan else "compiler")
    srcs = [os.path.join(ROOT, s) for s in ["driver.c"] + SOURCES]
    p = subprocess.run([CC] + flags + srcs + ["-o", COMPILER],
                       capture_output=True, text=True)
    if p.returncode != 0:
        print("building the compiler failed:\n" + p.stderr)
        sys.exit(1)
    # like ./compiler in the repository, the test build finds grammar.txt
    # next to itself when it is run from another directory
    shutil.copy(os.path.join(ROOT, "grammar.txt"), BUILD)


# ============================================================ unit tests

def add_unit_tests(suite, asan):
    def no_warnings():
        srcs = [os.path.join(ROOT, s) for s in ["driver.c"] + SOURCES]
        p = subprocess.run([CC, "-Wall", "-Wextra", "-O2", "-fsyntax-only"] +
                           srcs, capture_output=True, text=True)
        expect(p.returncode == 0 and "warning" not in p.stderr,
               "the compiler sources have warnings:\n" + p.stderr[-3000:])
    suite.add("build: sources compile without warnings (-Wall -Wextra)",
              no_warnings)

    def unit():
        exe = os.path.join(BUILD, "unit")
        srcs = [os.path.join(TESTS, "unit", "test_units.c")] + \
            [os.path.join(ROOT, s) for s in SOURCES]
        flags = ["-Wall", "-Wextra", "-I", ROOT] + \
            (SANITIZE if asan else ["-g"])
        p = subprocess.run([CC] + flags + srcs + ["-o", exe],
                           capture_output=True, text=True)
        expect(p.returncode == 0, "unit tests do not build:\n" + p.stderr)
        p = run([exe], timeout=60)
        expect(p.returncode == 0, p.stdout[-3000:] + p.stderr[-2000:])
    suite.add("unit: C unit tests (tests/unit/test_units.c)", unit)


# =========================================================== lexer tests

# (name, source, expected tokens, expected errors)
# A token is "TYPE", "TYPE lexeme" or "TYPE lexeme @line"; TK_DOLLAR is
# implied at the end. An error is (line, text).
LEXER_CASES = [
    ("single-char symbols", "+ - * / ( ) [ ] , ; : . ~",
     ["TK_PLUS", "TK_MINUS", "TK_MUL", "TK_DIV", "TK_OP", "TK_CL", "TK_SQL",
      "TK_SQR", "TK_COMMA", "TK_SEM", "TK_COLON", "TK_DOT", "TK_NOT"], []),
    ("relational operators", "< <= > >= == !=",
     ["TK_LT", "TK_LE", "TK_GT", "TK_GE", "TK_EQ", "TK_NE"], []),
    ("relational operators without spaces", "a<b<=c>d>=e==x!=y",
     ["TK_FIELDID a", "TK_LT", "TK_FIELDID b", "TK_LE", "TK_FIELDID c",
      "TK_GT", "TK_FIELDID d", "TK_GE", "TK_FIELDID e", "TK_EQ",
      "TK_FIELDID x", "TK_NE", "TK_FIELDID y"], []),
    ("assignment operator", "b2<---5",
     ["TK_ID b2", "TK_ASSIGNOP", "TK_NUM 5"], []),
    ("short assignment operator", "b2 <-- 5",
     ["TK_ID b2", "TK_NUM 5"], [(1, "expected <--")]),
    ("less-than minus", "b2 <-5",
     ["TK_ID b2", "TK_NUM 5"], [(1, "expected <--")]),
    ("logical operators", "&&& @@@ ~",
     ["TK_AND", "TK_OR", "TK_NOT"], []),
    ("incomplete and", "&& b2", ["TK_ID b2"], [(1, "expected &&&")]),
    ("incomplete and (one)", "& b2", ["TK_ID b2"], [(1, "expected &&&")]),
    ("incomplete or", "@@ b2", ["TK_ID b2"], [(1, "expected @@@")]),
    ("lone equals", "= b2", ["TK_ID b2"], [(1, "expected ==")]),
    ("lone bang", "! b2", ["TK_ID b2"], [(1, "expected !=")]),
    ("all keywords",
     "with parameters end while union endunion definetype as type global "
     "parameter list input output int real endwhile if then endif read "
     "write return call record endrecord else",
     ["TK_WITH", "TK_PARAMETERS", "TK_END", "TK_WHILE", "TK_UNION",
      "TK_ENDUNION", "TK_DEFINETYPE", "TK_AS", "TK_TYPE", "TK_GLOBAL",
      "TK_PARAMETER", "TK_LIST", "TK_INPUT", "TK_OUTPUT", "TK_INT",
      "TK_REAL", "TK_ENDWHILE", "TK_IF", "TK_THEN", "TK_ENDIF", "TK_READ",
      "TK_WRITE", "TK_RETURN", "TK_CALL", "TK_RECORD", "TK_ENDRECORD",
      "TK_ELSE"], []),
    ("keyword look-alikes are field ids", "endrecorb whiles ifx ends",
     ["TK_FIELDID endrecorb", "TK_FIELDID whiles", "TK_FIELDID ifx",
      "TK_FIELDID ends"], []),
    ("keywords are case sensitive", "while While",
     ["TK_WHILE", "TK_FIELDID hile"], [(1, "W not recognized")]),
    ("identifiers", "b2 c7 d2bcd b2bbb777 c3c d5cb34567",
     ["TK_ID b2", "TK_ID c7", "TK_ID d2bcd", "TK_ID b2bbb777", "TK_ID c3c",
      "TK_ID d5cb34567"], []),
    ("identifier split after digits", "b2c3d4",
     ["TK_ID b2c3", "TK_ID d4"], []),
    ("identifier stops at 8 and 9", "b28 c39",
     ["TK_ID b2", "TK_NUM 8", "TK_ID c3", "TK_NUM 9"], []),
    ("identifier of 20 characters", "b2" + "c" * 18,
     ["TK_ID b2" + "c" * 18], []),
    ("identifier of 21 characters", "b2" + "c" * 19 + " ;",
     ["TK_SEM"], [(1, "exceeded max length of identifier(20)")]),
    ("long identifier ending in digits", "b2" + "c" * 10 + "3" * 9 + ";",
     ["TK_SEM"], [(1, "exceeded max length of identifier(20)")]),
    ("field identifiers", "abc x maths physics",
     ["TK_FIELDID abc", "TK_FIELDID x", "TK_FIELDID maths",
      "TK_FIELDID physics"], []),
    ("one-letter b c d are field ids", "b c d",
     ["TK_FIELDID b", "TK_FIELDID c", "TK_FIELDID d"], []),
    ("b followed by a letter is a field id", "bat cow dog",
     ["TK_FIELDID bat", "TK_FIELDID cow", "TK_FIELDID dog"], []),
    ("field id of 20 characters", "a" * 20, ["TK_FIELDID " + "a" * 20], []),
    ("field id of 21 characters", "a" * 21 + " ;",
     ["TK_SEM"], [(1, "exceeded max length of identifier(20)")]),
    ("function identifiers", "_abc _abcDEF _f12 _main _mainx _ma",
     ["TK_FUNID _abc", "TK_FUNID _abcDEF", "TK_FUNID _f12", "TK_MAIN _main",
      "TK_FUNID _mainx", "TK_FUNID _ma"], []),
    ("function id followed by letters after digits", "_abc12x",
     ["TK_FUNID _abc12", "TK_FIELDID x"], []),
    ("bad function ids", "_ _9",
     ["TK_NUM 9"], [(1, "expected function name"),
                    (1, "expected function name")]),
    ("function id of 30 characters", "_" + "a" * 29,
     ["TK_FUNID _" + "a" * 29], []),
    ("function id of 31 characters", "_" + "a" * 30 + " ;",
     ["TK_SEM"], [(1, "exceeded max size of function name (30)")]),
    ("record identifiers", "#abc #a #marks",
     ["TK_RUID #abc", "TK_RUID #a", "TK_RUID #marks"], []),
    ("record id stops at a digit", "#ab1",
     ["TK_RUID #ab", "TK_NUM 1"], []),
    ("lone hash", "# x", ["TK_FIELDID x"],
     [(1, "expected record identifier")]),
    ("integers", "0 7 123456 007",
     ["TK_NUM 0", "TK_NUM 7", "TK_NUM 123456", "TK_NUM 007"], []),
    ("reals", "12.34 0.50 12.34E+05 12.34E-05 12.34E05 99.99E99",
     ["TK_RNUM 12.34", "TK_RNUM 0.50", "TK_RNUM 12.34E+05",
      "TK_RNUM 12.34E-05", "TK_RNUM 12.34E05", "TK_RNUM 99.99E99"], []),
    ("real needs two decimals", "12.3 x", ["TK_FIELDID x"],
     [(1, "expected number after decimal")]),
    ("real needs decimals", "12. x", ["TK_FIELDID x"],
     [(1, "expected number after decimal")]),
    ("exponent needs digits", "12.34E x", ["TK_FIELDID x"],
     [(1, "expected number after E")]),
    ("signed exponent needs digits", "12.34E+ x", ["TK_FIELDID x"],
     [(1, "expected number after E")]),
    ("signed exponent needs two digits", "12.34E+5 x", ["TK_FIELDID x"],
     [(1, "expected number after E")]),
    ("unsigned exponent needs two digits", "12.34E5 x", ["TK_FIELDID x"],
     [(1, "expected number after E")]),
    ("real then dot", "12.34.56",
     ["TK_RNUM 12.34", "TK_DOT", "TK_NUM 56"], []),
    ("record field access", "d3.tag.x",
     ["TK_ID d3", "TK_DOT", "TK_FIELDID tag", "TK_DOT", "TK_FIELDID x"], []),
    ("comment to end of line", "b2 % comment <--- here\nc3",
     ["TK_ID b2 @1", "TK_COMMENT @1", "TK_ID c3 @2"], []),
    ("comment at end of file without newline", "b2 % trailing",
     ["TK_ID b2", "TK_COMMENT"], []),
    ("only a comment", "% nothing else\n", ["TK_COMMENT"], []),
    ("empty file", "", [], []),
    ("line numbers", "\n\nb2\n\n c3\n%x\n\nd4",
     ["TK_ID b2 @3", "TK_ID c3 @5", "TK_COMMENT @6", "TK_ID d4 @8"], []),
    ("windows line endings", "b2;\r\nc3;\r\n",
     ["TK_ID b2 @1", "TK_SEM @1", "TK_ID c3 @2", "TK_SEM @2"], []),
    ("tabs", "\tb2\t;\t", ["TK_ID b2", "TK_SEM"], []),
    ("unknown characters", "$ ? ^ |",
     [], [(1, "$ not recognized"), (1, "? not recognized"),
          (1, "^ not recognized"), (1, "| not recognized")]),
    ("errors do not stop the lexer", "b2 $ c3\n? d4",
     ["TK_ID b2 @1", "TK_ID c3 @1", "TK_ID d4 @2"],
     [(1, "$ not recognized"), (2, "? not recognized")]),
    ("statement", "c2<---12.50*b3;",
     ["TK_ID c2", "TK_ASSIGNOP", "TK_RNUM 12.50", "TK_MUL", "TK_ID b3",
      "TK_SEM"], []),
    ("call statement", "[b2] <--- call _f with parameters [c3, d4];",
     ["TK_SQL", "TK_ID b2", "TK_SQR", "TK_ASSIGNOP", "TK_CALL", "TK_FUNID _f",
      "TK_WITH", "TK_PARAMETERS", "TK_SQL", "TK_ID c3", "TK_COMMA",
      "TK_ID d4", "TK_SQR", "TK_SEM"], []),
]


def add_lexer_tests(suite):
    for name, src, want, want_errs in LEXER_CASES:
        def test(src=src, want=want, want_errs=want_errs):
            toks, errs = tokens_of(src)
            expect(toks and toks[-1][1] == "TK_DOLLAR",
                   "token stream does not end with TK_DOLLAR: %r" % toks)
            got = toks[:-1]
            expect(len(got) == len(want),
                   "expected %d tokens, got %d: %s" %
                   (len(want), len(got),
                    " ".join("%s(%s)" % (t, lx) for _, t, lx in got)))
            for (line, typ, lex), w in zip(got, want):
                parts = w.split()
                at = [p for p in parts if p.startswith("@")]
                parts = [p for p in parts if not p.startswith("@")]
                expect(typ == parts[0], "expected %s, got %s(%s)" %
                       (w, typ, lex))
                if len(parts) > 1:
                    expect(lex == parts[1], "expected lexeme %r for %s, got "
                           "%r" % (parts[1], typ, lex))
                if at:
                    expect(line == int(at[0][1:]), "expected %s on line %s, "
                           "got line %d" % (typ, at[0][1:], line))
            match_errors("lexical errors", want_errs, errs)
        suite.add("lexer: " + name, test)

    def exit_status():
        ok = compiler("--tokens", scratch("lexok", "b2 <--- 3;"))
        bad = compiler("--tokens", scratch("lexbad", "b2 <--- $;"))
        expect(ok.returncode == 0 and bad.returncode == 1,
               "--tokens exit status should be 0 / 1, got %d / %d" %
               (ok.returncode, bad.returncode))
    suite.add("lexer: --tokens exit status reflects errors", exit_status)


# ========================================================== parser tests

def leaves_of_parse_tree(path):
    """Terminal leaves of a printed parse tree, in printed order."""
    leaves = []
    with open(path) as f:
        lines = f.read().splitlines()[2:]
    for row in lines:
        cols = row.split()
        # lexeme line tokenName value parent leaf nodeSymbol
        if len(cols) >= 7 and cols[-2] == "yes" and cols[2] != "TK_EPS":
            leaves.append(cols[2])
    return leaves


def check_parse_tree(src):
    """The in-order printed tree must list the source tokens left to right:
    leaves of an in-order walk keep their left-to-right order."""
    out = os.path.join(BUILD, "src", os.path.basename(src) + ".tree")
    p = compiler("--parse", src, out)
    expect(p.returncode == 0, "parse failed:\n" + p.stdout[-2000:])
    toks, _ = tokens_of(open(src).read())
    want = [t for _, t, _ in toks if t not in ("TK_COMMENT", "TK_DOLLAR")]
    got = leaves_of_parse_tree(out)
    expect(got == want, "parse tree leaves differ from the token stream "
           "(first difference at %d)" %
           next((i for i, (a, b) in enumerate(zip(got, want)) if a != b),
                min(len(got), len(want))))


def add_parser_tests(suite):
    # every valid program parses, and its tree keeps all the tokens
    valid = [os.path.join(ROOT, "testcase%d.txt" % i) for i in
             (3, 4, 5, 7, 8, 9, 10)]
    for d in ("programs", "semantic"):
        folder = os.path.join(TESTS, d)
        valid += [os.path.join(folder, f) for f in sorted(os.listdir(folder))]
    for path in valid:
        name = os.path.relpath(path, ROOT)
        suite.add("parser: parse tree of %s" % name,
                  lambda path=path: check_parse_tree(path))

    folder = os.path.join(TESTS, "syntax")
    for f in sorted(os.listdir(folder)):
        path = os.path.join(folder, f)

        def test(path=path):
            ann = annotations(path)
            p = compiler("--check", path)
            expect(p.returncode == 1, "expected exit status 1, got %d" %
                   p.returncode)
            syn = sorted({int(l) for l, _ in matches(SYN_ERR, p.stdout)})
            expect(syn == sorted(ann["syntax-error"]),
                   "syntax errors on lines %s, expected %s\n%s" %
                   (syn, sorted(ann["syntax-error"]), p.stdout[-1500:]))
            lex = [(int(l), m) for l, m in matches(LEX_ERR, p.stdout)]
            match_errors("lexical errors", ann["lex-error"], lex)
            expect(not matches(SEM_ERR, p.stdout),
                   "semantic analysis must not run after syntax errors")
            # printing the tree of an erroneous program must not crash
            out = os.path.join(BUILD, "src", f + ".tree")
            compiler("--parse", path, out)
        suite.add("syntax: " + f, test)

    def parse_tree_format():
        src = os.path.join(ROOT, "testcase4.txt")
        out = os.path.join(BUILD, "src", "format.tree")
        compiler("--parse", src, out)
        rows = open(out).read().splitlines()
        expect(rows[0].split() == ["lexeme", "line", "tokenName", "value",
                                   "parentNodeSymbol", "leaf", "NodeSymbol"],
               "bad header: %r" % rows[0])
        # the root is printed after its first subtree with parent ROOT
        root = [r for r in rows if " ROOT " in r]
        expect(len(root) == 1 and root[0].split()[-1] == "program",
               "expected one ROOT row for <program>: %r" % root)
        nums = [r.split() for r in rows if " TK_NUM " in r]
        expect(nums and all(r[0] == r[3] for r in nums),
               "TK_NUM rows must repeat the number as value: %r" % nums[:2])
    suite.add("parser: parse tree file format", parse_tree_format)


# ======================================================== semantic tests

def add_semantic_tests(suite):
    folder = os.path.join(TESTS, "semantic")
    for f in sorted(os.listdir(folder)):
        path = os.path.join(folder, f)

        def test(path=path):
            ann = annotations(path)
            p = compiler("--check", path)
            errs = [(int(l), m) for l, m in matches(SEM_ERR, p.stdout)]
            expect(not matches(SYN_ERR, p.stdout) and
                   not matches(LEX_ERR, p.stdout),
                   "unexpected lexical/syntax errors:\n" + p.stdout[-1500:])
            match_errors("semantic errors", ann["error"], errs)
            expect(p.returncode == (1 if ann["error"] else 0),
                   "exit status %d" % p.returncode)
            expect(ann["ok"] == (not ann["error"]),
                   "a semantic test needs '%? ok' or at least one error")
        suite.add("semantic: " + f, test)

    def sorted_by_line():
        p = compiler("--check", os.path.join(ROOT, "testcase5.txt"))
        lines = [int(l) for l, _ in matches(SEM_ERR, p.stdout)]
        expect(lines == sorted(lines) and lines, "errors not sorted: %s" %
               lines)
    suite.add("semantic: errors are reported in line order", sorted_by_line)


# ===================================================== end-to-end tests

def compile_and_run(path, stdin_lines, expect_exit=0, expected_stdout=None):
    base = os.path.join(BUILD, "bin", os.path.basename(path)[:-4])
    p = compiler("--build", path, base)
    expect(p.returncode == 0, "compilation failed:\n" + p.stdout[-2000:] +
           p.stderr[-1000:])
    stdin = "".join(l + "\n" for l in stdin_lines)
    r = run([base], stdin=stdin, timeout=10)
    got = r.stdout.splitlines()
    if expected_stdout is not None:
        expect(got == expected_stdout,
               "output differs\nexpected: %r\n     got: %r" %
               (expected_stdout, got))
    expect(r.returncode == expect_exit, "exit status %d, expected %d" %
           (r.returncode, expect_exit))
    return got


# the sample programs shipped with the project: stdin -> expected stdout
SAMPLE_RUNS = {
    "testcase3.txt": (["2", "50.00 60.00 70.00", "80.00 90.00 100.00"],
                      ["65.00", "75.00", "85.00"]),
    # c3 = 1, c4 = 7, c5 = 3: d4 = 17/4 = 4 (integer division), c4bbb =
    # 1.65 * 473 + 5 = 785.45, the condition holds, c6 = 4 / 785.45
    "testcase4.txt": (["7", "3"], ["0.01"]),
    "testcase7.txt": (["4", "9"], ["9"]),
    "testcase8.txt": ([], ["0.00", "0.00", "1"]),
    "testcase10.txt": (["55.50", "7"], ["7", "75.50"]),
}

TESTCASE_ERRORS = {
    # lexical-only test files
    "testcase1.txt": "lex",
    "testcase2.txt": "lex",
    "testcase6.txt": "syntax",
    "testcase5.txt": "semantic",
    "testcase9.txt": "semantic",
}


def add_program_tests(suite):
    folder = os.path.join(TESTS, "programs")
    for f in sorted(os.listdir(folder)):
        path = os.path.join(folder, f)
        name = "run: " + f
        if not CAN_RUN:
            suite.skip(name, "needs x86-64 Linux with nasm")
            continue

        def test(path=path):
            ann = annotations(path)
            expect(ann["stdout"] or ann["exit"],
                   "a program test needs '%? stdout:' lines")
            compile_and_run(path, ann["stdin"], ann["exit"], ann["stdout"])
        suite.add(name, test)

    for f, (stdin, stdout) in SAMPLE_RUNS.items():
        name = "run: " + f
        if not CAN_RUN:
            suite.skip(name, "needs x86-64 Linux with nasm")
            continue
        suite.add(name, lambda f=f, stdin=stdin, stdout=stdout:
                  compile_and_run(os.path.join(ROOT, f), stdin, 0, stdout))

    def every_program_assembles():
        # the assembly must at least be accepted by nasm even where it
        # cannot run
        for f in sorted(os.listdir(folder)):
            out = os.path.join(BUILD, "src", f + ".asm")
            p = compiler("--asm", os.path.join(folder, f), out)
            expect(p.returncode == 0, "%s: %s" % (f, p.stdout[-1000:]))
            text = open(out).read()
            for needle in ("global main", "main:", "section .text"):
                expect(needle in text, "%s: %r missing from assembly" %
                       (f, needle))
            if shutil.which("nasm"):
                obj = out[:-4] + ".o"
                r = run(["nasm", "-f", "elf64", out, "-o", obj])
                expect(r.returncode == 0, "%s: nasm rejected the assembly:\n"
                       "%s" % (f, r.stderr[-1500:]))
    suite.add("codegen: assembly for every test program is valid NASM",
              every_program_assembles)


# ===================================================== sample test files

def add_sample_file_tests(suite):
    def testcase1():
        p = compiler("--check", os.path.join(ROOT, "testcase1.txt"))
        lex = [int(l) for l, _ in matches(LEX_ERR, p.stdout)]
        expect(p.returncode == 1, "testcase1 has lexical errors")
        # line 5: '@&', '=+'  line 6: '<-' and '<*'  line 8: 12.3, 12., E+
        # line 9: '_9'
        for line in (5, 6, 8, 9):
            expect(line in lex, "no lexical error reported on line %d: %s" %
                   (line, lex))
    suite.add("samples: testcase1 lexical errors (no crash)", testcase1)

    def testcase2():
        p = compiler("--check", os.path.join(ROOT, "testcase2.txt"))
        expect(p.returncode == 1 and matches(LEX_ERR, p.stdout),
               "testcase2 has lexical errors")
    suite.add("samples: testcase2 lexical errors (no crash)", testcase2)

    def testcase6():
        p = compiler("--check", os.path.join(ROOT, "testcase6.txt"))
        lex = sorted({int(l) for l, _ in matches(LEX_ERR, p.stdout)})
        syn = sorted({int(l) for l, _ in matches(SYN_ERR, p.stdout)})
        expect(lex == [8, 10, 13, 28, 29], "lexical error lines %s" % lex)
        expect(syn == [7, 8, 10, 11, 13, 16, 20, 25, 29],
               "syntax error lines %s" % syn)
    suite.add("samples: testcase6 lexical and syntax errors", testcase6)

    def testcase5():
        p = compiler("--check", os.path.join(ROOT, "testcase5.txt"))
        errs = [(int(l), m) for l, m in matches(SEM_ERR, p.stdout)]
        match_errors("semantic errors", [
            (16, "variable c6 is not declared"),
            (17, "variable c3 is not declared"),
            (18, "relational operator <= needs int or real operands"),
            (19, "variable c3bd is not declared"),
            (21, "operator + cannot be applied to #two and int"),
            (37, "undefined type #traingle for field tr of #four"),
            (56, "arithmetic is not allowed on #variantrecord"),
            (57, "cannot write d4 of type #variantrecord"),
        ], errs)
    suite.add("samples: testcase5 semantic errors", testcase5)

    def testcase9():
        p = compiler("--check", os.path.join(ROOT, "testcase9.txt"))
        errs = [(int(l), m) for l, m in matches(SEM_ERR, p.stdout)]
        # _sumRange declares a local b5 although _main declares b5 global
        match_errors("semantic errors",
                     [(16, "b5 is already declared as a global variable")],
                     errs)
    suite.add("samples: testcase9 global/local clash", testcase9)

    for i in (3, 4, 7, 8, 10):
        def ok(i=i):
            p = compiler("--check", os.path.join(ROOT, "testcase%d.txt" % i))
            expect(p.returncode == 0 and "compiles successfully" in p.stdout,
                   p.stdout[-1500:])
        suite.add("samples: testcase%d is error free" % i, ok)


# ============================================================ driver tests

def add_driver_tests(suite):
    src = os.path.join(ROOT, "testcase7.txt")

    def menu(choices, outname="menu.out"):
        out = os.path.join(BUILD, "src", outname)
        p = compiler(src, out, stdin=choices, timeout=20)
        return p, out

    def exit_option():
        p, _ = menu("0\n")
        expect(p.returncode == 0 and p.stdout.count("Enter your choice") == 1,
               p.stdout)
    suite.add("driver: option 0 exits", exit_option)

    def eof_terminates():
        p, _ = menu("")  # used to loop forever on end of input
        expect(p.returncode == 0, "exit %d" % p.returncode)
    suite.add("driver: end of input ends the menu loop", eof_terminates)

    def remove_comments():
        p, _ = menu("1\n0\n")
        expect("%Test Case 7" not in p.stdout and "_maxOfTwo" in p.stdout,
               p.stdout[:500])
        expect("Successfully removed comments" in p.stdout, p.stdout[-300:])
    suite.add("driver: option 1 removes comments", remove_comments)

    def print_tokens():
        p, _ = menu("2\n0\n")
        expect("TK_FUNID" in p.stdout and "_maxOfTwo" in p.stdout,
               p.stdout[:500])
    suite.add("driver: option 2 prints tokens", print_tokens)

    def parse_tree():
        p, out = menu("3\n0\n", "menu3.tree")
        expect("No syntax errors" in p.stdout, p.stdout[-500:])
        expect(os.path.exists(out) and "program" in open(out).read(),
               "parse tree file missing")
    suite.add("driver: option 3 writes the parse tree", parse_tree)

    def timing():
        p, _ = menu("4\n0\n")
        expect("Total CPU time taken" in p.stdout, p.stdout[-500:])
    suite.add("driver: option 4 prints timing", timing)

    def ast():
        p, _ = menu("5\n0\n")
        expect("Function _maxOfTwo" in p.stdout and "If (line 4)" in p.stdout,
               p.stdout[-800:])
    suite.add("driver: option 5 prints the AST", ast)

    def symbols():
        p, _ = menu("6\n0\n")
        expect("Function _maxOfTwo" in p.stdout and "input" in p.stdout,
               p.stdout[-800:])
    suite.add("driver: option 6 prints the symbol table", symbols)

    def semantic():
        p, _ = menu("7\n0\n")
        expect("compiles successfully" in p.stdout, p.stdout[-500:])
    suite.add("driver: option 7 runs semantic analysis", semantic)

    def codegen():
        p, out = menu("8\n0\n", "menu8.asm")
        expect("Assembly written" in p.stdout, p.stdout[-500:])
        expect("F_maxOfTwo:" in open(out).read(), "function label missing")
    suite.add("driver: option 8 writes assembly", codegen)

    def wrong_choice():
        p, _ = menu("9\n0\n")
        expect("wrong choice" in p.stdout, p.stdout[-300:])
    suite.add("driver: unknown option", wrong_choice)

    def no_args():
        p = compiler()
        expect(p.returncode == 1 and "usage" in p.stdout, p.stdout)
    suite.add("driver: missing arguments", no_args)

    def bad_flags():
        expect(compiler("--nope", src).returncode == 2, "unknown flag")
        expect(compiler("--asm", src).returncode == 2, "missing output")
        expect(compiler("--check", src, "x").returncode == 2, "extra arg")
    suite.add("driver: bad flag usage exits with 2", bad_flags)

    def missing_source():
        p = compiler("--check", os.path.join(BUILD, "no-such-file.txt"))
        expect(p.returncode == 1, "exit %d" % p.returncode)
    suite.add("driver: missing source file", missing_source)

    def no_asm_on_error():
        out = os.path.join(BUILD, "src", "should-not-exist.asm")
        if os.path.exists(out):
            os.remove(out)
        p = compiler("--asm", os.path.join(ROOT, "testcase9.txt"), out)
        expect(p.returncode == 1 and not os.path.exists(out),
               "assembly must not be written for an invalid program")
    suite.add("driver: no assembly for invalid programs", no_asm_on_error)

    def other_directory():
        # grammar.txt is found next to the executable
        p = compiler("--check", src, cwd=os.path.join(BUILD, "src"))
        expect(p.returncode == 0, p.stdout[-500:])
    suite.add("driver: works from another directory", other_directory)


# ==================================================================== main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-k", dest="pattern", help="only run matching tests")
    ap.add_argument("-v", action="store_true", help="list passing tests")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 2,
                    help="parallel jobs")
    ap.add_argument("--asan", action="store_true",
                    help="build with AddressSanitizer/UBSan and fail on any "
                    "report")
    args = ap.parse_args()

    build(args.asan)
    suite = Suite(args.pattern, args.v)
    add_unit_tests(suite, args.asan)
    add_lexer_tests(suite)
    add_parser_tests(suite)
    add_semantic_tests(suite)
    add_sample_file_tests(suite)
    add_program_tests(suite)
    add_driver_tests(suite)
    suite.run(args.j)

    for name, reason in suite.skipped:
        print("  skip  %s (%s)" % (name, reason))
    total = suite.passed + len(suite.failed)
    print("\n%d passed, %d failed, %d skipped (of %d)" %
          (suite.passed, len(suite.failed), len(suite.skipped),
           total + len(suite.skipped)))
    sys.exit(1 if suite.failed else 0)


if __name__ == "__main__":
    main()
