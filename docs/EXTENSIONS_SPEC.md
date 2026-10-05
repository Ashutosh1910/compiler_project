# Language Extensions Specification

Status: normative. Audience: the engineer implementing the extensions in this
repository, the engineer writing tests for them, and the author of the toy
compiler `toy/toycc.txt`. Each part can be worked on independently from this
document alone.

* **Part A** extends the course language (the "language" below) with byte I/O,
  output without newlines, string and character literals, one-dimensional
  arrays, recursion, calls to functions defined later, and an `exit`
  statement.
* **Part B** defines a tiny source language **TL** and the contract of a toy
  compiler for TL that is written *in* the extended language.
* **Part C** is the test plan, laid out like the existing suite
  (`tests/syntax`, `tests/semantic`, `tests/programs`, `LEXER_CASES`), plus the
  toy-compiler acceptance tests.

Conventions: "must" is mandatory. Error texts are given exactly, without the
`[LEXER-ERROR] at line N: ` / `[SEMANTIC-ERROR] at line N: ` prefix that the
existing printers add. `<x>` inside a message stands for a value described next
to it. Code is shown in the file's own notation.

---

## 0. Summary of the design

| # | Need of a toy compiler | Extension |
|---|---|---|
| 1 | read source byte by byte, detect EOF | statement `readchar(<int variable or element>);` stores the next byte (0..255) or -1 at end of input |
| 2 | write assembly text | statement `print(<item>, <item>, ...);` writes string literals, ints (`%lld`) and reals (`%.2f`) with no newline; statement `writechar(<int expr>);` writes one byte |
| 3 | buffers, tables | fixed-size 1-D arrays `type int[N] : b2;` / `type real[N] : c2;`, locals or globals; elements `b2[<arith expr>]` usable wherever a variable is (assignment target, expressions, conditions, `read`, `write`, `readchar`); runtime bounds check |
| 4 | recursive-descent parser | a function may call any function, including itself and functions defined later; static stack check only for non-recursive chains; runtime stack guard |
| 5 | readable byte comparisons | character literals `'a'`, `'\n'`, ... are int constants |
| 6 | fail with a status | statement `exit(<int expr>);` |
| 7 | loops driven by globals that callees update | while rule: a call statement counts as updating every global variable |

New reserved words: `readchar`, `writechar`, `print`, `exit`. They remain
usable as record/union field names (so no existing program breaks).

Rejected as nice-to-haves (see A.15): remainder operator, unary minus, string
variables, arrays as parameters/fields/elements of records, whole-array
assignment, arithmetic expressions in conditions, `break`, stderr output,
functions without input parameters, static (compile-time) bounds checks.

---

# PART A — Extensions of the course language

## A.1 Lexical changes

### A.1.1 New token types

Insert six values into `enum TokenType` in `lexerDef.h` **immediately after
`TK_NE` and before `TK_EPS`**, in this order:

```c
  TK_READCHAR,
  TK_WRITECHAR,
  TK_PRINT,
  TK_EXIT,
  TK_CHARLIT,
  TK_STR,
```

`NUM_TOKENS` becomes 66 (still below the 128 bits of `BitSet` and below
`TERM_MAP_CAP`). The existing unit test `test_terminal_names` iterates
`t < TK_EPS`, so the new tokens must be before `TK_EPS`.

`logging.c`:

* `tokenTypeToString` returns `"TK_READCHAR"`, `"TK_WRITECHAR"`, `"TK_PRINT"`,
  `"TK_EXIT"`, `"TK_CHARLIT"`, `"TK_STR"`.
* `tokenTypeToLexeme` returns `"readchar"`, `"writechar"`, `"print"`, `"exit"`
  for the four keywords (needed because a keyword may appear as a field name
  in the parse tree); `TK_CHARLIT` and `TK_STR` fall through to `t->lexeme`.

| TokenType | Pattern | `Token.lexeme` | Max lexeme |
|---|---|---|---|
| `TK_READCHAR` | keyword `readchar` | `readchar` | 8 |
| `TK_WRITECHAR` | keyword `writechar` | `writechar` | 9 |
| `TK_PRINT` | keyword `print` | `print` | 5 |
| `TK_EXIT` | keyword `exit` | `exit` | 4 |
| `TK_CHARLIT` | `'` *char* `'` (A.1.3) | the source spelling, quotes included, e.g. `'a'`, `'\n'` | 4 |
| `TK_STR` | `"` *schar*\* `"` (A.1.4) | the first 30 bytes of the source spelling, opening quote included | 30 |

### A.1.2 Keywords

Add to `initializeKeywordMap()`:

```c
  insertInHashmap(&keywordMap, "readchar", TK_READCHAR);
  insertInHashmap(&keywordMap, "writechar", TK_WRITECHAR);
  insertInHashmap(&keywordMap, "print", TK_PRINT);
  insertInHashmap(&keywordMap, "exit", TK_EXIT);
```

(31 keywords in a 53-slot table.) The words are recognised exactly like the
existing keywords: a `[a-z]+` lexeme scanned on the TK_FIELDID path is looked
up after scanning, so `prints`, `exits`, `printx`, `readchars` stay
`TK_FIELDID`, and `Print` is not a keyword (it is the existing
`P not recognized` error). TK_ID lexemes (`[b-d][2-7]...`) can never equal a
keyword.

The four words are reserved everywhere except as field names: the grammar
(A.2) accepts them after `.` and in field definitions, so a record field
named `print` keeps working (`d2.print`). They cannot be variable names (they
never were: variables are TK_ID).

### A.1.3 Character literals (`'`)

A new `case '\''` in `scan()`. All characters are bytes; "printable" means
0x20..0x7E.

```
char      ::= any printable byte except ' (0x27) and \ (0x5C) | escape
escape    ::= \n | \t | \r | \0 | \\ | \' | \"
```

Values: the byte itself for a plain char; `\n`=10, `\t`=9, `\r`=13, `\0`=0,
`\\`=92, `\'`=39, `\"`=34. A raw `"` is a plain char (`'"'` = 34). A raw tab
is **not** allowed (write `'\t'`).

DFA (c1, c2, c3 are the bytes read after the opening quote; "consume" means the
byte is part of the literal; "leave" means `s->scanNext = 0` so the byte is
processed next by the main loop, which keeps line counting right). "End of
line" means LF (0x0A) **or CR (0x0D)**: a CR is treated exactly like LF inside
literals, so on a CRLF line an unterminated literal gives one error and the CR
and LF are then skipped as whitespace by the main loop:

1. Read c1.
   * c1 is EOF or end of line: error `unterminated character literal`; leave c1. No token.
   * c1 is `'`: error `empty character literal` (both quotes consumed). No token.
   * c1 is `\`: read c2.
     * c2 in `n t r 0 \ ' "`: value from the table; go to step 2.
     * c2 is EOF or end of line: error `unterminated character literal`; leave c2. No token.
     * otherwise: error `unknown escape sequence \<X> in character literal`; go to SKIP.
   * c1 printable (and not `'`, `\`): value = c1; go to step 2.
   * otherwise (control byte other than LF/CR, tab, byte >= 0x80): error
     `byte \x<HH> not allowed in character literal`; go to SKIP.
2. Read c3.
   * c3 is `'`: emit `TK_CHARLIT` with the spelling as lexeme.
   * c3 is EOF or end of line: error `unterminated character literal`; leave c3. No token.
   * otherwise: error `character literal must contain exactly one character`;
     go to SKIP.
3. SKIP: read bytes until `'` (consumed) or until end of line/EOF (left). No further
   errors are reported for this literal, and no token is emitted.

`<X>` in an escape message is the byte after the backslash printed as itself
if it is printable, else as `x` followed by two upper-case hex digits (so the
message reads `unknown escape sequence \x01 in character literal`). `<HH>` is
two upper-case hex digits, as in the existing `byte \x%02X not recognized`.
All errors are reported at the line of the opening quote (a literal never
spans lines). The existing `char msg[32]` buffer is too small for the new
messages; use at least 80 bytes.

Helper (lexer.c, declared in lexer.h): `int charLiteralValue(const char
*lexeme)` returns the value of a valid `TK_CHARLIT` spelling. The AST builder
uses it.

### A.1.4 String literals (`"`)

A new `case '"'` in `scan()`.

```
schar     ::= any printable byte except " (0x22) and \ (0x5C) | sescape
sescape   ::= \n | \t | \r | \\ | \" | \'
```

There is no `\0` in strings (the runtime prints strings with `%s`). Raw tabs
are not allowed. Decoded length (each escape counts 1) is 0..255 bytes; `""`
is valid.

DFA: after the opening quote, loop:

* `"`: end of literal.
* EOF or end of line (LF or CR, as in A.1.3): error `unterminated string literal`; leave the byte; stop. No token.
* `\`: read the next byte b.
  * b in `n t r \ " '`: append the decoded byte.
  * b is EOF or end of line: error `unterminated string literal`; leave b; stop. No token.
  * otherwise: error `unknown escape sequence \<X> in string literal`
    (`<X>` as in A.1.3); nothing is appended; continue.
* printable byte: append it.
* any other byte (control byte other than LF/CR, tab, byte >= 0x80): error
  `byte \x<HH> not allowed in string literal`; continue.

Appending a byte when 255 bytes are already decoded reports
`string literal longer than 255 characters` once for this literal and stops
appending; scanning continues to the closing quote. At the closing quote a
`TK_STR` token is emitted only if no error was reported for this literal.
Each error is reported at the line of the opening quote. Several errors in
one literal are all reported (e.g. two bad escapes give two errors).

`%` inside a string or character literal is an ordinary byte, not a comment.

### A.1.5 Where the decoded string lives

`Token.lexeme` holds only 30 bytes, so the decoded string is kept in a string
table owned by the lexer:

* `lexerDef.h`: `Token` gains `int literal;` — index into the string table
  for `TK_STR`, `-1` for every other token. `newToken` sets it to `-1`.
* `lexer.c` / `lexer.h`:
  `int addStringLiteral(const char *bytes, int len);` (returns the index),
  `const char *stringLiteralText(int index, int *len);` (NUL-terminated;
  strings contain no NUL), and the table is cleared by `initializeState()`.
* `parserDef.h`: `TreeNode` gains `int literal;` (`newTreeNode` sets `-1`);
  `parseTokens` copies `tokens->buf[tokenIdx].literal` into the node when it
  matches a terminal.
* The AST copies the bytes (`malloc`) into its own `PrintItem` (A.11), so
  clearing the table on the next `initializeState()` is harmless.

### A.1.6 Printing tokens

* `--tokens` (and menu option 2, i.e. `printToken`) and the parse-tree file
  (`printNodeRow`) print the lexeme of `TK_STR` and `TK_CHARLIT` with every
  space byte replaced by the four characters `\x20`, so that every column of
  those listings stays free of spaces (the test suite splits rows on
  whitespace). Examples: `"a b"` prints as `"a\x20b"`, `' '` as `'\x20'`.
  Nothing else changes in these printers.
* Syntax-error messages print the lexeme unchanged.

### A.1.7 `removeComments` (menu option 1)

`removeComments` must not treat `%` inside a string or character literal as
a comment: when it copies a `"` or `'`, it copies bytes verbatim up to and
including the matching closing quote, where a backslash copies the following
byte unconditionally unless that byte is LF, CR or EOF, and the literal also
ends (without consuming the byte) at LF, CR or EOF.

---

## A.2 Grammar

### A.2.1 The new `grammar.txt` (complete file)

Replace `grammar.txt` by exactly the following 58 lines (58 non-terminals, 113
rules, 17 `eps` rules). Line order matters only in that `<program>` stays
first.

```
<program> ::= <otherFunctions> <mainFunction>
<mainFunction> ::= TK_MAIN <stmts> TK_END
<otherFunctions> ::= <function> <otherFunctions> | eps
<function> ::= TK_FUNID <input_par> <output_par> TK_SEM <stmts> TK_END
<input_par> ::= TK_INPUT TK_PARAMETER TK_LIST TK_SQL <parameter_list> TK_SQR
<output_par> ::= TK_OUTPUT TK_PARAMETER TK_LIST TK_SQL <parameter_list> TK_SQR | eps
<parameter_list> ::= <dataType> TK_ID <remaining_list>
<dataType> ::= <primitiveDatatype> | <constructedDatatype>
<primitiveDatatype> ::= TK_INT | TK_REAL
<constructedDatatype> ::= TK_RECORD TK_RUID | TK_UNION TK_RUID | TK_RUID
<remaining_list> ::= TK_COMMA <parameter_list> | eps
<stmts> ::= <typeDefinitions> <declarations> <otherStmts> <returnStmt>
<typeDefinitions> ::= <actualOrRedefined> <typeDefinitions> | eps
<actualOrRedefined> ::= <typeDefinition> | <definetypestmt>
<typeDefinition> ::= TK_RECORD TK_RUID <fieldDefinitions> TK_ENDRECORD | TK_UNION TK_RUID <fieldDefinitions> TK_ENDUNION
<fieldDefinitions> ::= <fieldDefinition> <fieldDefinition> <moreFields>
<fieldDefinition> ::= TK_TYPE <fieldType> TK_COLON <fieldName> TK_SEM
<fieldName> ::= TK_FIELDID | TK_READCHAR | TK_WRITECHAR | TK_PRINT | TK_EXIT
<fieldType> ::= <primitiveDatatype> | TK_RUID
<moreFields> ::= <fieldDefinition> <moreFields> | eps
<declarations> ::= <declaration> <declarations> | eps
<declaration> ::= TK_TYPE <dataType> <arrayDim> TK_COLON TK_ID <global_or_not> TK_SEM
<arrayDim> ::= TK_SQL TK_NUM TK_SQR | eps
<global_or_not> ::= TK_COLON TK_GLOBAL | eps
<otherStmts> ::= <stmt> <otherStmts> | eps
<stmt> ::= <assignmentStmt> | <iterativeStmt> | <conditionalStmt> | <ioStmt> | <funCallStmt> | <exitStmt>
<assignmentStmt> ::= <singleOrRecId> TK_ASSIGNOP <arithmeticExpression> TK_SEM
<singleOrRecId> ::= TK_ID <option_single_constructed>
<option_single_constructed> ::= eps | <oneExpansion> <moreExpansions> | TK_SQL <arithmeticExpression> TK_SQR
<oneExpansion> ::= TK_DOT <fieldName>
<moreExpansions> ::= <oneExpansion> <moreExpansions> | eps
<funCallStmt> ::= <outputParameters> TK_CALL TK_FUNID TK_WITH TK_PARAMETERS <inputParameters> TK_SEM
<outputParameters> ::= TK_SQL <idList> TK_SQR TK_ASSIGNOP | eps
<inputParameters> ::= TK_SQL <idList> TK_SQR
<iterativeStmt> ::= TK_WHILE TK_OP <booleanExpression> TK_CL <stmt> <otherStmts> TK_ENDWHILE
<conditionalStmt> ::= TK_IF TK_OP <booleanExpression> TK_CL TK_THEN <stmt> <otherStmts> <elsePart>
<elsePart> ::= TK_ELSE <stmt> <otherStmts> TK_ENDIF | TK_ENDIF
<ioStmt> ::= TK_READ TK_OP <var> TK_CL TK_SEM | TK_WRITE TK_OP <var> TK_CL TK_SEM | TK_READCHAR TK_OP <singleOrRecId> TK_CL TK_SEM | TK_WRITECHAR TK_OP <arithmeticExpression> TK_CL TK_SEM | TK_PRINT TK_OP <printItem> <morePrintItems> TK_CL TK_SEM
<printItem> ::= TK_STR | <arithmeticExpression>
<morePrintItems> ::= TK_COMMA <printItem> <morePrintItems> | eps
<exitStmt> ::= TK_EXIT TK_OP <arithmeticExpression> TK_CL TK_SEM
<arithmeticExpression> ::= <term> <expPrime>
<expPrime> ::= <lowPrecedenceOperators> <term> <expPrime> | eps
<term> ::= <factor> <termPrime>
<termPrime> ::= <highPrecedenceOperators> <factor> <termPrime> | eps
<factor> ::= TK_OP <arithmeticExpression> TK_CL | <var>
<highPrecedenceOperators> ::= TK_MUL | TK_DIV
<lowPrecedenceOperators> ::= TK_PLUS | TK_MINUS
<booleanExpression> ::= TK_OP <booleanExpression> TK_CL <logicalOp> TK_OP <booleanExpression> TK_CL | <var> <relationalOp> <var> | TK_NOT TK_OP <booleanExpression> TK_CL
<var> ::= <singleOrRecId> | TK_NUM | TK_RNUM | TK_CHARLIT
<logicalOp> ::= TK_AND | TK_OR
<relationalOp> ::= TK_LT | TK_LE | TK_EQ | TK_GT | TK_GE | TK_NE
<returnStmt> ::= TK_RETURN <optionalReturn> TK_SEM
<optionalReturn> ::= TK_SQL <idList> TK_SQR | eps
<idList> ::= TK_ID <more_ids>
<more_ids> ::= TK_COMMA <idList> | eps
<definetypestmt> ::= TK_DEFINETYPE <A> TK_RUID TK_AS TK_RUID
<A> ::= TK_RECORD | TK_UNION
```

Changes relative to the current file:

| Non-terminal | Change |
|---|---|
| `<fieldDefinition>` | `TK_FIELDID` replaced by `<fieldName>` |
| `<fieldName>` | **new**: `TK_FIELDID \| TK_READCHAR \| TK_WRITECHAR \| TK_PRINT \| TK_EXIT` |
| `<declaration>` | `<arrayDim>` inserted after `<dataType>` |
| `<arrayDim>` | **new**: `TK_SQL TK_NUM TK_SQR \| eps` |
| `<stmt>` | new alternative `<exitStmt>` |
| `<option_single_constructed>` | new alternative `TK_SQL <arithmeticExpression> TK_SQR` |
| `<oneExpansion>` | `TK_FIELDID` replaced by `<fieldName>` |
| `<ioStmt>` | three new alternatives (`readchar`, `writechar`, `print`) |
| `<printItem>` | **new**: `TK_STR \| <arithmeticExpression>` |
| `<morePrintItems>` | **new**: `TK_COMMA <printItem> <morePrintItems> \| eps` |
| `<exitStmt>` | **new**: `TK_EXIT TK_OP <arithmeticExpression> TK_CL TK_SEM` |
| `<var>` | new alternative `TK_CHARLIT` |

`parserDef.h`: `MAX_RULES` must become **128** (113 rules do not fit in 100);
`MAX_NON_TERMINALS` becomes **64** (58 are used). `MAX_RHS` (20) and
`MAX_LINE_LEN` (1024; the longest line is 246 bytes) are sufficient.

Every token sequence accepted by the old grammar is accepted by the new one
(alternatives were only added where the old table had error entries, and the
two `TK_FIELDID` positions now also accept the old token). This was checked by
parsing every valid file of the repository with a prototype parser built from
the new grammar: no syntax errors; all `tests/syntax` files, `testcase1/2/6`
give the same error lines as before. The *shape* of parse trees changes
(`<arrayDim>` eps leaf in every declaration, a `<fieldName>` node above each
field name); the parse-tree tests only compare leaves, which are unchanged.

### A.2.2 LL(1) argument

FIRST and FOLLOW below were computed mechanically from the new file; PREDICT
of `A ::= α` is FIRST(α), plus FOLLOW(A) if α is nullable. LL(1) holds iff the
PREDICT sets of each non-terminal's alternatives are pairwise disjoint. Only
non-terminals whose PREDICT sets can change are listed; all others have
unchanged alternatives and FIRST sets, and the only FOLLOW sets that grow
(`<arithmeticExpression>`, `<expPrime>`, `<term>`, `<termPrime>`, `<factor>`,
`<var>`, `<singleOrRecId>`, `<option_single_constructed>`, `<moreExpansions>`,
`<dataType>`, `<declarations>`, `<typeDefinitions>`) are listed with their
nullable alternatives.

New/changed FIRST sets:

* FIRST(`<var>`) = FIRST(`<factor>`) − {TK_OP} = {TK_ID, TK_NUM, TK_RNUM, TK_CHARLIT}
* FIRST(`<arithmeticExpression>`) = FIRST(`<term>`) = FIRST(`<factor>`) = {TK_OP, TK_ID, TK_NUM, TK_RNUM, TK_CHARLIT}
* FIRST(`<printItem>`) = {TK_STR} ∪ FIRST(`<arithmeticExpression>`)
* FIRST(`<ioStmt>`) = {TK_READ, TK_WRITE, TK_READCHAR, TK_WRITECHAR, TK_PRINT}
* FIRST(`<stmt>`) = {TK_ID, TK_WHILE, TK_IF, TK_READ, TK_WRITE, TK_READCHAR, TK_WRITECHAR, TK_PRINT, TK_SQL, TK_CALL, TK_EXIT}
* FIRST(`<booleanExpression>`) = {TK_OP, TK_NOT, TK_ID, TK_NUM, TK_RNUM, TK_CHARLIT}
* FIRST(`<option_single_constructed>`) = {TK_DOT, TK_SQL, eps}; FIRST(`<arrayDim>`) = {TK_SQL, eps}; FIRST(`<morePrintItems>`) = {TK_COMMA, eps}

New/changed FOLLOW sets:

* FOLLOW(`<arithmeticExpression>`) = FOLLOW(`<expPrime>`) = {TK_SEM, TK_CL, TK_SQR, TK_COMMA}
  (TK_SEM: assignment; TK_CL: `( )`, `writechar`, `exit`, last print item;
  TK_SQR: index; TK_COMMA: print item)
* FOLLOW(`<term>`) = FOLLOW(`<termPrime>`) = {TK_PLUS, TK_MINUS} ∪ FOLLOW(`<arithmeticExpression>`)
* FOLLOW(`<factor>`) = {TK_MUL, TK_DIV} ∪ FOLLOW(`<term>`)
* FOLLOW(`<var>`) = FOLLOW(`<factor>`) ∪ {TK_LT, TK_LE, TK_EQ, TK_GT, TK_GE, TK_NE}
  (`<booleanExpression>`'s second `<var>` adds FOLLOW(`<booleanExpression>`) = {TK_CL}; `read`/`write` add TK_CL)
* FOLLOW(`<singleOrRecId>`) = FOLLOW(`<option_single_constructed>`) = FOLLOW(`<moreExpansions>`)
  = {TK_ASSIGNOP, TK_MUL, TK_DIV, TK_PLUS, TK_MINUS, TK_SEM, TK_CL, TK_SQR, TK_COMMA, TK_LT, TK_LE, TK_EQ, TK_GT, TK_GE, TK_NE}
  (`readchar(` `<singleOrRecId>` `)` adds only TK_CL)
* FOLLOW(`<printItem>`) = {TK_COMMA, TK_CL}; FOLLOW(`<morePrintItems>`) = {TK_CL}
* FOLLOW(`<arrayDim>`) = {TK_COLON}; FOLLOW(`<dataType>`) = {TK_ID, TK_COLON, TK_SQL}
* FOLLOW(`<declarations>`) = FIRST(`<otherStmts>`) − {eps} ∪ {TK_RETURN}
  = {TK_ID, TK_WHILE, TK_IF, TK_READ, TK_WRITE, TK_READCHAR, TK_WRITECHAR, TK_PRINT, TK_EXIT, TK_SQL, TK_CALL, TK_RETURN}
* FOLLOW(`<otherStmts>`) unchanged = {TK_RETURN, TK_ENDWHILE, TK_ENDIF, TK_ELSE}

PREDICT sets of every non-terminal with a new or nullable alternative:

| Non-terminal | Alternative | PREDICT |
|---|---|---|
| `<stmt>` | `<assignmentStmt>` | TK_ID |
| | `<iterativeStmt>` | TK_WHILE |
| | `<conditionalStmt>` | TK_IF |
| | `<ioStmt>` | TK_READ TK_WRITE TK_READCHAR TK_WRITECHAR TK_PRINT |
| | `<funCallStmt>` | TK_SQL TK_CALL |
| | `<exitStmt>` | TK_EXIT |
| `<ioStmt>` | 5 alternatives | TK_READ / TK_WRITE / TK_READCHAR / TK_WRITECHAR / TK_PRINT (one each) |
| `<printItem>` | `TK_STR` | TK_STR |
| | `<arithmeticExpression>` | TK_OP TK_ID TK_NUM TK_RNUM TK_CHARLIT |
| `<morePrintItems>` | `TK_COMMA ...` | TK_COMMA |
| | `eps` | TK_CL |
| `<var>` | 4 alternatives | TK_ID / TK_NUM / TK_RNUM / TK_CHARLIT |
| `<factor>` | `TK_OP ...` / `<var>` | TK_OP / TK_ID TK_NUM TK_RNUM TK_CHARLIT |
| `<booleanExpression>` | `TK_OP ...` / `<var> ...` / `TK_NOT ...` | TK_OP / TK_ID TK_NUM TK_RNUM TK_CHARLIT / TK_NOT |
| `<option_single_constructed>` | `eps` | FOLLOW(`<singleOrRecId>`) above (contains neither TK_DOT nor TK_SQL) |
| | `<oneExpansion> <moreExpansions>` | TK_DOT |
| | `TK_SQL <arithmeticExpression> TK_SQR` | TK_SQL |
| `<moreExpansions>` | `<oneExpansion> ...` / `eps` | TK_DOT / FOLLOW(`<singleOrRecId>`) (no TK_DOT) |
| `<arrayDim>` | `TK_SQL TK_NUM TK_SQR` / `eps` | TK_SQL / TK_COLON |
| `<fieldName>` | 5 alternatives | TK_FIELDID / TK_READCHAR / TK_WRITECHAR / TK_PRINT / TK_EXIT |
| `<otherStmts>` | `<stmt> <otherStmts>` / `eps` | FIRST(`<stmt>`) / TK_RETURN TK_ENDWHILE TK_ENDIF TK_ELSE |
| `<declarations>` | `<declaration> ...` / `eps` | TK_TYPE / FOLLOW(`<declarations>`) above (no TK_TYPE) |
| `<typeDefinitions>` | `<actualOrRedefined> ...` / `eps` | TK_RECORD TK_UNION TK_DEFINETYPE / {TK_TYPE} ∪ FOLLOW(`<declarations>`) |
| `<expPrime>` | `<lowPrecedenceOperators> ...` / `eps` | TK_PLUS TK_MINUS / TK_SEM TK_CL TK_SQR TK_COMMA |
| `<termPrime>` | `<highPrecedenceOperators> ...` / `eps` | TK_MUL TK_DIV / TK_PLUS TK_MINUS TK_SEM TK_CL TK_SQR TK_COMMA |

All rows are pairwise disjoint. The critical points: (1) `[` never follows a
variable reference (statements starting with `[` are calls and begin after
`;` or a keyword), so `<option_single_constructed>` can use `TK_SQL` for
indexing; (2) a string can only be a print item and `TK_STR` is not in
FIRST(`<arithmeticExpression>`); (3) `<arrayDim>` sits between `<dataType>`
(non-nullable) and `TK_COLON`; (4) `<fieldName>` is only reached after
`TK_DOT` or `TK_COLON` inside a field definition, so the keyword tokens
there cannot be confused with statements.

Why conditions keep `<var> relop <var>` (no arithmetic expressions): an
arithmetic operand may start with `(`, and so may a parenthesised boolean
(`TK_OP <booleanExpression> ...`) — that would be an LL(1) conflict. With
indexing, `<var>` already covers what a scanner needs (`c2[b3] == '\n'`).

### A.2.3 Unit-test values that change (`tests/unit/test_units.c`)

```c
  CHECK_EQ_INT(grammar->numNT, 58);
  CHECK_EQ_INT(grammar->numRules, 113);
  CHECK_EQ_INT(epsRules, 17);
```

`test_first_sets` (new arrays; `END` terminates as today):

```c
  int stmt[] = {TK_ID, TK_WHILE, TK_IF, TK_READ, TK_WRITE, TK_READCHAR,
                TK_WRITECHAR, TK_PRINT, TK_SQL, TK_CALL, TK_EXIT, END};
  int otherStmts[] = {TK_ID, TK_WHILE, TK_IF, TK_READ, TK_WRITE, TK_READCHAR,
                      TK_WRITECHAR, TK_PRINT, TK_SQL, TK_CALL, TK_EXIT,
                      TK_EPS, END};
  int boolExpr[] = {TK_OP, TK_NOT, TK_ID, TK_NUM, TK_RNUM, TK_CHARLIT, END};
  int stmts[] = {TK_RECORD, TK_UNION, TK_DEFINETYPE, TK_TYPE, TK_ID,
                 TK_WHILE, TK_IF, TK_READ, TK_WRITE, TK_READCHAR,
                 TK_WRITECHAR, TK_PRINT, TK_SQL, TK_CALL, TK_EXIT,
                 TK_RETURN, END};
  int optSingle[] = {TK_DOT, TK_SQL, TK_EPS, END};
  int arith[] = {TK_OP, TK_ID, TK_NUM, TK_RNUM, TK_CHARLIT, END};
```

(`program`, `typeDefs`, `dataType` unchanged.) `test_follow_sets`:

```c
  int arith[] = {TK_SEM, TK_CL, TK_SQR, TK_COMMA, END};      // arithmeticExpression, expPrime
  int termPrime[] = {TK_PLUS, TK_MINUS, TK_SEM, TK_CL, TK_SQR, TK_COMMA, END};
  int decls[] = {TK_ID, TK_WHILE, TK_IF, TK_READ, TK_WRITE, TK_READCHAR,
                 TK_WRITECHAR, TK_PRINT, TK_EXIT, TK_SQL, TK_CALL,
                 TK_RETURN, END};
  int singleOrRec[] = {TK_ASSIGNOP, TK_MUL, TK_DIV, TK_PLUS, TK_MINUS,
                       TK_SEM, TK_CL, TK_SQR, TK_COMMA, TK_LT, TK_LE,
                       TK_EQ, TK_GT, TK_GE, TK_NE, END};
```

(`program`, `otherStmts`, `otherFunctions`, `moreIds`, `remaining`,
`boolExpr` follow sets unchanged.) `test_grammar_is_ll1` and
`test_parse_table` need no change and must pass.

`test_bitset` hard-codes bit numbers that the enlarged enum moves (`TK_EPS`
becomes 63 and `NUM_TOKENS - 1` becomes 65). Change exactly two lines:

```c
  CHECK(!bs_contains(&a, 1) && !bs_contains(&a, 62));   // line 149, was 65
  CHECK(bs_contains(&c, 64));                           // line 158, was 63
```

(line 149: bits 0, 63, 64 and 65 are now set, so 65 can no longer be the
"absent" probe; line 158: bit 63 is `TK_EPS`, which `bs_union_no_eps`
removes, so the surviving bit to probe is 64.)

---

## A.3 Character literals

Syntax: `TK_CHARLIT` is a new alternative of `<var>`, so a character literal
can appear wherever an integer literal can: in arithmetic expressions, as a
relational operand, as the argument of `write`, as a print item, inside an
index. Examples: `if (b2 == '\n') then`, `b3 <--- b2 - '0';`,
`write('A');` (prints `65`).

Static semantics: type `int`; value per A.1.3. `read('a');` reports the
existing message `read needs a variable, not the constant 'a'`.

AST: an `EXPR_NUM` node with `text` = the lexeme as written (e.g. `'\n'`),
`ival` = the value, and a new flag `int isChar = 1`. Semantic analysis must
not re-parse `text` for such a node (the existing `checkIntLiteral` is skipped
when `isChar` is set). Everything else treats it as an int literal: `--ast`
prints it as written (`'\n'`).

Dynamic semantics / codegen: identical to an int literal (`mov rax, <value>`).

---

## A.4 Byte input: `readchar`

Syntax: `readchar(<singleOrRecId>);` — a variable, a record field path, or an
array element. Constants are a syntax error.

Static semantics:
* The target's type must be `int`: otherwise
  `readchar needs an int variable, not <path> of type <type>` where `<path>`
  is the reference as text (`c2`, `d2.y`, `b2[...]`, see A.7.6) and `<type>`
  its type name (`real`, `#pt`).
* A bare array name is the array error of A.7.5.
* The target counts as assigned for the while rule (A.9) and for the rule
  "returned outputs must be assigned".

Dynamic semantics: reads one byte from standard input with C `getchar()`; the
target receives the byte value 0..255. At end of input (or on a read error)
the target receives -1, and every later `readchar` also gives -1. `read` and
`readchar` share the C `stdin` stream: after `read(b2)` consumed `42` from the
line `42 z`, the next `readchar` returns the space (32), then `z`, then `\n`
(10). For an element target, the index is evaluated and bounds-checked
*before* any input is consumed.

Codegen (scalar target):

```nasm
    call getchar wrt ..plt
    movsxd rax, eax                 ; EOF (-1) stays -1, bytes are 0..255
    mov qword <target>, rax
```

Element target: evaluate the index, bounds-check, `lea rcx, <base>`,
`lea rax, [rcx + rax*8]`, `push rax`, `sub rsp, 8`, `call getchar wrt ..plt`,
`add rsp, 8`, `pop rcx`, `movsxd rax, eax`, `mov qword [rcx], rax`. (The
push/sub pair keeps `rsp` 16-byte aligned at the call.)

---

## A.5 Output without newlines: `print` and `writechar`

### A.5.1 `print`

Syntax: `print(<printItem> {, <printItem>});` with at least one item; an item
is a string literal or an arithmetic expression.

Static semantics: each expression item must have type `int` or `real`;
otherwise `print needs int or real values, not <type>` (e.g. a record or a
record expression). An item whose type is already an error reports nothing
more (e.g. the array error of A.7.5, an undeclared variable). Each expression
item is subject to the depth limit (A.10).

Dynamic semantics: items are evaluated and written left to right, each one
written before the next one is evaluated:
* string: its decoded bytes, unchanged;
* int: decimal, as `printf("%lld")` (e.g. `-2`);
* real: as `printf("%.2f")` (e.g. `2.50`).
No separator and no newline are added. Output goes to stdout through C stdio,
like `write`, so the relative order of `print`, `write`, `writechar` and
runtime-error messages is the program order.

Codegen per item (rsp is 16-byte aligned at statement level, and every
expression leaves the stack balanced):

```nasm
    ; string item k
    lea rsi, [rel S<k>]
    lea rdi, [rel fmt_str]          ; db "%s", 0
    xor eax, eax
    call printf wrt ..plt
    ; int item
    <genInt expr>                   ; rax
    mov rsi, rax
    lea rdi, [rel fmt_int]          ; db "%lld", 0
    xor eax, eax
    call printf wrt ..plt
    ; real item
    <genReal expr>                  ; xmm0
    lea rdi, [rel fmt_real]         ; db "%.2f", 0
    mov eax, 1
    call printf wrt ..plt
```

Each string literal is emitted once as `S<k>: db <b1>, <b2>, ..., 0` with
every byte written as a decimal number (so no NASM quoting issues; `""` is
`S<k>: db 0`). Labels are numbered from 0 in the order the strings are
generated. They may be emitted in a `section .data` block placed before
`.text` (pre-pass) or inline by switching sections around the use
(`section .data` / `S<k>: ...` / `section .text`); both are valid NASM.

### A.5.2 `writechar`

Syntax: `writechar(<arithmeticExpression>);`

Static semantics: the expression must be `int`, else
`writechar needs an int value, not <type>`.

Dynamic semantics: writes one byte, the value modulo 256 (its low 8 bits;
`writechar(256 + 'A')` writes `A`), with C `putchar`.

Codegen: `<genInt expr>`, `mov edi, eax`, `call putchar wrt ..plt`.

### A.5.3 Unchanged

`write(<var>)` keeps its exact behaviour (one value or record per line). A
string is not a `<var>`: `write("x");` is a syntax error; use `print`.

---

## A.6 `exit`

Syntax: `exit(<arithmeticExpression>);` — a statement allowed anywhere a
statement is.

Static semantics: the expression must be `int`, else
`exit needs an int value, not <type>`. It does not replace `return`: every
function still ends with its return statement.

Dynamic semantics: flushes all output and terminates the program with exit
status `value mod 256` (low 8 bits: `exit(3)` gives 3, `exit(256 + 4)` gives
4, `exit(0 - 1)` gives 255). Implemented with C `exit`:
`<genInt expr>`, `mov edi, eax`, `call exit wrt ..plt`.

---

## A.7 Arrays

### A.7.1 Declarations

Syntax: `type <dataType>[<N>] : <id> [: global];` where `<N>` is a `TK_NUM`
literal, e.g.

```
	type int[33] : d2c : global;
	type real[4] : c2;
```

Static semantics:
* `<N>` must be 1..131072 (so an array is at most 1 MiB = `MAX_TYPE_SIZE`):
  else `array length must be between 1 and 131072, not <lexeme>` (`<lexeme>`
  as written, e.g. `0`, `131073`, `99999999999999999999`). Leading zeros are
  allowed (`007` is 7).
* The element type must be `int` or `real`: else
  `array element type must be int or real, not <type>` with the resolved type
  name (for an alias, the defined record's name). This message replaces the
  union-variable message for `type #u[3] : b2;`. An undefined element type
  gives only the existing `undefined type <name>`.
* Both checks are made, length first: `type #pt[0] : b2;` reports
  `array length must be between 1 and 131072, not 0` and then
  `array element type must be int or real, not #pt` (both on the line of the
  declaration). With an alias of a record as element type
  (`definetype record #pt as #ali` ... `type #ali[2] : b3;`) the message names
  the record: `array element type must be int or real, not #pt`. An undefined
  element type (`type #zz[3] : b5;`) reports only `undefined type #zz` (plus the
  length message if the length is also wrong).
* After any of these errors the variable is declared with the error type
  (size 8), so its uses are silent: indexing it, using it in expressions or
  assigning to it reports nothing more.
* Arrays are allowed only in `<declaration>`: as locals (in any function,
  including recursive ones) and as globals. They cannot be parameters
  (`<parameter_list>` takes a plain `<dataType>`: syntax error), record or
  union fields (syntax error), or elements of arrays (no syntax).
* The existing rules apply unchanged: a local may not have the name of a
  global; names are unique per function; the locals of a function together are
  at most 4 MiB (`MAX_FRAME_SIZE`), counting arrays.

Symbol table (`symbolTable.h`): new `TypeKind` value `TY_ARRAY`; `Type`
gains `struct Type *elem; int length;`. Array types are interned per
(element type, length) in the `SymbolTable` (new array `Type **arrays`,
freed by `freeSymbolTable`), so two `int[10]` declarations share one `Type`.
`name` is `"<elem>[<length>]"` in decimal (`"int[10]"`, `"real[131072]"`; at
most 12 characters), `size = 8 * length`, `isScalar` and `isAggregate` are
false for arrays.

### A.7.2 Element access

Syntax: `<id>[<arithmeticExpression>]` (the new alternative of
`<option_single_constructed>`). An element access can appear wherever a
`<singleOrRecId>` can: assignment target, operand of an arithmetic
expression, operand of a relational operator, argument of `read`, `write`,
`readchar`, and (through expressions) items of `print`, arguments of
`writechar`/`exit`, and other indices. It cannot be combined with field
access (`b2[1].x`, `d2.x[1]`: syntax errors) and cannot be passed in a call's
parameter lists, which take identifiers only (copy an element to a scalar
first).

Static semantics:
* The variable must be an array: else `<name> is not an array (it has type <type>)`
  (e.g. `b3 is not an array (it has type int)`); an undeclared name gives only
  `variable <name> is not declared` (once per function, as today); a variable
  whose type is already the error type (failed declaration) reports nothing.
  The index is still checked in all three cases, and the access then has the
  **error type**, which suppresses every follow-on message for it (e.g.
  `b7 <--- b7[0] + 1.50;` with `b7` an int reports only
  `b7 is not an array (it has type int)`, no type mismatch).
* The index must have type `int` (a char literal is an int): else
  `array index must be an int expression, not <type>`.
* The type of the access is the element type. All existing rules apply:
  `int` converts to `real` on assignment, `real` to `int` is the existing
  `type mismatch: cannot assign a value of type real to <path> of type int`.
* Static bounds are never checked, even for constant indices (`b2[10]` on an
  `int[10]` compiles and fails at run time).

### A.7.3 Dynamic semantics

* Elements are 8-byte ints / doubles. Local arrays are zeroed on every entry
  to their function (they are part of the locals, which are already zeroed);
  global arrays are zero at program start (`.bss`). In a recursive function
  each activation has its own local arrays.
* Every element access evaluates the index (an int) and checks
  `0 <= index < length`. On failure the program writes
  `Runtime error: index <i> out of bounds for array <name> of length <n> at line <L>`
  and a newline to stdout and exits with status 1, where `<i>` is the index
  in signed decimal, `<name>` the array variable, `<n>` its length and `<L>`
  the line of the array name in the source. Output written earlier appears
  first.
* Evaluation order: in `<element> <--- <expr>;` the right-hand side is
  evaluated first, then the index (so `c2[7] <--- 1 / b2;` with `b2 = 0`
  reports division by zero even if 7 is out of bounds). In `read` / `readchar`
  the index is evaluated before input is consumed. In a relational operator
  the right operand is evaluated first, then the left one (unobservable
  except for which runtime error is reported first; specified for
  determinism). `&&&` and `@@@` short-circuit (unchanged behaviour of
  `genCond`, now observable): in `(b2 < 3) &&& (c2[b2] == 0)` the element
  is not accessed when `b2 >= 3`.

### A.7.4 Memory layout and codegen

Arrays use the existing layout: a local array occupies
`[rbp - offset, rbp - offset + 8*length)` (its `VarEntry.offset` is computed
exactly as for records); a global array is `G_<name>: resb <8*length>` in
`.bss`. Element `i` is at `base + 8*i` where `base` is `lea rcx, [rbp - offset]`
(local) or `lea rcx, [rel G_<name>]` (global). Parameters are never arrays.

Element load (int; real uses `movsd xmm0, qword [rcx + rax*8]`):

```nasm
    <genInt index>                  ; rax = index
    cmp rax, <length>
    jb L<ok>                        ; unsigned: negative indices fail too
    mov rsi, rax                    ; index
    lea rdx, [rel AN_<name>]        ; db "<name>", 0  (one per array name)
    mov rcx, <length>
    mov r8, <line>
    jmp rt_index_error
L<ok>:
    lea rcx, <base>
    mov rax, qword [rcx + rax*8]
```

```nasm
rt_index_error:                     ; emitted once
    and rsp, -16                    ; may be reached mid-expression
    lea rdi, [rel msg_index]
    xor eax, eax
    call printf wrt ..plt
    mov edi, 1
    call exit wrt ..plt
msg_index: db "Runtime error: index %lld out of bounds for array %s of length %lld at line %lld", 10, 0
```

Element store `a[i] <--- e` (int): `<genInt e>`, `push rax`, `<genInt i>`,
check, `lea rcx, <base>`, `pop rdx`, `mov qword [rcx + rax*8], rdx`. Real:
`<genReal e>` (which converts an int expression), `sub rsp, 8`,
`movsd qword [rsp], xmm0`, index, check, `lea rcx, <base>`,
`movsd xmm0, qword [rsp]`, `add rsp, 8`, `movsd qword [rcx + rax*8], xmm0`.

`read(a[i])`: index, check, `lea rcx, <base>`, `lea rsi, [rcx + rax*8]`, then
the existing `scanf` call with `fmt_read_int`/`fmt_read_real`.
`write(a[i])`: `<genInt>` → `mov rsi, rax` → `fmt_int_nl`, or `<genReal>` →
`fmt_real_nl` (same output format as a scalar variable).

`genCond` (relational operators) must no longer assume that evaluating one
operand leaves the other register alone. New sequence for every relational
test: int — `<genInt rhs>`, `push rax`, `<genInt lhs>`, `pop rcx`,
`cmp rax, rcx`, then the existing jumps; real — `<genReal rhs>`, `sub rsp, 8`,
`movsd qword [rsp], xmm0`, `<genReal lhs>`, `movsd xmm1, qword [rsp]`,
`add rsp, 8`, then the existing `ucomisd` sequences (unchanged register
roles: lhs in xmm0, rhs in xmm1).

`genInt` of a binary operator keeps `push rax / genInt(right) / mov rcx, rax /
pop rax` — correct because an element load returns its value in rax and only
clobbers rcx before that.

### A.7.5 Whole arrays

An array name without an index ("bare array") may appear only in its
declaration. Everywhere else:

* in an expression (including conditions and print items), as an assignment
  target, or as the argument of `read`, `write`, `readchar`:
  `array <name> cannot be used without an index` (and no further message for
  that statement part: no type mismatch, no "cannot write", no print/readchar
  type message);
* in a call's input or output list, or in a return list:
  `array <name> cannot be passed to or returned from a function`; for that
  position no type-mismatch message and (for return lists) no "is never
  assigned a value" message is reported; arity messages are unaffected;
* `b2.x` on an array: the existing `b2 has type int[10] and has no field x`.

There is no whole-array assignment, comparison, arithmetic, `read` or `write`.

### A.7.6 Text of a reference in messages

`varText` (used by messages such as type mismatch and readchar) renders an
element access as `<name>[...]` (three literal dots), e.g.
`type mismatch: cannot assign a value of type real to b2[...] of type int`.

---

## A.8 Recursion and calls to functions defined later

### A.8.1 Rules relaxed

* Removed: `function <f> cannot call itself (recursion is not allowed)`.
* Removed: `function <f> is called before it is defined`.

A call may name any function of the program except `_main` (which is
`TK_MAIN`, not `TK_FUNID`, so it cannot be named in a call). Signatures are
already collected for all functions before bodies are checked
(`collectFunctions`), so nothing else changes in `checkCall`. NASM resolves
the `F<name>` labels regardless of order. Copy-in/copy-out, zeroed outputs
and locals, and the return parallel assignment work per activation as today.

### A.8.2 Static stack check (replaces `checkStackUse`)

Definitions per function f (existing quantities):
`temp(f) = max(tempStack(f->ast->stmts), sum of the sizes of f's outputs)`,
`maxParam(f)` = the largest `paramSize` of any callee of a call statement in f
(0 if none), and
`need(f) = 16 + localSize(f) + max(temp(f), maxParam(f))` — the bytes one
activation of f uses itself, including the parameter block it reserves for a
callee but excluding the callee's own use.

1. Build the call graph (edge f → g for every call statement in f whose callee
   resolved; the current filter `callee->index < cur->index` in `callStack`
   disappears). Compute strongly connected components (Tarjan's algorithm;
   recursion depth is bounded by the number of functions).
2. f is *recursive* if its component has two or more functions or f calls
   itself. f is *unbounded* if it is recursive or calls an unbounded
   function; otherwise *bounded*.
3. For bounded f, in callees-first order (reverse topological order of the
   component DAG, i.e. the order Tarjan emits components):
   `use(f) = 16 + localSize(f) + max(temp(f), max over calls f→g of (paramSize(g) + use(g)))`
   — exactly the existing formula. The existing error
   `<f> needs about <use> bytes of stack, counting the functions it calls; the limit is 6291456`
   is reported at f's line when `use(f) > MAX_STACK_USE`, unless some callee
   already exceeds it (existing suppression).
4. For unbounded f compute
   `single(f) = 16 + localSize(f) + max(temp(f), max over calls f→g with g bounded of (paramSize(g) + use(g)), max over calls f→g with g unbounded of paramSize(g))`
   — one activation of f plus the full use of every bounded callee (each
   unbounded callee is checked on its own and at run time) — and report
   `<f> needs about <single> bytes of stack for a single call; the limit is 6291456`
   at f's line (the line of its `TK_FUNID`, or of `_main`) when
   `single(f) > MAX_STACK_USE`. Unbounded functions are processed after all
   bounded ones, so every `use(g)` needed is known. If a bounded callee itself
   exceeds the limit, this message is still reported for f (the callee has its
   own message). Example (`tests/semantic/stack_mixed.txt`): `_main` with
   three `int[131072]` arrays and two int locals (`localSize` 3145744) that
   calls a recursive `_h` (16-byte block) and a bounded `_g` (16-byte block,
   three `int[131072]` locals, `use(_g)` = 16 + 3145728 + 8016 = 3153760)
   gives
   `_main needs about 6299536 bytes of stack for a single call; the limit is 6291456`
   (`16 + 3145744 + 16 + 3153760`), although each function alone is within
   the limits.

For every currently valid program the call graph is acyclic and every
function is bounded, so the results are identical to today.

### A.8.3 Runtime stack guard

Deep or infinite recursion cannot be checked statically, so the generated
code checks the stack at every function entry.

* `.bss` gains `rt_stack_top: resq 1`.
* First instructions of `main` (before `push rbp`):
  `lea rax, [rsp + 8]` / `mov [rel rt_stack_top], rax`.
* First instructions of every other function `F<name>` (before `push rbp`):

  ```nasm
      mov rax, [rel rt_stack_top]
      sub rax, rsp
      cmp rax, <6291456 + 8 - need(f)>
      jg rt_stack_overflow
  ```

  i.e. the stack used by all callers down to f's return address, plus f's own
  activation, must stay within `MAX_STACK_USE` (6 MiB). For a bounded chain
  this sum never exceeds the static `use(_main)`, so valid programs that run
  today never trip the guard (the accounting matches A.8.2 exactly). The
  immediate is positive because a compiled program has `need(f) <= 6 MiB`.
* ```nasm
  rt_stack_overflow:              ; emitted once
      and rsp, -16
      lea rdi, [rel msg_stack]
      xor eax, eax
      call printf wrt ..plt
      mov edi, 1
      call exit wrt ..plt
  msg_stack: db "Runtime error: stack overflow (recursion too deep)", 10, 0
  ```

At run time, a call chain deeper than the budget prints
`Runtime error: stack overflow (recursion too deep)` (after all earlier
output) and exits with status 1, instead of crashing with SIGSEGV. With 6 MiB
used, 2 MiB of the usual 8 MiB stack remain for `printf`. The depth reachable
depends on the actual frame sizes: e.g. a function with one int input, one int
output and one int local uses 48 bytes per level (8 return address + 8 saved
rbp + 16 locals + 16 parameter block), so 100000 levels (4.8 MB) run fine.

---

## A.9 While rule (relaxed and extended)

The rule "a while body must update a variable of its condition" keeps its
message (`none of the variables in the while condition is updated inside the
loop`) and its overlap logic, with these changes:

* Variables read by a condition (`collectCondVars`): for a reference with an
  index, the **whole** array, plus every variable referenced anywhere in the
  index expression (recursively, including indices of indices). Literals and
  char literals contribute nothing (so `while ('a' < 'b')` is still an error).
* Things a body may change (`collectAssigned`):
  * assignment to an element, `read` of an element, `readchar` of an element:
    the whole array;
  * `readchar(<scalar or field path>)`: that piece (like `read`);
  * **a call statement: its output variables (as today) and every global
    variable (whole)**. This is a relaxation: a loop like
    `while (d4 == '+') ... call _next with parameters [b2]; ... endwhile`,
    where `_next` updates the global `d4`, is now accepted. No interprocedural
    analysis is done: any call counts, whatever the callee does.
  * `print`, `writechar`, `exit`, `write`: nothing.
* The check is skipped when any variable of the condition, including inside
  index expressions, failed to resolve (`condHasUndeclared` must recurse into
  index expressions), **and** when any relational operand of the condition
  has the error type — e.g. a bare array (`while (c2 == 0)`, which reports
  only `array c2 cannot be used without an index`) or an index on a non-array
  (`while (b3[0] < 1)`, only `b3 is not an array (it has type int)`), or a
  too-deep operand. So the while message never adds to another error in the
  same condition.

---

## A.10 Expression depth and temporaries

* `depth` of an element access = 1 + depth of its index; of a literal or
  scalar/field variable 0 (unchanged). `fold` and the "truncated" marking work
  unchanged, so an index nested more than 1000 deep is truncated and rejected.
* The depth limit (existing message `expression is too long or too deeply
  nested (more than 1000 operators deep)`, at the statement's line) is
  checked for every expression root: assignment right-hand side (existing),
  the index of an assignment target, each relational operand, the index of a
  `read`/`write`/`readchar` argument, each print item, the argument of
  `writechar` and `exit`. At most one such message per root. A root that is
  too deep is not type-checked and counts as having the error type, so no
  other message is reported for it (for the index of a target, the target
  counts as error-typed: no type-mismatch message).
* `tempStack` keeps its bound `8 * (MAX_EXPR_DEPTH + 2)`: an element store or
  a relational test adds one 8-byte slot to at most 1000 levels of
  expression temporaries, and `readchar` into an element adds 16 bytes after
  the index temporaries are popped; both fit in the 2 spare slots.

---

## A.11 AST and printers

### A.11.1 AST (`ast.h`)

```c
typedef struct {            // AstVarRef gains:
  ...
  struct Expr *index;       // element access: the index; NULL otherwise
} AstVarRef;                // (numFields == 0 whenever index != NULL)

typedef struct Expr {       // Expr gains:
  ...
  int isChar;               // EXPR_NUM written as a character literal
} Expr;

typedef enum { STMT_ASSIGN, STMT_WHILE, STMT_IF, STMT_READ, STMT_WRITE,
               STMT_CALL, STMT_READCHAR, STMT_WRITECHAR, STMT_PRINT,
               STMT_EXIT } StmtKind;

typedef struct PrintItem {
  int isString;
  char *text;               // isString: decoded bytes, NUL-terminated (owned)
  int len;
  Expr *expr;               // !isString
  struct PrintItem *next;
} PrintItem;

// Stmt gains: PrintItem *items;   (STMT_PRINT)
//   STMT_READCHAR uses ioArg (an EXPR_VAR), STMT_WRITECHAR and STMT_EXIT use
//   ioArg (any expression).
// Decl gains: int isArray; char lengthText[AST_NAME_LEN];
```

Builder notes (`ast.c`): in `<declaration>` the child indices shift — `kid(d,1)`
`<dataType>`, `kid(d,2)` `<arrayDim>`, `kid(d,4)` `TK_ID`, `kid(d,5)`
`<global_or_not>`. Field names are now one level down:
`kid(kid(one,1),0)` in `buildVarRef` and `kid(kid(n,3),0)` in `buildField`.
`<option_single_constructed>` with first child `TK_SQL` builds `index` from
`kid(opt,1)`. Statement lines: `readchar`/`writechar`/`print`/`exit` use the
keyword's line. `freeAST` frees `index` expressions and print items.

### A.11.2 `--ast` (and menu option 5)

* Element access: `<name>[<index>]` with the index printed by `printExpr`
  (fully parenthesised), e.g. `c3[(b3 - 1)]`, `c2[c2[0]]`.
* Character literal: as written, e.g. `'\t'`.
* Declarations: `Declare c2 : int[16] (global) (line 41)`; the length is the
  lexeme as written. Parameters never have a length.
* New statement lines (same indentation scheme as existing ones):
  `ReadChar (line N): <ref>`, `WriteChar (line N): <expr>`,
  `Exit (line N): <expr>`, and
  `Print (line N): <item>, <item>, ...` where a string item is printed in
  double quotes with `\n`, `\t`, `\r`, `\\`, `\"` escaped and every other
  byte as is (e.g. `"count: "`, `"\n"`), and an expression item by
  `printExpr`.

### A.11.3 `--symbols` (and menu option 6)

Arrays appear only in the variable rows, with the array type name and size,
e.g. `c2  global  int[16]  128  G_c2  41` and
`c3  local  real[4]  32  rbp-64  42`. The "Types" table lists only records and
unions, as today.

### A.11.4 `--parse`

See A.1.6 (spaces in literal lexemes shown as `\x20`). The `value` column is
filled only for `TK_NUM`/`TK_RNUM`, as today.

---

## A.12 Codegen summary

* `extern printf, scanf, exit, getchar, putchar`.
* `.data` gains `fmt_int: db "%lld", 0`, `fmt_real: db "%.2f", 0`,
  `fmt_str: db "%s", 0`, `msg_index`, `msg_stack`, one `AN_<name>: db
  <bytes>, 0` per distinct array variable name, and the `S<k>` strings.
  Existing formats and `msg_div_zero` are unchanged.
* `.bss` gains `rt_stack_top: resq 1` and holds global arrays like any global.
* New runtime routines `rt_index_error` and `rt_stack_overflow` next to
  `rt_div_by_zero`; all three print on stdout through `printf` and `exit(1)`.
* The comment emitted per statement (`; line N: <kind>`) uses the names
  `readchar`, `writechar`, `print`, `exit` for the new statements.
* Header comments of `codegen.c` and `semantic.c` must be updated to describe
  arrays, the stack guard and the new pass order (A.8.2).

---

## A.13 Backward compatibility

Every currently valid program keeps its meaning and output:

* Lexical: `'` and `"` were errors (`' not recognized`), so no valid program
  contains them outside comments. The four new keywords were valid only as
  field names, which remain valid (`<fieldName>`).
* Syntax: only alternatives were added (A.2.1).
* Semantics: errors were only removed (A.8.1, A.9) or added for constructs
  that did not parse before (arrays, new statements) or that were already
  errors (the single-call stack check concerns only programs with recursion,
  which were rejected before).
* Code: the same values are computed and printed; the added stack guard
  provably never fires for programs that pass today's static check (A.8.3).

Existing rules that are relaxed, and the files that encode them:

| Rule | Status | Files that change |
|---|---|---|
| a function may only call functions defined earlier | removed | `tests/semantic/call_order.txt`: the `%? error: function _second is called before it is defined` annotation (line 5) and `%? error: function _second cannot call itself` (line 11) are removed and the header comment updated (new file content in C.3) |
| a function cannot call itself | removed | same file |
| static stack limit over the whole call chain | now only for non-recursive chains; recursive ones get a per-activation check + runtime guard | none (`tests/semantic/limits.txt` keeps passing: `_main` is bounded) |
| while body must update a condition variable | a call statement now updates every global | none (no existing test has a global in a loop condition with a call in the body) |
| `TK_FIELDID` after `.` and in field definitions | now `<fieldName>` | none |
| grammar sizes | 53/95/15 → 58/113/17 | `tests/unit/test_units.c`: counts and FIRST/FOLLOW arrays (A.2.3) |
| token numbering (`TK_EPS` 57 → 63, `NUM_TOKENS` 60 → 66) | changed | `tests/unit/test_units.c`: `test_bitset` lines 149 and 158 (A.2.3) |

Of the existing files under `tests/`, exactly these change:
`tests/semantic/call_order.txt` (replaced) and `tests/unit/test_units.c` (the
edits listed in A.2.3). `tests/run_tests.py` only gains new cases and the toy
section (Part C); its existing cases are unchanged. `Coding Details` is a
submission document and is not updated.

---

## A.14 Limits

| Item | Limit |
|---|---|
| character literal | exactly one byte; lexeme at most 4 characters |
| string literal | 0..255 decoded bytes; lexeme column keeps the first 30 bytes |
| array length | 1..131072 elements (8 bytes each, at most 1 MiB) |
| locals of one function (arrays included) | 4 MiB (unchanged) |
| global arrays | each at most 1 MiB, no total limit (unchanged rule for globals) |
| print items per statement | unlimited (`<morePrintItems>` is tail-recursive; the parser stack does not grow) |
| expression depth | 1000 per expression root; an index level counts 1 |
| stack | 6 MiB: statically, `use(f)` for every non-recursive chain and `single(f)` (one activation plus its non-recursive callees' full use) for functions in or above a recursive cycle (A.8.2); at run time, the stack guard at every function entry (A.8.3) |
| recursion depth | only the runtime stack guard |
| exit status | value mod 256 |

## A.15 Rejected features (and the workaround a program uses)

| Feature | Why not |
|---|---|
| remainder `%` | not needed by a compiler that prints numbers with `print`; `a - a / b * b`; `%` is also the comment character |
| unary minus | `0 - x`; changing `<factor>` is not needed |
| string variables / string values | a compiler needs only constant output text; names are stored as byte arrays |
| arrays as parameters or outputs | copy-in/copy-out would copy whole arrays per call; global arrays are shared by all functions |
| arrays in records, arrays of records, multi-dimensional arrays | parallel 1-D arrays suffice; keeps layout and `varText` simple |
| whole-array assignment / comparison / `read` / `write` | loops over elements |
| arithmetic expressions in conditions | LL(1) conflict with parenthesised booleans; assign to a temporary first |
| `break` / `continue` | loop flags |
| writing to stderr | the toy compiler's contract uses stdout plus the exit status (Part B) |
| functions without input parameters | pass a dummy int (`call _next with parameters [b2];`) |
| compile-time bounds checks for constant indices | the runtime check covers them |

## A.16 Message catalogue

Lexical (`[LEXER-ERROR] at line N: ...`):

| Message | Cause |
|---|---|
| `unterminated character literal` | LF, CR or EOF before the closing `'` |
| `empty character literal` | `''` |
| `character literal must contain exactly one character` | e.g. `'ab'` |
| `unknown escape sequence \<X> in character literal` | backslash + byte not in `n t r 0 \ ' "` |
| `byte \x<HH> not allowed in character literal` | non-printable byte other than LF/CR (tab included) after `'` |
| `unterminated string literal` | LF, CR or EOF before the closing `"` |
| `unknown escape sequence \<X> in string literal` | backslash + byte not in `n t r \ " '` (so `\0` too) |
| `byte \x<HH> not allowed in string literal` | non-printable byte other than LF/CR (tab included) in a string |
| `string literal longer than 255 characters` | more than 255 decoded bytes |

Semantic (`[SEMANTIC-ERROR] at line N: ...`; N is the line of the
statement/declaration, as for existing messages; the stack messages use the
function's line):

| Message |
|---|
| `array length must be between 1 and 131072, not <lexeme>` |
| `array element type must be int or real, not <type>` |
| `<name> is not an array (it has type <type>)` |
| `array index must be an int expression, not <type>` |
| `array <name> cannot be used without an index` |
| `array <name> cannot be passed to or returned from a function` |
| `readchar needs an int variable, not <path> of type <type>` |
| `writechar needs an int value, not <type>` |
| `print needs int or real values, not <type>` |
| `exit needs an int value, not <type>` |
| `<f> needs about <n> bytes of stack for a single call; the limit is 6291456` |

Runtime (stdout, then exit status 1):

| Message |
|---|
| `Runtime error: division by zero` (existing) |
| `Runtime error: index <i> out of bounds for array <name> of length <n> at line <L>` |
| `Runtime error: stack overflow (recursion too deep)` |

## A.17 Implementation checklist (by file)

* `lexerDef.h` — six token types (A.1.1); `Token.literal`.
* `lexer.c`, `lexer.h` — keywords; `'` and `"` DFAs with the exact messages;
  string table; `charLiteralValue`; literal-aware `removeComments`; larger
  message buffers.
* `logging.c` — token names and keyword lexemes; `\x20` display in `printToken`.
* `grammar.txt` — A.2.1. `parserDef.h` — `MAX_RULES 128`,
  `MAX_NON_TERMINALS 64`, `TreeNode.literal`.
* `parser.c` — `newTreeNode` sets `literal = -1`; copy `literal` on terminal
  match; `\x20` display in `printNodeRow`.
* `ast.h`, `ast.c` — A.11.1, A.11.2; char literals; `depth` of element accesses.
* `symbolTable.h`, `symbolTable.c` — `TY_ARRAY`, interning, names, free, rows.
* `semantic.c` — array declarations; element references; A.4–A.7 checks;
  removed call-order errors; A.8.2 stack pass; A.9; A.10.
* `codegen.c` — A.4–A.8.3, A.12.
* `README.md` — one paragraph listing the new statements, literals and
  arrays, and `toy/` (Part B).
* `tests/` — Part C.

---

# PART B — The toy language TL and the toy compiler `toycc`

The toy compiler is a program written in the extended language, stored at
`toy/toycc.txt`, built with `./compiler --build toy/toycc.txt toycc`. It reads
a TL program from standard input and writes x86-64 NASM to standard output.
Expected size: 400–900 lines.

All behaviour below was validated with a reference implementation (a Python
model of exactly this contract): every example's expected output was produced
by assembling, linking and running its generated code.

## B.1 TL lexical structure

* Input is a sequence of bytes. Lines are numbered from 1; the line number
  increases after each LF (0x0A). The line of a token is the line of its first
  byte. The end of input is a token `EOF` whose line is 1 + the number of LF
  bytes in the input.
* Whitespace: space (0x20), tab (0x09), CR (0x0D), LF (0x0A). Ignored.
* Comment: `#` up to, not including, the next LF or the end of input. Ignored.
* Integer literal `INT`: `[0-9]+`, decimal, leading zeros allowed. Its value
  must be at most 9223372036854775807, else error `integer literal too large`.
* Name `NAME`: `[A-Za-z_][A-Za-z0-9_]*`, at most 32 bytes, else error
  `identifier too long`. Case-sensitive.
* Keywords (reserved; not names): `while`, `if`, `else`, `print`.
* Operators and punctuation (longest match): `+ - * / ( ) { } ; = == != < <= > >=`.
  An `INT` directly followed by a letter is two tokens (`12ab` = `12` `ab`).
* Any other byte is an error: for a printable byte 0x21..0x7E,
  `unexpected character '<c>'` (the byte itself between single quotes, e.g.
  `unexpected character '$'`; a `!` not followed by `=` is
  `unexpected character '!'`); for any other byte (0x00..0x08, 0x0B, 0x0C,
  0x0E..0x1F, 0x7F..0xFF), `unexpected byte <n>` with n in decimal.

## B.2 TL grammar

```
program  ::= { stmt } EOF
stmt     ::= NAME '=' expr ';'
           | 'print' expr ';'
           | 'while' cond block
           | 'if' cond block [ 'else' block ]
block    ::= '{' { stmt } '}'
cond     ::= expr relop expr
relop    ::= '==' | '!=' | '<' | '<=' | '>' | '>='
expr     ::= term { ( '+' | '-' ) term }
term     ::= unary { ( '*' | '/' ) unary }
unary    ::= '-' unary | primary
primary  ::= INT | NAME | '(' expr ')'
```

Binary operators are left-associative; `*` `/` bind tighter than `+` `-`;
unary minus binds tightest (`-7 / 2` is `(-7) / 2`). A condition is not
parenthesised as a whole (`while (x) < 10 { }` is legal, `while (x < 10)` is
the error `expected ')'`). `else` must be followed by a block (no `else if`
without braces).

## B.3 TL semantics

* Values are 64-bit two's-complement integers. `+`, `-`, `*` and unary minus
  wrap modulo 2^64. `/` truncates toward zero; `MIN / -1` is `MIN`
  (-9223372036854775808); division by zero is a runtime error. Comparisons are
  signed.
* Variables are global to the program and hold one integer each; every
  variable starts at 0 when the compiled program starts (so
  `while 0 > 1 { y = 5; } print y;` prints `0`: `y` is defined, B.3 below, but
  never assigned at run time). A variable
  is *defined* once an assignment statement to it has been parsed completely
  (including its `;`). Using a name in an expression before any such
  assignment precedes it textually is the compile-time error
  `undefined variable <name>`. Hence `x = x + 1;` with no earlier assignment to
  `x` is an error, and a variable assigned inside a loop body may be used after
  the loop.
* `NAME = expr;` evaluates expr and stores it. `print expr;` writes the value in
  decimal followed by LF. `while c b` evaluates c and runs b while c holds.
  `if c b1 else b2` runs b1 if c holds, else b2. Operands are evaluated left to
  right.

## B.4 TL errors

The toy compiler reports exactly one error, the first one met by a
recursive-descent parser with one token of lookahead in which the token after
token k is scanned exactly when the parser consumes token k (the first token
is scanned before parsing starts). So a lexical error in token k+1 wins over a
syntax error that would be detected at token k+1, and nothing after the first
error matters.

| Message | Detected when |
|---|---|
| `unexpected character '<c>'`, `unexpected byte <n>`, `integer literal too large`, `identifier too long` | while scanning a token (B.1) |
| `expected statement` | a statement must start and the current token is none of NAME, `print`, `while`, `if` — and it is not `}` inside a block, nor EOF at top level (so a stray `}` or `else` at top level, or `5;`) |
| `expected '='` | after the NAME that starts a statement |
| `expected ';'` | after the expression of an assignment or `print` |
| `expected expression` | a `unary` must start and the token is none of `-`, INT, NAME, `(` |
| `expected ')'` | after the expression inside `( ... )` |
| `expected '{'` | a block must start (after a condition, after `else`) |
| `expected '}'` | EOF inside a block |
| `expected comparison operator` | after the first expression of a condition |
| `undefined variable <name>` | a NAME in an expression (a `primary`) that is not yet defined (B.3); detected while that NAME is the current token, before it is consumed — so before the token after it is scanned |

The error's line is the line of the current token when the error is detected
(the token at which it is detected; for scanning errors, the line where the
offending token or byte starts; at EOF, the EOF line of B.1). For
`undefined variable` it is the NAME's line: with the input
`y = 1;` LF `print x` LF `$` LF the error is `error: 2: undefined variable x`,
not the lexical error at the `$` on line 3, which is never scanned.

## B.5 The `toycc` contract

Invocation:

```sh
./toycc < prog.tl > prog.asm
nasm -f elf64 prog.asm -o prog.o && gcc -no-pie prog.o -o prog && ./prog
```

* `toycc` reads standard input with `readchar`; on success it has read up to
  the end of input. It stops at the first error.
* Success: exit status 0; stdout is a complete NASM program for x86-64 Linux
  that `nasm -f elf64` (NASM 2.x) accepts and `gcc -no-pie` links against the
  C library (it may use `printf` and `exit`).
* Error: exit status 1; the **last line** of stdout is exactly
  `error: <line>: <message>` (B.4), it starts at the beginning of a line (if
  anything was written before it, that output ends with LF), and it ends with
  LF, which is the last byte of stdout. What precedes it on stdout is
  otherwise unspecified (a one-pass compiler may already have written part of
  the assembly; writing every assembly line with a single `print` whose last
  item ends in `\n` satisfies the rule). Nothing is required on stderr. `toycc` must not end in an
  extended-language runtime error (index out of bounds, stack overflow) for
  inputs within the limits below.
* Limits that `toycc` must support: any input length; at most 500 distinct
  variable names; at most 8000 bytes of variable names in total (keywords are
  not variable names); nesting depth at most 200, where the nesting depth of a
  token is the number of enclosing parentheses, unary minus operators and
  blocks. Inputs beyond these limits are outside the contract (no acceptance
  test exceeds them; `limits_names.tl` and `limits_nesting.tl` in C.6 reach
  them exactly).

The generated program:

* each `print` writes the value as `printf("%lld\n")` does (e.g. `-3`), on
  stdout;
* exits with status 0 after the last statement;
* on division by zero writes `runtime error: division by zero` and LF to
  stdout, after all earlier output, and exits with status 1;
* reads no input.

Suggested shape of the generated assembly (non-normative; the reference
implementation emits essentially this, without the comments — values in
`rax`, temporaries pushed, one `.bss` quadword `V<k>` per variable, labels
`L<k>`):

```nasm
default rel
extern printf, exit
global main
section .data
fmt: db "%lld", 10, 0
msg: db "runtime error: division by zero", 10, 0
section .text
main:
    push rbp
    mov rbp, rsp
    ; x = 1 + 2;
    mov rax, 1
    push rax
    mov rax, 2
    mov rcx, rax
    pop rax
    add rax, rcx
    mov [rel V0], rax
    ; while x < 10 { ... }        (relop -> inverse jump: == jne, != je,
L0:                               ;  < jge, <= jg, > jle, >= jl)
    mov rax, [rel V0]
    push rax
    mov rax, 10
    mov rcx, rax
    pop rax
    cmp rax, rcx
    jge L1
    ; ... body ...
    jmp L0
L1:
    ; print x;
    mov rax, [rel V0]
    mov rsi, rax
    lea rdi, [rel fmt]
    xor eax, eax
    call printf wrt ..plt
    xor eax, eax
    pop rbp
    ret
tl_div:                           ; rax / rcx -> rax, called as "call tl_div"
    test rcx, rcx
    jz tl_divzero
    cmp rcx, -1
    je tl_divneg
    cqo
    idiv rcx
    ret
tl_divneg:
    neg rax                       ; also maps MIN to MIN, avoiding the idiv trap
    ret
tl_divzero:
    and rsp, -16
    lea rdi, [rel msg]
    xor eax, eax
    call printf wrt ..plt
    mov edi, 1
    call exit wrt ..plt
section .bss
V0: resq 1
section .note.GNU-stack noalloc noexec nowrite progbits
```

## B.6 Example programs (acceptance tests)

Each file is stored as `tests/toy/<name>.tl`. Expectations are written as TL
comments at the top of the file: `#? stdout: <line>` (expected output lines of
the compiled program, in order), `#? exit: <n>` (its exit status, default 0),
`#? error: <line>: <message>` (expected toycc error). The annotation lines are
part of the files, so line numbers below count them. A file shown as a text
block consists of exactly the lines shown, each terminated by one LF (so the
file ends with exactly one LF and has no trailing blank line); line numbers in
error messages depend on this (e.g. the EOF line of `err_unclosed_block.tl`
is 5). Files that do not fit this form are given as Python code that writes
their bytes.

**`tests/toy/print_literals.tl`**

```text
#? stdout: 42
#? stdout: 0
#? stdout: 7
#? stdout: 9223372036854775807
# integer literals and print
print 42;
print 0;
print 007;
print 9223372036854775807;
```

**`tests/toy/precedence.tl`**

```text
#? stdout: 7
#? stdout: 9
#? stdout: 3
#? stdout: 3
#? stdout: 23
#? stdout: 3
#? stdout: -3
#? stdout: -3
#? stdout: 3
#? stdout: 4
#? stdout: 5
#? stdout: 4
#? stdout: 1
# precedence, left associativity, parentheses, unary minus,
# division truncating toward zero
print 1 + 2 * 3;
print (1 + 2) * 3;
print 10 - 4 - 3;
print 100 / 10 / 3;
print 2 * 3 + 4 * 5 - 6 / 2;
print 7 / 2;
print -7 / 2;
print 7 / -2;
print -7 / -2;
print -(3 - 5) * 2;
print 2 - -3;
print --4;
print ((((1))));
```

**`tests/toy/variables.tl`**

```text
#? stdout: 20
#? stdout: 25
#? stdout: 46
#? stdout: 42
#? stdout: -1
# assignment, reassignment, identifiers up to 32 characters
x = 5;
y = x * x;
x = y - x;
print x;
print y;
counter_2 = x + y + 1;
print counter_2;
ThisIsAVeryLongIdentifierName_32 = 7;
print ThisIsAVeryLongIdentifierName_32 * 6;
_a = 1; _A = 2;
print _a - _A;
```

**`tests/toy/sum_loop.tl`**

```text
#? stdout: 5050
#? stdout: 101
# sum of 1..100 with a while loop
i = 1;
sum = 0;
while i <= 100 {
    sum = sum + i;
    i = i + 1;
}
print sum;
print i;
```

**`tests/toy/relops.tl`**

```text
#? stdout: 1
#? stdout: 0
#? stdout: 1
#? stdout: 0
#? stdout: 1
#? stdout: 0
#? stdout: 1
#? stdout: 0
#? stdout: 1
#? stdout: 0
#? stdout: 1
#? stdout: 0
#? stdout: 7
#? stdout: 8
#? stdout: 10
# every comparison operator, true and false; if with and without else
a = 3; b = 5;
if a == 3 { print 1; } else { print 0; }
if a == b { print 1; } else { print 0; }
if a != b { print 1; } else { print 0; }
if a != 3 { print 1; } else { print 0; }
if a < b { print 1; } else { print 0; }
if b < a { print 1; } else { print 0; }
if a <= 3 { print 1; } else { print 0; }
if b <= a { print 1; } else { print 0; }
if b > a { print 1; } else { print 0; }
if a > a { print 1; } else { print 0; }
if a >= 3 { print 1; } else { print 0; }
if a >= b { print 1; } else { print 0; }
if a - 10 < 0 { print 7; }
if a * 2 == b + 1 { print 8; }
if 0 - 1 < -1 { print 9; }
print 10;
```

**`tests/toy/factorial.tl`**

```text
#? stdout: 120
#? stdout: 3628800
#? stdout: 2432902008176640000
# 20! is the largest factorial that fits in 64 bits
n = 1;
f = 1;
while n <= 20 {
    f = f * n;
    if n == 5 { print f; }
    if n == 10 { print f; }
    n = n + 1;
}
print f;
```

**`tests/toy/fibonacci.tl`**

```text
#? stdout: 0
#? stdout: 1
#? stdout: 1
#? stdout: 2
#? stdout: 3
#? stdout: 5
#? stdout: 8
#? stdout: 13
#? stdout: 21
#? stdout: 34
#? stdout: 2880067194370816120
# the first ten Fibonacci numbers, then F(90)
a = 0;
b = 1;
k = 0;
while k < 10 {
    print a;
    t = a + b;
    a = b;
    b = t;
    k = k + 1;
}
while k < 90 {
    t = a + b;
    a = b;
    b = t;
    k = k + 1;
}
print a;
```

**`tests/toy/primes.tl`**

```text
#? stdout: 2
#? stdout: 3
#? stdout: 5
#? stdout: 7
#? stdout: 11
#? stdout: 13
#? stdout: 17
#? stdout: 19
#? stdout: 23
#? stdout: 29
#? stdout: 31
#? stdout: 37
#? stdout: 41
#? stdout: 43
#? stdout: 47
# primes below 50 by trial division (n - n / d * d is n mod d)
n = 2;
while n < 50 {
    d = 2;
    prime = 1;
    while d * d <= n {
        if n - n / d * d == 0 { prime = 0; }
        d = d + 1;
    }
    if prime == 1 { print n; }
    n = n + 1;
}
```

**`tests/toy/gcd_collatz.tl`**

```text
#? stdout: 21
#? stdout: 111
#? stdout: 9232
# Euclid's algorithm, then the Collatz sequence of 27 (steps, peak)
a = 1071; b = 462;
while b != 0 {
    t = a - a / b * b;
    a = b;
    b = t;
}
print a;
n = 27; steps = 0; peak = n;
while n != 1 {
    if n - n / 2 * 2 == 0 { n = n / 2; } else { n = 3 * n + 1; }
    if n > peak { peak = n; }
    steps = steps + 1;
}
print steps;
print peak;
```

**`tests/toy/blocks.tl`**

```text
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
```

**`tests/toy/wraparound.tl`**

```text
#? stdout: -9223372036854775808
#? stdout: -9223372036854775808
#? stdout: 9223372036854775807
#? stdout: -9223372036854775808
#? stdout: -9223372036854775808
#? stdout: -9223372036854775808
#? stdout: -2
# 64-bit two's complement arithmetic wraps; MIN / -1 is MIN
max = 9223372036854775807;
min = -max - 1;
print min;
print max + 1;
print min - 1;
print min / -1;
print min * -1;
print -min;
print max * 2;
```

**`tests/toy/layout.tl`**

This file has CRLF line ends and no final newline, so it is given as the Python code that creates it:

```python
open("tests/toy/layout.tl", "wb").write(
    b'#? stdout: 2\r\n'
    b'#? stdout: 6\r\n'
    b'# comments, tabs and CRLF line endings\r\n'
    b'x\t=\t1;   # trailing comment\r\n'
    b'\r\n'
    b'#print 99;\r\n'
    b'print x+1;# no space\r\n'
    b'print(x)*(x+1)*3;\r\n'
    b'# comment at end of file without newline')
```


Runtime error of the compiled program:

**`tests/toy/div_by_zero.tl`**

```text
#? stdout: 3
#? stdout: runtime error: division by zero
#? exit: 1
# division by zero stops the compiled program
x = 10;
print x / 3;
y = x - 10;
print x / y;
print 5;
```


## B.7 Erroneous programs

For each, `toycc` exits with status 1 and the last stdout line is the
`#? error:` text prefixed by `error: `.

**`tests/toy/err_bad_char.tl`**

```text
#? error: 2: unexpected character '$'
x = 3 $ 4;
print x;
```

**`tests/toy/err_missing_semicolon.tl`**

```text
#? error: 4: expected ';'
x = 1;
print x
print 2;
```

**`tests/toy/err_undefined.tl`**

```text
#? error: 3: undefined variable x
y = 2;
print x + y;
```

**`tests/toy/err_unclosed_block.tl`**

```text
#? error: 5: expected '}'
i = 0;
while i < 3 {
    i = i + 1;
```

**`tests/toy/err_too_large.tl`**

```text
#? error: 3: integer literal too large
x = 1;
print 9223372036854775808;
```

**`tests/toy/err_no_relop.tl`**

```text
#? error: 3: expected comparison operator
x = 1;
if x {
    print x;
}
```


More error cases are listed in C.6.

## B.8 Sufficiency check: excerpts of `toycc` in the extended language

These excerpts use only the extensions of Part A (marked in comments) plus the
existing language, and follow its rules: identifiers `[b-d][2-7][b-d]*[2-7]*`,
globals named `d…` so that no local clashes with them, a dummy int input on
every function, declarations before statements, while bodies that update the
condition (here through calls, A.9). They show byte input with EOF, the token
buffer and name table in global arrays, recursive descent with self- and
forward recursion, and assembly output.

Globals (declared in `_main`): `d2` lookahead byte (-1 at end of input); `d3`
current line; `d4` current token kind (`'0'` number, `'a'` name, `'$'` end of
input, otherwise the operator byte or a code for a keyword or two-byte
operator); `d5` number value; `d6` line of the current token; `d2b` index of
a name token in the name table; `d4b` int[504], 1 once name k has been
assigned; `d3b` int[8016] bytes of all names; `d3c`, `d3d` int[504] start and
length of name k. The table also holds the four keywords as names 0..3
(16 bytes), hence 500 + 4 entries and 8000 + 16 bytes for the limits of B.5. `_fail` has one `if` per message number. `_term` parses
`unary { ('*' | '/') unary }` and leaves the value in `rax`.

```
_advance input parameter list [int b2];
	if (d2 == '\n') then                        % character literal (A.3)
		d3 <--- d3 + 1;
	endif
	readchar(d2);                               % byte input, -1 at EOF (A.4)
	return;
end

_term input parameter list [int b2];
	type int : b3;
	call _unary with parameters [b2];
	while ((d4 == '*') @@@ (d4 == '/'))         % d4 is updated by the calls (A.9)
		b3 <--- d4;
		call _next with parameters [b2];
		print("    push rax\n");                % output without newline (A.5)
		call _unary with parameters [b2];
		print("    mov rcx, rax\n    pop rax\n");
		if (b3 == '*') then
			print("    imul rax, rcx\n");
		else
			print("    call tl_div\n");
		endif
	endwhile
	return;
end

_unary input parameter list [int b2];
	if (d4 == '-') then
		call _next with parameters [b2];
		call _unary with parameters [b2];       % self-recursion (A.8)
		print("    neg rax\n");
	else
		call _primary with parameters [b2];     % defined later (A.8)
	endif
	return;
end

_primary input parameter list [int b2];
	type int : b3;
	if (d4 == '0') then
		print("    mov rax, ", d5, "\n");       % int item (A.5)
		call _next with parameters [b2];
	else
		if (d4 == 'a') then
			if (d4b[d2b] == 0) then             % array element in a condition (A.7)
				b3 <--- 12;
				call _fail with parameters [b3];
			endif
			print("    mov rax, [rel V", d2b, "]\n");
			call _next with parameters [b2];
		else
			if (d4 == '(') then
				call _next with parameters [b2];
				call _expr with parameters [b2];   % mutual recursion (A.8)
				b3 <--- ')';
				call _expect with parameters [b3];
			else
				b3 <--- 7;
				call _fail with parameters [b3];
			endif
		endif
	endif
	return;
end

_fail input parameter list [int b2];
	type int : b3;
	type int : b4;
	print("error: ", d6, ": ");
	if (b2 == 12) then
		print("undefined variable ");
		b3 <--- d3c[d2b];
		b4 <--- b3 + d3d[d2b];
		while (b3 < b4)
			writechar(d3b[b3]);                 % single byte (A.5)
			b3 <--- b3 + 1;
		endwhile
	endif
	print("\n");
	exit(1);                                    % exit status (A.6)
	return;
end

_main
	type int : d2 : global;
	type int[504] : d4b : global;               % global arrays (A.7)
	type int[8016] : d3b : global;
	% ... d3, d4, d5, d6, d2b, d3c, d3d as described above ...
	type int : b2;
	d3 <--- 1;
	call _keywords with parameters [b2];
	call _advance with parameters [b2];
	call _next with parameters [b2];
	print("default rel\nextern printf, exit\nglobal main\nsection .text\nmain:\n    push rbp\n    mov rbp, rsp\n");
	while (d4 != '$')
		call _stmt with parameters [b2];
	endwhile
	call _finish with parameters [b2];
	return;
end
```

Suggested function list for the complete compiler (non-normative):
`_advance`; `_keywords` (interns `while`, `if`, `else`, `print` as names 0..3);
`_intern` (output: index of the name in `d2c[0..d2d)`, adding it if new);
`_next` (skips blanks and comments; numbers with the overflow test
`b4 <--- (9223372036854775807 - (d2 - '0')) / 10; if (d5 > b4) then ...`;
names through `_intern`, mapping indices 0..3 to keyword kinds; one- and
two-byte operators); `_fail`; `_expect`; `_primary`; `_unary`; `_term`;
`_expr`; `_cond` (input: the label to jump to when false); `_block`; `_stmt`;
`_finish` (emits the epilogue `xor eax, eax` / `pop rbp` / `ret`, then
`tl_div`, `.data`, the `.bss` slots `V0..`); `_main` (prints the header and
the prologue of `main`, as above).

---

# PART C — Test plan

All tests live in the existing layout and are run by
`python3 tests/run_tests.py` (also with `--asan`). Every file below is given
in full; place it at the path in its heading. Expected outputs of the
extension programs were derived by hand from Part A; expected outputs of the
TL tests were produced by running a reference implementation; the syntax
tests that use only existing tokens were checked with a prototype parser built
from the new grammar.

Annotation rules of the existing runner that matter here: `%? stdout:` values
are stripped, so expected output lines never start or end with spaces; a
`%?` inside a string literal would be taken as an annotation, so no test
string contains `%?`; each `%? stdin:` line is fed followed by LF.

## C.1 Lexer (`LEXER_CASES` in `tests/run_tests.py`)

Append these cases (Python source, to be pasted into the list):

```python
    ("new keywords", "readchar writechar print exit",
     ["TK_READCHAR readchar", "TK_WRITECHAR writechar", "TK_PRINT print",
      "TK_EXIT exit"], []),
    ("new keyword look-alikes are field ids", "prints exits readchars printx",
     ["TK_FIELDID prints", "TK_FIELDID exits", "TK_FIELDID readchars",
      "TK_FIELDID printx"], []),
    ("new keywords are case sensitive", "Print",
     ["TK_FIELDID rint"], [(1, "P not recognized")]),
    ("character literals",
     r"""'a' '0' '~' '"' '\n' '\t' '\r' '\0' '\\' '\'' '\"'""",
     ["TK_CHARLIT 'a'", "TK_CHARLIT '0'", "TK_CHARLIT '~'",
      "TK_CHARLIT '\"'", r"TK_CHARLIT '\n'", r"TK_CHARLIT '\t'",
      r"TK_CHARLIT '\r'", r"TK_CHARLIT '\0'", r"TK_CHARLIT '\\'",
      r"TK_CHARLIT '\''", r"TK_CHARLIT '\"'"], []),
    ("space character literal is displayed as \\x20", "' ' b2",
     [r"TK_CHARLIT '\x20'", "TK_ID b2"], []),
    ("character literal in an expression", "b2<---'a'+1;",
     ["TK_ID b2", "TK_ASSIGNOP", "TK_CHARLIT 'a'", "TK_PLUS", "TK_NUM 1",
      "TK_SEM"], []),
    ("empty character literal", "'' b2", ["TK_ID b2"],
     [(1, "empty character literal")]),
    ("character literal with two characters", "'ab' b2", ["TK_ID b2"],
     [(1, "character literal must contain exactly one character")]),
    ("three quotes", "''' b2", [],
     [(1, "empty character literal"),
      (1, "character literal must contain exactly one character")]),
    ("unterminated character literal", "'a\nb2", ["TK_ID b2 @2"],
     [(1, "unterminated character literal")]),
    ("character literal at end of file", "b2 '", ["TK_ID b2"],
     [(1, "unterminated character literal")]),
    ("unknown escape in a character literal", r"'\q' b2", ["TK_ID b2"],
     [(1, r"unknown escape sequence \q in character literal")]),
    ("control byte in a character literal", "'\x01' b2", ["TK_ID b2"],
     [(1, r"byte \x01 not allowed in character literal")]),
    ("raw tab in a character literal", "'\t' b2", ["TK_ID b2"],
     [(1, r"byte \x09 not allowed in character literal")]),
    ("string literal", '"hello" b2', ['TK_STR "hello"', "TK_ID b2"], []),
    ("empty string literal", '""', ['TK_STR ""'], []),
    ("string literal with escapes", r'"a\n\t\r\\\"\'b"',
     [r'TK_STR "a\n\t\r\\\"\'b"'], []),
    ("spaces in a string are displayed as \\x20", '"a b  c"',
     [r'TK_STR "a\x20b\x20\x20c"'], []),
    ("percent inside a string is not a comment", '"50% done" b2',
     [r'TK_STR "50%\x20done"', "TK_ID b2"], []),
    ("string lexeme keeps 30 bytes", '"' + "a" * 40 + '"',
     ['TK_STR "' + "a" * 29], []),
    ("string of 255 characters", '"' + "x" * 255 + '" b2',
     ['TK_STR "' + "x" * 29, "TK_ID b2"], []),
    ("an escape counts as one character", '"' + "\\n" * 255 + '"',
     ['TK_STR "' + "\\n" * 14 + "\\"], []),
    ("string of 256 characters", '"' + "x" * 256 + '" b2', ["TK_ID b2"],
     [(1, "string literal longer than 255 characters")]),
    ("unterminated string literal", '"abc\nb2', ["TK_ID b2 @2"],
     [(1, "unterminated string literal")]),
    ("string literal at end of file", 'b2 "abc', ["TK_ID b2"],
     [(1, "unterminated string literal")]),
    ("unknown escape in a string", r'"a\qb" b2', ["TK_ID b2"],
     [(1, r"unknown escape sequence \q in string literal")]),
    ("no NUL escape in strings", r'"\0" b2', ["TK_ID b2"],
     [(1, r"unknown escape sequence \0 in string literal")]),
    ("two bad escapes give two errors", r'"\q\w" b2', ["TK_ID b2"],
     [(1, r"unknown escape sequence \q in string literal"),
      (1, r"unknown escape sequence \w in string literal")]),
    ("raw tab in a string", '"a\tb" b2', ["TK_ID b2"],
     [(1, r"byte \x09 not allowed in string literal")]),
    ("non-ASCII bytes in a string", '"é" b2', ["TK_ID b2"],
     [(1, r"byte \xC3 not allowed in string literal"),
      (1, r"byte \xA9 not allowed in string literal")]),
    ("line numbers after a bad string", '"abc\n"def" b2',
     ['TK_STR "def" @2', "TK_ID b2 @2"],
     [(1, "unterminated string literal")]),
    ("backslash at end of file in a character literal", "b2 '\\",
     ["TK_ID b2"], [(1, "unterminated character literal")]),
    ("backslash before LF in a character literal", "'\\\nb2",
     ["TK_ID b2 @2"], [(1, "unterminated character literal")]),
    ("CR ends an unterminated character literal", "'a\r\nb2",
     ["TK_ID b2 @2"], [(1, "unterminated character literal")]),
    ("CR ends an unterminated string literal", '"abc\r\nb2',
     ["TK_ID b2 @2"], [(1, "unterminated string literal")]),
    ("backslash before CR in a string literal", '"a\\\r\nb2',
     ["TK_ID b2 @2"], [(1, "unterminated string literal")]),
    ("literals on CRLF lines", "b2 <--- 'a';\r\nprint(\"x\");\r\n",
     ["TK_ID b2 @1", "TK_ASSIGNOP", "TK_CHARLIT 'a'", "TK_SEM",
      "TK_PRINT @2", "TK_OP", 'TK_STR "x"', "TK_CL", "TK_SEM"], []),
    ("apostrophe in a comment", "b2 % it's\nc3",
     ["TK_ID b2 @1", "TK_COMMENT @1", "TK_ID c3 @2"], []),
    ("array declaration", "type int[10] : b2;",
     ["TK_TYPE", "TK_INT", "TK_SQL", "TK_NUM 10", "TK_SQR", "TK_COLON",
      "TK_ID b2", "TK_SEM"], []),
    ("element access", "b2[b3+1]",
     ["TK_ID b2", "TK_SQL", "TK_ID b3", "TK_PLUS", "TK_NUM 1", "TK_SQR"], []),
```

The existing cases keep passing unchanged (the "all keywords" case lists only
the old keywords; `$ ? ^ |` are still unrecognised).

## C.2 Syntax tests (`tests/syntax/`)

Each file must give exactly the annotated syntax-error lines (and lexical
errors) under `--check`, and `--parse` must exit with 1 without crashing.

**`tests/syntax/array_parameter.txt`**

```text
% Arrays cannot be parameters: the parameter list takes a plain type.
_f input parameter list [int[5] b2]; %? syntax-error
	write(1);
	return;
end
_main
	return;
end
```

**`tests/syntax/array_in_record.txt`**

```text
% Arrays cannot be record fields.
_main
	record #r
		type int[3] : x; %? syntax-error
		type int : y;
	endrecord
	return;
end
```

**`tests/syntax/index_after_field.txt`**

```text
% An index cannot follow a field access.
_main
	type int : b2;
	b2.x[1] <--- 2; %? syntax-error
	return;
end
```

**`tests/syntax/array_length_not_literal.txt`**

```text
% The length of an array is an integer literal.
_main
	type int : b3;
	type int[b3] : b2; %? syntax-error
	return;
end
```

**`tests/syntax/print_without_items.txt`**

```text
% print needs at least one item.
_main
	print(); %? syntax-error
	return;
end
```

**`tests/syntax/readchar_constant.txt`**

```text
% readchar needs a variable.
_main
	readchar('a'); %? syntax-error
	return;
end
```

**`tests/syntax/string_as_value.txt`**

```text
% A string literal is only allowed as an item of print.
_main
	type int : b2;
	b2 <--- "abc"; %? syntax-error
	return;
end
```

**`tests/syntax/write_string.txt`**

```text
% write takes a variable or a number, not a string; use print.
_main
	write("abc"); %? syntax-error
	return;
end
```

**`tests/syntax/exit_without_parentheses.txt`**

```text
% exit takes its status in parentheses.
_main
	exit 1; %? syntax-error
	return;
end
```

**`tests/syntax/keyword_as_variable_name.txt`**

```text
% The new keywords cannot be used where a variable name is expected.
_main
	type int : print; %? syntax-error
	return;
end
```

**`tests/syntax/literal_errors.txt`**

```text
% Lexical errors in character and string literals.
_main
	type int : b2;
	b2 <--- ''; %? syntax-error %? lex-error: empty character literal
	b2 <--- 'ab'; %? syntax-error %? lex-error: character literal must contain exactly one character
	print("a\qb"); %? syntax-error %? lex-error: unknown escape sequence \q in string literal
	return;
end
```


## C.3 Semantic tests (`tests/semantic/`)

Each file is run with `--check`; every `%? error:` text must be reported on
its line and nothing else (or `%? ok` and exit status 0). Every file here is
also parse-tree checked automatically.

`tests/semantic/call_order.txt` — **replaces** the current file (A.13):

**`tests/semantic/call_order.txt`**

```text
% A function may call any function, including itself and functions defined
% later in the file; function names are unique.
_first input parameter list [int b2]
output parameter list [int b3];
	[b3] <--- call _second with parameters [b2];
	return [b3];
end

_second input parameter list [int b2]
output parameter list [int b3];
	[b3] <--- call _second with parameters [b2];
	return [b3];
end

_first input parameter list [int b4] %? error: function _first is already defined at line 3
output parameter list [int b5];
	b5 <--- b4;
	return [b5];
end

_main
	type int : b2;
	type int : b3;
	[b3] <--- call _first with parameters [b2];
	[b3] <--- call _second with parameters [b2];
	call _nowhere with parameters [b2]; %? error: function _nowhere is not defined
	return;
end
```

**`tests/semantic/arrays.txt`**

```text
% Arrays: declarations, indexing, and the places a whole array may not appear.
_f input parameter list [int b2]
output parameter list [int b3];
	b3 <--- b2;
	return [b3];
end

_g input parameter list [int b2]
output parameter list [int b3];
	type int[4] : c3;
	b3 <--- 1;
	return [c3]; %? error: array c3 cannot be passed to or returned from a function
end

_main
	record #pt
		type int : x;
		type int : y;
	endrecord
	type int[10] : b2;
	type real[3] : c2;
	type int[0] : b4; %? error: array length must be between 1 and 131072, not 0
	type int[131073] : b5; %? error: array length must be between 1 and 131072, not 131073
	type #pt[2] : b6; %? error: array element type must be int or real, not #pt
	type int[131072] : d2 : global;
	type int : b3;
	type real : c3;
	type #pt : d3;
	b2[0] <--- 1;
	b2[b3 + 1] <--- b2[b3] * 2;
	c2[2] <--- b2[1];
	d2[131071] <--- b2['a' - 'a'] * 0;
	b2[1] <--- c2[0]; %? error: type mismatch: cannot assign a value of type real to b2[...] of type int
	b2[c3] <--- 1; %? error: array index must be an int expression, not real
	b2[d3] <--- 1; %? error: array index must be an int expression, not #pt
	b3[0] <--- 1; %? error: b3 is not an array (it has type int)
	d3[1] <--- 1; %? error: d3 is not an array (it has type #pt)
	b3 <--- b2; %? error: array b2 cannot be used without an index
	b2 <--- 0; %? error: array b2 cannot be used without an index
	b3 <--- b2.x; %? error: b2 has type int[10] and has no field x
	if (b2 == 0) then %? error: array b2 cannot be used without an index
		write(b2); %? error: array b2 cannot be used without an index
	endif
	read(b2); %? error: array b2 cannot be used without an index
	readchar(b2); %? error: array b2 cannot be used without an index
	print(b2); %? error: array b2 cannot be used without an index
	[b3] <--- call _f with parameters [b2]; %? error: array b2 cannot be passed to or returned from a function
	[b2] <--- call _f with parameters [b3]; %? error: array b2 cannot be passed to or returned from a function
	b2[b7] <--- 1; %? error: variable b7 is not declared
	if (b2[b3] < c2[1]) then
		read(b2[b3]);
		write(c2[b2[0]]);
	endif
	return;
end
```

**`tests/semantic/io_extensions.txt`**

```text
% Operands of readchar, writechar, print and exit.
_main
	record #pt
		type int : x;
		type real : y;
	endrecord
	type int : b2;
	type real : c2;
	type #pt : d2;
	type int[3] : b3;
	readchar(b2);
	readchar(d2.x);
	readchar(b3[1]);
	readchar(c2); %? error: readchar needs an int variable, not c2 of type real
	readchar(d2.y); %? error: readchar needs an int variable, not d2.y of type real
	readchar(d2); %? error: readchar needs an int variable, not d2 of type #pt
	writechar(b2 + 'a');
	writechar(c2); %? error: writechar needs an int value, not real
	writechar(d2); %? error: writechar needs an int value, not #pt
	print("x", b2, c2, d2.y, 'a', b3[0] * 2);
	print(d2); %? error: print needs int or real values, not #pt
	print("a", d2 + d2); %? error: print needs int or real values, not #pt
	exit(b2 * 2);
	exit(2.50); %? error: exit needs an int value, not real
	exit(d2); %? error: exit needs an int value, not #pt
	readchar(b4); %? error: variable b4 is not declared
	return;
end
```

**`tests/semantic/while_rule_extensions.txt`**

```text
% The while rule with array elements, readchar and calls.
_touch input parameter list [int b2];
	b5 <--- b5 + 1;
	return;
end

_same input parameter list [int b2]
output parameter list [int b3];
	b3 <--- b2;
	return [b3];
end

_main
	type int : b5 : global;
	type int[5] : c2;
	type int : b2;
	type int : b3;
	while (c2[b2] == 0)
		b2 <--- b2 + 1;
	endwhile
	while (c2[b2] == 0)
		c2[3] <--- 1;
	endwhile
	while (c2[0] == 0) %? error: none of the variables in the while condition is updated inside the loop
		b3 <--- 1;
	endwhile
	while (b3 != 'x')
		readchar(b3);
	endwhile
	while ('a' < 'b') %? error: none of the variables in the while condition is updated
		b3 <--- 1;
	endwhile
	while (b5 < 10)
		call _touch with parameters [b2];
	endwhile
	while (b3 < 10) %? error: none of the variables in the while condition is updated
		call _touch with parameters [b2];
	endwhile
	while (c2[b3] < 10)
		[b3] <--- call _same with parameters [b2];
	endwhile
	while (c2 == 0) %? error: array c2 cannot be used without an index
		b3 <--- 1;
	endwhile
	while (b3[0] < 1) %? error: b3 is not an array (it has type int)
		b2 <--- 1;
	endwhile
	return;
end
```

**`tests/semantic/stack_recursive.txt`**

```text
% One activation of a recursive function must fit in the 6 MiB stack limit:
% 4 MiB of local arrays plus a 3 MiB parameter block for the recursive call.
_big input parameter list [#rq c2, #rq c3, #rq c4] %? error: _big needs about 7340064 bytes of stack for a single call; the limit is 6291456
output parameter list [int b2];
	type int[131072] : d2;
	type int[131072] : d3;
	type int[131072] : d4;
	type int[131072] : d5;
	[b2] <--- call _big with parameters [c2, c3, c4];
	return [b2];
end

_main
	record #ra
		type int : x;
		type int : y;
	endrecord
	record #rb
		type #ra : p;
		type #ra : q;
	endrecord
	record #rc
		type #rb : p;
		type #rb : q;
	endrecord
	record #rd
		type #rc : p;
		type #rc : q;
	endrecord
	record #re
		type #rd : p;
		type #rd : q;
	endrecord
	record #rf
		type #re : p;
		type #re : q;
	endrecord
	record #rg
		type #rf : p;
		type #rf : q;
	endrecord
	record #rh
		type #rg : p;
		type #rg : q;
	endrecord
	record #ri
		type #rh : p;
		type #rh : q;
	endrecord
	record #rj
		type #ri : p;
		type #ri : q;
	endrecord
	record #rk
		type #rj : p;
		type #rj : q;
	endrecord
	record #rl
		type #rk : p;
		type #rk : q;
	endrecord
	record #rm
		type #rl : p;
		type #rl : q;
	endrecord
	record #rn
		type #rm : p;
		type #rm : q;
	endrecord
	record #ro
		type #rn : p;
		type #rn : q;
	endrecord
	record #rp
		type #ro : p;
		type #ro : q;
	endrecord
	record #rq
		type #rp : p;
		type #rp : q;
	endrecord
	type int : b2;
	write(b2);
	return;
end
```

**`tests/semantic/extensions_ok.txt`**

```text
%? ok
% Every extension used correctly.
_count input parameter list [int b2]
output parameter list [int b3];
	type int : b4;
	if (b2 <= 0) then
		b3 <--- 0;
	else
		b4 <--- b2 - 1;
		[b3] <--- call _count with parameters [b4];
		b3 <--- b3 + 1;
	endif
	return [b3];
end

_ping input parameter list [int b2]
output parameter list [int b3];
	type int : b4;
	b4 <--- b2 - 1;
	b3 <--- 0;
	if (b2 > 0) then
		[b3] <--- call _pong with parameters [b4];
	endif
	return [b3];
end

_pong input parameter list [int b2]
output parameter list [int b3];
	[b3] <--- call _ping with parameters [b2];
	return [b3];
end

_main
	record #rec
		type int : print;
		type int : exit;
		type int : readchar;
		type int : writechar;
	endrecord
	type #rec : d2;
	type int[16] : c2 : global;
	type real[4] : c3;
	type int : b2;
	type int : b3;
	readchar(b2);
	while (b2 != '\n')
		c2[b3] <--- b2;
		b3 <--- b3 + 1;
		readchar(b2);
	endwhile
	d2.print <--- c2[0] + 'a';
	d2.exit <--- d2.print * 2;
	readchar(d2.readchar);
	c3[b3 - 1] <--- c2[b3 - 1] / 2.00;
	if ((c3[0] > 1.50) &&& (c2[c2[0]] == '\t')) then
		writechar(c2[0]);
	endif
	print("count: ", b3, ", first: ", c2[0], ", half: ", c3[0], "\n");
	[b3] <--- call _count with parameters [b2];
	[b3] <--- call _ping with parameters [b3];
	write(c2[b3]);
	if (b3 < 0) then
		exit(2);
	endif
	return;
end
```

**`tests/semantic/array_declarations.txt`**

```text
% Array declaration errors, for locals and globals; uses of a variable whose
% declaration failed are silent, and so is an expression that indexes a
% non-array.
_main
	record #pt
		type int : x;
		type int : y;
	endrecord
	union #u
		type int : i;
		type real : r;
	endunion
	definetype record #pt as #ali
	type #pt[0] : b2; %? error: array length must be between 1 and 131072, not 0 %? error: array element type must be int or real, not #pt
	type #ali[2] : b3; %? error: array element type must be int or real, not #pt
	type #u[2] : b4; %? error: array element type must be int or real, not #u
	type #zz[3] : b5; %? error: undefined type #zz
	type int[99999999999999999999] : b6; %? error: array length must be between 1 and 131072, not 99999999999999999999
	type real[007] : c2;
	type int[0] : d4 : global; %? error: array length must be between 1 and 131072, not 0
	type #pt[2] : d5 : global; %? error: array element type must be int or real, not #pt
	type int : b7;
	b2[1] <--- b3[0] + b4[0];
	b5[0] <--- b6[1];
	d4[0] <--- d5[0] + 1;
	c2[6] <--- 1.50;
	b7 <--- b7[0] + 1; %? error: b7 is not an array (it has type int)
	b7 <--- b7[0] + 1.50; %? error: b7 is not an array (it has type int)
	return;
end
```

**`tests/semantic/stack_mixed.txt`**

```text
% A function that calls a recursive function is checked over one activation
% plus the whole stack use of its non-recursive callees: 3145744 bytes of
% locals plus a call of _g (16-byte block, 3153760 bytes of stack) exceed 6 MiB.
_g input parameter list [int b2];
	type int[131072] : d2;
	type int[131072] : d3;
	type int[131072] : d4;
	write(b2);
	return;
end

_h input parameter list [int b2]
output parameter list [int b3];
	[b3] <--- call _h with parameters [b2];
	return [b3];
end

_main %? error: _main needs about 6299536 bytes of stack for a single call; the limit is 6291456
	type int[131072] : d2;
	type int[131072] : d3;
	type int[131072] : d4;
	type int : b2;
	type int : b3;
	call _g with parameters [b2];
	[b3] <--- call _h with parameters [b2];
	return;
end
```


**`tests/semantic/depth_new_roots.txt`**

Each annotated line holds an expression 1001 operators deep (`1 + 1 + ... + 1`
with 1002 ones), so the file is given as the Python code that writes it
(`python3 gen_depth.py tests/semantic`); every annotated statement reports
exactly one message, at its own line (5, 6, 9, 10, 11, 12, 13, 14):

```python
# Writes tests/semantic/depth_new_roots.txt
import sys, os
d = sys.argv[1] if len(sys.argv) > 1 else "tests/semantic"
E = "1" + " + 1" * 1001                     # 1001 operators deep
M = " %? error: expression is too long or too deeply nested"
lines = ["% The 1000-operator depth limit applies to every new expression root.",
         "_main",
         "\ttype int[2] : b2;",
         "\ttype int : b3;",
         "\tb2[" + E + "] <--- 1;" + M,
         "\tif (b2[" + E + "] < 1) then" + M,
         "\t\twrite(b3);",
         "\tendif",
         "\tread(b2[" + E + "]);" + M,
         "\twrite(b2[" + E + "]);" + M,
         "\treadchar(b2[" + E + "]);" + M,
         "\tprint(\"x\", " + E + ");" + M,
         "\twritechar(" + E + ");" + M,
         "\texit(" + E + ");" + M,
         "\treturn;",
         "end"]
open(os.path.join(d, "depth_new_roots.txt"), "w").write("\n".join(lines) + "\n")
```


## C.4 Program tests (`tests/programs/`)

Compiled with `--build`, run with the `%? stdin:` lines, compared with the
`%? stdout:` lines and `%? exit:` status. They are also covered by the
existing "assembly for every test program is valid NASM" test and by the
parse-tree test (which exercises the `\x20` display of A.1.6, since several
files contain `" "` and `' '`). The existing `big_stack.txt` (about 5 MiB of
stack in a non-recursive chain) must keep passing with the runtime guard in
place.

**`tests/programs/char_literals_print.txt`**

```text
% Character literals are int constants; print writes ints (%lld), reals
% (%.2f) and strings without adding a newline.
%? stdout: 97 48 10 9 13 0 92 39 34 32
%? stdout: a1b-2c2.50d
%? stdout: q"b\s'%
%? stdout: 3
%? stdout: 100
_main
	type int : b2;
	type real : c2;
	print('a', " ", '0', " ", '\n', " ", '\t', " ", '\r', " ", '\0', " ", '\\', " ", '\'', " ", '"', " ", ' ', "\n");
	b2 <--- 0 - 2;
	c2 <--- 2.50;
	print("a", 1, "b", b2, "c", c2, "d\n");
	print("q\"b\\s'%\n");
	b2 <--- 'd' - 'a';
	write(b2);
	write('d');
	return;
end
```

**`tests/programs/writechar.txt`**

```text
% writechar writes the low 8 bits of its value as one byte.
%? stdout: ABC
%? stdout: A!
_main
	type int : b2;
	b2 <--- 'A';
	writechar(b2);
	writechar(b2 + 1);
	writechar('C');
	writechar('\n');
	writechar(256 + 'A');
	writechar(33);
	writechar(10);
	return;
end
```

**`tests/programs/readchar_echo.txt`**

```text
% readchar returns each byte of the input, then -1 at end of input and on
% every later call; writechar writes one byte.
%? stdin: Hi there
%? stdin: x
%? stdout: Hi there
%? stdout: x
%? stdout: bytes=11 lines=2 eof=-1 again=-1
_main
	type int : b2;
	type int : b3;
	type int : b4;
	readchar(b2);
	while (b2 >= 0)
		writechar(b2);
		b3 <--- b3 + 1;
		if (b2 == '\n') then
			b4 <--- b4 + 1;
		endif
		readchar(b2);
	endwhile
	print("bytes=", b3, " lines=", b4, " eof=", b2);
	readchar(b2);
	print(" again=", b2, "\n");
	return;
end
```

**`tests/programs/readchar_empty_input.txt`**

```text
% With no input at all the first readchar already returns -1.
%? stdout: -1
_main
	type int : b2;
	readchar(b2);
	write(b2);
	return;
end
```

**`tests/programs/read_then_readchar.txt`**

```text
% read leaves the rest of the line in the input; readchar continues there.
%? stdin: 42 z
%? stdout: 42
%? stdout: 32 122 10 -1
_main
	type int : b2;
	type int : b3;
	type int : b4;
	type int : b5;
	type int : b6;
	read(b2);
	write(b2);
	readchar(b3);
	readchar(b4);
	readchar(b5);
	readchar(b6);
	print(b3, " ", b4, " ", b5, " ", b6, "\n");
	return;
end
```

**`tests/programs/exit_status.txt`**

```text
% exit flushes the output and stops the program with the given status.
%? stdout: before
%? exit: 3
_main
	type int : b2;
	print("before\n");
	b2 <--- 1;
	if (b2 == 1) then
		exit(b2 + 2);
	endif
	print("after\n");
	return;
end
```

**`tests/programs/exit_status_wraps.txt`**

```text
% The exit status is the value modulo 256; output without a final newline
% is still flushed.
%? stdout: x
%? exit: 255
_main
	print("x");
	exit(0 - 1);
	return;
end
```

**`tests/programs/arrays.txt`**

```text
% Local and global arrays of int and real: zero-initialised, indexed by
% expressions, used in conditions, read into and written element by element.
%? stdin: 5 7
%? stdin: 1.25
%? stdout: 0 0
%? stdout: 55
%? stdout: 10 9 8 7 6 5 4 3 2 1
%? stdout: 12
%? stdout: 1.25
%? stdout: 3.75
%? stdout: 3
_fill input parameter list [int b2];
	% writes the global array d2, which is declared in _main
	type int : b3;
	b3 <--- 0;
	while (b3 < 10)
		d2[b3] <--- b3 + 1;
		b3 <--- b3 + 1;
	endwhile
	return;
end

_main
	type int[10] : d2 : global;
	type int[10] : b2;
	type real[4] : c2;
	type int : b3;
	type int : b4;
	print(b2[0], " ", d2[9], "\n");
	call _fill with parameters [b3];
	b3 <--- 0;
	b4 <--- 0;
	while (b3 < 10)
		b4 <--- b4 + d2[b3];
		b2[9 - b3] <--- d2[b3];
		b3 <--- b3 + 1;
	endwhile
	write(b4);
	b3 <--- 0;
	while (b3 < 10)
		print(b2[b3]);
		if (b3 < 9) then
			print(" ");
		endif
		b3 <--- b3 + 1;
	endwhile
	print("\n");
	read(b2[0]);
	read(b2[b2[0] - 4]);
	b4 <--- b2[0] + b2[1];
	write(b4);
	read(c2[3]);
	write(c2[3]);
	c2[0] <--- c2[3] + 2.50;
	write(c2[0]);
	b3 <--- 0;
	while (d2[b3] != 4)
		b3 <--- b3 + 1;
	endwhile
	write(b3);
	return;
end
```

**`tests/programs/array_reverse_line.txt`**

```text
% readchar into array elements; elements in while conditions.
%? stdin: hello
%? stdout: olleh
_main
	type int[100] : c2;
	type int : b2;
	readchar(c2[0]);
	while (c2[b2] != '\n')
		b2 <--- b2 + 1;
		readchar(c2[b2]);
	endwhile
	while (b2 > 0)
		b2 <--- b2 - 1;
		writechar(c2[b2]);
	endwhile
	writechar('\n');
	return;
end
```

**`tests/programs/array_bounds.txt`**

```text
% An index outside 0 .. length-1 stops the program with exit status 1.
%? stdout: 1
%? stdout: Runtime error: index 5 out of bounds for array c2 of length 5 at line 15
%? exit: 1
_main
	type int[5] : c2;
	type int : b2;
	b2 <--- 0;
	while (b2 < 5)
		c2[b2] <--- 1;
		b2 <--- b2 + 1;
	endwhile
	write(c2[4]);
	b2 <--- c2[0] + 3;
	b2 <--- c2[b2 + 1];
	write(b2);
	return;
end
```

**`tests/programs/array_negative_index.txt`**

```text
% Negative indices are out of bounds too; the message names the array.
%? stdout: Runtime error: index -1 out of bounds for array d2 of length 3 at line 5
%? exit: 1
_set input parameter list [int b2];
	d2[b2] <--- 7;
	return;
end

_main
	type int[3] : d2 : global;
	type int : b2;
	b2 <--- 0 - 1;
	call _set with parameters [b2];
	write(d2[0]);
	return;
end
```

**`tests/programs/short_circuit.txt`**

```text
% &&& and @@@ evaluate their right operand only when needed, so a guard can
% protect an array access.
%? stdout: 3
%? stdout: 1
_main
	type int[3] : c2;
	type int : b2;
	b2 <--- 0;
	while ((b2 < 3) &&& (c2[b2] == 0))
		b2 <--- b2 + 1;
	endwhile
	write(b2);
	if ((b2 >= 3) @@@ (c2[b2] == 1)) then
		write(1);
	else
		write(0);
	endif
	return;
end
```

**`tests/programs/element_assignment_order.txt`**

```text
% In an element assignment the right-hand side is evaluated before the index.
%? stdout: Runtime error: division by zero
%? exit: 1
_main
	type int[3] : c2;
	type int : b2;
	c2[7] <--- 1 / b2;
	return;
end
```

**`tests/programs/recursion.txt`**

```text
% Recursion, mutual recursion and calls to functions defined later.
%? stdout: 3628800
%? stdout: 6765
%? stdout: 1
%? stdout: 0
%? stdout: 15
_fact input parameter list [int b2]
output parameter list [int b3];
	type int : b4;
	if (b2 <= 1) then
		b3 <--- 1;
	else
		b4 <--- b2 - 1;
		[b3] <--- call _fact with parameters [b4];
		b3 <--- b3 * b2;
	endif
	return [b3];
end

_fib input parameter list [int b2]
output parameter list [int b3];
	type int : b4;
	type int : b5;
	type int : b6;
	if (b2 < 2) then
		b3 <--- b2;
	else
		b4 <--- b2 - 1;
		[b5] <--- call _fib with parameters [b4];
		b4 <--- b2 - 2;
		[b6] <--- call _fib with parameters [b4];
		b3 <--- b5 + b6;
	endif
	return [b3];
end

_even input parameter list [int b2]
output parameter list [int b3];
	type int : b4;
	if (b2 == 0) then
		b3 <--- 1;
	else
		b4 <--- b2 - 1;
		[b3] <--- call _odd with parameters [b4];
	endif
	return [b3];
end

_odd input parameter list [int b2]
output parameter list [int b3];
	type int : b4;
	if (b2 == 0) then
		b3 <--- 0;
	else
		b4 <--- b2 - 1;
		[b3] <--- call _even with parameters [b4];
	endif
	return [b3];
end

_first input parameter list [int b2]
output parameter list [int b3];
	[b3] <--- call _later with parameters [b2];
	return [b3];
end

_later input parameter list [int b2]
output parameter list [int b3];
	b3 <--- b2 * 3;
	return [b3];
end

_main
	type int : b2;
	type int : b3;
	b2 <--- 10;
	[b3] <--- call _fact with parameters [b2];
	write(b3);
	b2 <--- 20;
	[b3] <--- call _fib with parameters [b2];
	write(b3);
	b2 <--- 10;
	[b3] <--- call _even with parameters [b2];
	write(b3);
	b2 <--- 7;
	[b3] <--- call _even with parameters [b2];
	write(b3);
	b2 <--- 5;
	[b3] <--- call _first with parameters [b2];
	write(b3);
	return;
end
```

**`tests/programs/deep_recursion.txt`**

```text
% 100000 nested calls (48 bytes of stack each) fit in the 6 MiB budget.
%? stdout: 5000050000
_sum input parameter list [int b2]
output parameter list [int b3];
	type int : b4;
	if (b2 == 0) then
		b3 <--- 0;
	else
		b4 <--- b2 - 1;
		[b3] <--- call _sum with parameters [b4];
		b3 <--- b3 + b2;
	endif
	return [b3];
end

_main
	type int : b2;
	type int : b3;
	b2 <--- 100000;
	[b3] <--- call _sum with parameters [b2];
	write(b3);
	return;
end
```

**`tests/programs/stack_overflow.txt`**

```text
% Unbounded recursion stops with an error message instead of crashing.
%? stdout: start
%? stdout: Runtime error: stack overflow (recursion too deep)
%? exit: 1
_down input parameter list [int b2]
output parameter list [int b3];
	b2 <--- b2 + 1;
	[b3] <--- call _down with parameters [b2];
	return [b3];
end

_main
	type int : b2;
	type int : b3;
	print("start\n");
	[b3] <--- call _down with parameters [b2];
	write(b3);
	return;
end
```

**`tests/programs/calls_update_globals.txt`**

```text
% A call in a loop body counts as an update of every global variable.
%? stdout: 5
_step input parameter list [int b2];
	d2 <--- d2 + 1;
	return;
end

_main
	type int : d2 : global;
	type int : b2;
	while (d2 < 5)
		call _step with parameters [b2];
	endwhile
	write(d2);
	return;
end
```

**`tests/programs/keyword_field_names.txt`**

```text
% The new keywords stay usable as record field names.
%? stdout: 1 2 3 4
_main
	record #words
		type int : print;
		type int : exit;
		type int : readchar;
		type int : writechar;
	endrecord
	type #words : d2;
	d2.print <--- 1;
	d2.exit <--- d2.print + 1;
	d2.readchar <--- 3;
	d2.writechar <--- d2.readchar + 1;
	write(d2);
	return;
end
```

**`tests/programs/array_bounds_read.txt`**

```text
% read into an element checks the index before reading.
%? stdin: 7
%? stdout: Runtime error: index 3 out of bounds for array c2 of length 3 at line 9
%? exit: 1
_main
	type int[3] : c2;
	type int : b2;
	b2 <--- 3;
	read(c2[b2]);
	write(c2[0]);
	return;
end
```

**`tests/programs/array_bounds_readchar.txt`**

```text
% readchar into an element checks the index before reading.
%? stdin: x
%? stdout: Runtime error: index -1 out of bounds for array c2 of length 3 at line 9
%? exit: 1
_main
	type int[3] : c2;
	type int : b2;
	b2 <--- 3;
	readchar(c2[b2 - 4]);
	write(c2[0]);
	return;
end
```

**`tests/programs/real_array.txt`**

```text
% Elements of a real array: int values are converted on store; reals print
% with two decimals.
%? stdout: 3.00
%? stdout: 1.50
%? stdout: 4.50|-0.25
%? stdout: 1
_main
	type real[3] : c2;
	type int : b2;
	c2[1] <--- 3;
	b2 <--- 2;
	c2[b2] <--- c2[1] / b2;
	write(c2[1]);
	write(c2[2]);
	c2[0] <--- 0.25 - c2[0] - 0.50;
	print(c2[1] + c2[2], "|", c2[0], "\n");
	if (c2[2] < 2) then
		write(1);
	else
		write(0);
	endif
	return;
end
```

**`tests/programs/print_percent.txt`**

```text
% A % inside a string or character literal is not a comment, and print
% writes strings literally (no printf formatting).
%? stdout: 100% %d %s %%
%? stdout: %
_main
	type int : b2;
	b2 <--- 100;
	print(b2, "% %d %s %%\n"); % trailing comment
	writechar('%');
	writechar('\n');
	return;
end
```


For `array_bounds_read.txt` and `array_bounds_readchar.txt` the index is
checked before input is read (A.4, A.7.3); whether input was consumed cannot
be observed once the program has stopped, so these tests check the message,
the line and the status.

Two more tests go into `add_program_tests` (skipped when `CAN_RUN` is false),
because their output is not text the `%? stdout:` mechanism can express:

```python
    def writechar_byte_255():
        # writechar(0 - 1) writes the byte 0xFF (value mod 256)
        src = scratch("wc255", "_main\n\twritechar(0 - 1);\n"
                      "\twritechar(10);\n\treturn;\nend\n")
        exe = os.path.join(BUILD, "bin", "writechar255")
        p = compiler("--build", src, exe)
        expect(p.returncode == 0, p.stdout[-1000:])
        r = subprocess.run([exe], capture_output=True, timeout=10)
        expect(r.returncode == 0 and r.stdout == b"\xff\n",
               "got %r, exit %d" % (r.stdout, r.returncode))
    suite.add("run: writechar(0 - 1) writes byte 255", writechar_byte_255)
```

Register it with `suite.add` only when `CAN_RUN` is true; otherwise call
`suite.skip(name, "needs x86-64 Linux with nasm")`, like the other run tests.

## C.5 Driver and printer tests (`add_driver_tests`)

Add two tests on `tests/semantic/extensions_ok.txt` (line numbers refer to
that file as given in C.3):

* `driver: --ast prints the extensions` — `./compiler --ast` stdout contains
  each of these lines (substring match, like the existing `ast_flag` test):

  ```
  Record #rec (line 34)
  Field print : int
  Declare c2 : int[16] (global) (line 41)
  Declare c3 : real[4] (line 42)
  ReadChar (line 45): b2
  While (line 46): b2 != '\n'
  Assign (line 47): c2[b3] <--- b2
  Assign (line 51): d2.print <--- (c2[0] + 'a')
  ReadChar (line 53): d2.readchar
  Assign (line 54): c3[(b3 - 1)] <--- (c2[(b3 - 1)] / 2.00)
  If (line 55): (c3[0] > 1.50) &&& (c2[c2[0]] == '\t')
  WriteChar (line 56): c2[0]
  Print (line 58): "count: ", b3, ", first: ", c2[0], ", half: ", c3[0], "\n"
  Call (line 59): [b3] <--- _count with [b2]
  Write (line 61): c2[b3]
  Exit (line 63): 2
  ```

* `driver: --symbols prints arrays` — `./compiler --symbols` stdout, with
  runs of whitespace collapsed to one space per line (like `symbols_flag`),
  contains the rows:

  ```
  #rec record 32 print:int@0 exit:int@8 readchar:int@16 writechar:int@24
  c2 global int[16] 128 G_c2 41
  d2 local #rec 32 rbp-32 40
  c3 local real[4] 32 rbp-64 42
  b3 local int 8 rbp-80 44
  Function _main (locals 80 bytes, parameter block 0 bytes)
  ```

* `driver: option 1 keeps % inside literals` — run the menu
  (`./compiler tests/programs/print_percent.txt <out>` with stdin `"1\n0\n"`)
  and check that stdout contains `\tprint(b2, "% %d %s %%\n"); \n` (the
  string intact, the trailing comment removed, the space before it kept) and
  `\twritechar('%');\n`, and contains neither `trailing comment` nor
  `A % inside`.

The existing driver tests (`--ast prints every construct`,
`--symbols prints types, aliases and addresses`) must pass unchanged.

## C.6 Toy compiler acceptance tests

Layout:

* `toy/toycc.txt` — the toy compiler (Part B), written in the extended
  language.
* `tests/toy/*.tl` — TL programs with `#?` annotations at the start of lines:
  `#? stdout: <text>` (repeatable), `#? exit: <n>`, `#? error: <line>: <message>`.
  The twelve programs of B.6, `div_by_zero.tl`, the six programs of B.7, and
  the seventeen below (36 files in all).

Additional programs (`tests/toy/`):

**`tests/toy/paren_condition.tl`**

```text
#? stdout: 10
#? stdout: 1
# a condition may start with a parenthesised expression
x = 0;
while (x) < 10 { x = x + 1; }
if (x) == (5 + 5) { print x; }
if (x - 9) >= 1 { print 1; }
```

**`tests/toy/zero_init.tl`**

```text
#? stdout: 0
# every variable starts at 0; y is defined (textually) but never assigned
while 0 > 1 { y = 5; }
print y;
```


**`tests/toy/limits_names.tl`, `tests/toy/limits_nesting.tl`** — the B.5
limits exactly: 500 distinct names of 16 bytes (8000 bytes), and nesting depth
200 for each of parentheses, unary minus and blocks. Generated by
`python3 gen_limits.py tests/toy`:

```python
# Writes tests/toy/limits_names.tl and tests/toy/limits_nesting.tl
import sys, os
d = sys.argv[1] if len(sys.argv) > 1 else "tests/toy"
names = ["v%015d" % i for i in range(1, 501)]          # 500 names x 16 bytes
lines = ["#? stdout: 125250", "#? stdout: 499",
         "# exactly 500 distinct variable names, 8000 bytes of names"]
lines += ["%s = %d;" % (n, i) for i, n in enumerate(names, 1)]
lines += ["%s = %s;" % (names[-1], " + ".join(names)),
          "print %s;" % names[-1], "print %s;" % names[-2]]
open(os.path.join(d, "limits_names.tl"), "w").write("\n".join(lines) + "\n")
lines = ["#? stdout: 7", "#? stdout: 7",
         "# nesting depth 200: parentheses, unary minus, blocks",
         "x = " + "(" * 200 + "7" + ")" * 200 + ";",
         "print x;",
         "y = " + "-" * 200 + "x;"]
lines += ["if x == 7 {"] * 200 + ["print y;"] + ["}"] * 200
open(os.path.join(d, "limits_nesting.tl"), "w").write("\n".join(lines) + "\n")
```


Additional error cases (`tests/toy/`):

**`tests/toy/err_no_brace_after_cond.tl`**

```text
#? error: 3: expected '{'
x = 0;
while x < 10 print x;
```

**`tests/toy/err_undefined_before_lex.tl`**

```text
#? error: 3: undefined variable x
y = 1;
print x
$
```

**`tests/toy/err_byte_200.tl`**

This file has , so it is given as the Python code that creates it:

```python
open("tests/toy/err_byte_200.tl", "wb").write(
    b'#? error: 3: unexpected byte 200\n'
    b'x = 1;\n'
    b'\xc8\n')
```


**`tests/toy/err_no_expr.tl`**

```text
#? error: 3: expected expression
x = 1;
x = x + ;
```

**`tests/toy/err_no_paren.tl`**

```text
#? error: 2: expected ')'
print (1 + 2;
```

**`tests/toy/err_self_reference.tl`**

```text
#? error: 2: undefined variable x
x = x + 1;
```

**`tests/toy/err_else_without_brace.tl`**

```text
#? error: 6: expected '{'
y = 1;
if y < 2 {
    z = 5;
}
else print z;
```

**`tests/toy/err_stray_brace.tl`**

```text
#? error: 6: expected statement
a = 1;
while a < 2 {
  a = a + 1;
}
}
```

**`tests/toy/err_else_alone.tl`**

```text
#? error: 2: expected statement
else { }
```

**`tests/toy/err_long_identifier.tl`**

```text
#? error: 2: identifier too long
abcdefghijabcdefghijabcdefghijabc = 1;
```

**`tests/toy/err_no_assign.tl`**

```text
#? error: 3: expected '='
a = 1;
a 2;
```

**`tests/toy/err_bang.tl`**

```text
#? error: 2: unexpected character '!'
if 1 ! 2 { }
```

**`tests/toy/err_control_byte.tl`**

This file has a control byte, so it is given as the Python code that creates it:

```python
open("tests/toy/err_control_byte.tl", "wb").write(
    b'#? error: 2: unexpected byte 7\n'
    b'x = 1;\x07\n')
```


Runner (`tests/run_tests.py`): add the section below, call
`add_toy_tests(suite)` in `main()` after `add_program_tests(suite)`, and
mention `tests/toy/` and `toy/toycc.txt` in the module docstring. While
`toy/toycc.txt` does not exist the toy tests are reported as skipped; once it
exists they must all pass. Files are handled as bytes because some contain CR
and control bytes.

```python
# ===================================================== toy compiler tests

TOY_SRC = os.path.join(ROOT, "toy", "toycc.txt")
TOY_TESTS = os.path.join(TESTS, "toy")
_toy = {}
_toy_lock = threading.Lock()


def toy_annotations(path, data):
    """#? stdout: / #? exit: / #? error: lines at the start of a TL file."""
    ann = {"stdout": [], "exit": 0, "error": None}
    for lineno, line in enumerate(data.decode("latin-1").splitlines(), 1):
        if not line.startswith("#?"):
            continue
        key, _, value = line[2:].strip().partition(":")
        key, value = key.strip(), value.strip()
        if key == "stdout":
            ann["stdout"].append(value)
        elif key == "exit":
            ann["exit"] = int(value)
        elif key == "error":
            ann["error"] = value
        else:
            raise Failure("%s:%d: unknown directive %r" % (path, lineno, key))
    return ann


def toycc():
    """Builds toy/toycc.txt once; tests run in parallel threads."""
    with _toy_lock:
        if "exe" not in _toy:
            exe = os.path.join(BUILD, "bin", "toycc")
            p = compiler("--build", TOY_SRC, exe, timeout=120)
            _toy["exe"] = exe if p.returncode == 0 else None
            _toy["log"] = p.stdout[-3000:] + p.stderr[-1000:]
    expect(_toy["exe"], "toy/toycc.txt does not build:\n" + _toy["log"])
    return _toy["exe"]


def run_bytes(args, data, timeout):
    p = subprocess.run(args, input=data, capture_output=True,
                       timeout=timeout)
    if p.returncode < 0:
        raise Failure("%s was killed by signal %d (crash)" %
                      (args[0], -p.returncode))
    return p


def add_toy_tests(suite):
    if not os.path.exists(TOY_SRC):
        suite.skip("toy: toy/toycc.txt", "not written yet")
        return
    suite.add("parser: parse tree of toy/toycc.txt",
              lambda: check_parse_tree(TOY_SRC))

    def no_errors():
        p = compiler("--check", TOY_SRC, timeout=60)
        expect(p.returncode == 0, p.stdout[-2000:])
    suite.add("toy: toycc.txt compiles without errors", no_errors)

    for f in sorted(os.listdir(TOY_TESTS)):
        if not f.endswith(".tl"):
            continue
        path, name = os.path.join(TOY_TESTS, f), "toy: " + f
        if not CAN_RUN:
            suite.skip(name, "needs x86-64 Linux with nasm")
            continue

        def test(path=path, f=f):
            data = open(path, "rb").read()
            ann = toy_annotations(path, data)
            p = run_bytes([toycc()], data, 20)
            out = p.stdout.decode("latin-1")
            if ann["error"] is not None:
                want = "error: " + ann["error"]
                last = out.splitlines()[-1:]
                expect(p.returncode == 1,
                       "toycc exit status %d, expected 1" % p.returncode)
                expect(last == [want], "last line %r, expected %r" %
                       (last, want))
                expect(out.endswith("\n"),
                       "the error line must end with LF")
                return
            expect(p.returncode == 0, "toycc failed (exit %d):\n%s" %
                   (p.returncode, out[-1500:]))
            base = os.path.join(BUILD, "bin", "toy_" + f[:-3])
            with open(base + ".asm", "w", encoding="latin-1") as a:
                a.write(out)
            r = run(["nasm", "-f", "elf64", base + ".asm", "-o", base + ".o"])
            expect(r.returncode == 0, "nasm rejected the output of toycc:\n" +
                   r.stderr[-1500:])
            r = run(["gcc", "-no-pie", base + ".o", "-o", base])
            expect(r.returncode == 0, "linking failed:\n" + r.stderr[-1500:])
            r = run_bytes([base], b"", 10)
            got = r.stdout.decode("latin-1").splitlines()
            expect(got == ann["stdout"], "output differs\nexpected: %r\n"
                   "     got: %r" % (ann["stdout"], got))
            expect(r.returncode == ann["exit"], "exit status %d, expected %d"
                   % (r.returncode, ann["exit"]))
        suite.add(name, test)
```

## C.7 Unit tests (`tests/unit/test_units.c`)

* Update the grammar counts and FIRST/FOLLOW arrays exactly as in A.2.3;
  `test_grammar_is_ll1` and `test_parse_table` must pass unchanged.
* `test_bitset`: line 149 becomes
  `CHECK(!bs_contains(&a, 1) && !bs_contains(&a, 62));` and line 158 becomes
  `CHECK(bs_contains(&c, 64));` (A.2.3); without this the test fails with the
  enlarged enum.
* New `test_char_literal_value`: `charLiteralValue("'a'") == 97`,
  `("'\\n'") == 10`, `("'\\t'") == 9`, `("'\\r'") == 13`, `("'\\0'") == 0`,
  `("'\\\\'") == 92`, `("'\\''") == 39`, `("'\"'") == 34`,
  `("'\\\"'") == 34`, `("' '") == 32`.
* New `test_string_literal_table`: lexing `print("a\tb\n");` (C source
  `"print(\"a\\tb\\n\");"`) gives a `TK_STR` token whose `literal` index
  yields, through `stringLiteralText`, the 4 bytes `a`, TAB, `b`, LF —
  i.e. `len == 4` and `memcmp(text, "a\tb\n", 4) == 0`;
  every other token has `literal == -1`.
* New `test_new_keywords`: `terminalFromString("TK_PRINT") == TK_PRINT` (and
  the five other new names) — already implied by `test_terminal_names`, which
  must pass with the enlarged enum.

## C.8 Test inventory

| Area | Where | Count |
|---|---|---|
| lexer | `LEXER_CASES` | 40 new cases |
| syntax | `tests/syntax/` | 11 new files |
| semantic | `tests/semantic/` | 8 new files (one generated), `call_order.txt` replaced |
| run | `tests/programs/` | 22 new files + `writechar_byte_255` |
| driver | `add_driver_tests` | 3 new tests |
| unit | `tests/unit/test_units.c` | counts/sets and `test_bitset` updated, 2–3 new tests |
| toy | `tests/toy/` + `add_toy_tests` | 36 `.tl` files (two generated) + 2 checks of `toycc.txt` |
