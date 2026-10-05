// Unit tests for the compiler internals. Built and run by tests/run_tests.py;
// can also be built by hand from the repository root:
//   gcc -I. tests/unit/test_units.c lexer.c logging.c parser.c ast.c
//       symbolTable.c semantic.c codegen.c compiler.c -o unit && ./unit
#include "../../ast.h"
#include "../../compiler.h"
#include "../../lexer.h"
#include "../../logging.h"
#include "../../parser.h"
#include "../../semantic.h"
#include "../../symbolTable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

TokenType lookupKeyword(Hashmap *h, const char *key); // lexer.c, not exported

static int failures, checks;
static const char *currentTest;

#define CHECK(cond)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(cond)) {                                                             \
      failures++;                                                              \
      printf("FAIL %s (%s:%d): %s\n", currentTest, __FILE__, __LINE__,        \
             #cond);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_EQ_INT(a, b)                                                     \
  do {                                                                         \
    long long va_ = (long long)(a), vb_ = (long long)(b);                      \
    checks++;                                                                  \
    if (va_ != vb_) {                                                          \
      failures++;                                                              \
      printf("FAIL %s (%s:%d): %s == %lld, expected %lld\n", currentTest,      \
             __FILE__, __LINE__, #a, va_, vb_);                                \
    }                                                                          \
  } while (0)

#define CHECK_EQ_STR(a, b)                                                     \
  do {                                                                         \
    const char *sa_ = (a), *sb_ = (b);                                         \
    checks++;                                                                  \
    if (strcmp(sa_, sb_) != 0) {                                               \
      failures++;                                                              \
      printf("FAIL %s (%s:%d): %s == \"%s\", expected \"%s\"\n", currentTest,  \
             __FILE__, __LINE__, #a, sa_, sb_);                                \
    }                                                                          \
  } while (0)

#define TEST(name) static void name(void)
#define RUN(name)                                                              \
  do {                                                                         \
    currentTest = #name;                                                       \
    name();                                                                    \
  } while (0)

/* ----------------------------------------------------------------- helpers */

static char tmpPath[256];

static const char *writeSource(const char *text) {
  snprintf(tmpPath, sizeof(tmpPath), "tests/build/unit_src_%d.txt",
           (int)getpid());
  FILE *f = fopen(tmpPath, "w");
  fputs(text, f);
  fclose(f);
  return tmpPath;
}

// silence what the pipeline prints while analysing a snippet
static int analyse(const char *text, Compilation *c) {
  const char *path = writeSource(text);
  fflush(stdout);
  FILE *saved = fdopen(dup(fileno(stdout)), "w");
  freopen("/dev/null", "w", stdout);
  int ok = runAnalysis(path, c);
  fflush(stdout);
  dup2(fileno(saved), fileno(stdout));
  fclose(saved);
  remove(path);
  return ok;
}

static TokenList lex(const char *text, int *errors) {
  const char *path = writeSource(text);
  fflush(stdout);
  FILE *saved = fdopen(dup(fileno(stdout)), "w");
  freopen("/dev/null", "w", stdout);
  State s = initializeState(path, 0);
  TokenList tl = scan(&s);
  fflush(stdout);
  dup2(fileno(saved), fileno(stdout));
  fclose(saved);
  remove(path);
  if (errors)
    *errors = s.errorCount;
  return tl;
}

static Grammar *grammar;
static FirstFollowSets ff;
static ParseTable pt;

static int nt(const char *name) {
  for (int i = 0; i < grammar->numNT; i++)
    if (strcmp(grammar->ntNames[i], name) == 0)
      return i;
  printf("FAIL: unknown non-terminal %s\n", name);
  failures++;
  return 0;
}

// set equality between a bitset and a TK_DOLLAR-terminated list
static int setIs(BitSet *s, const int *expected) {
  BitSet want;
  bs_clear(&want);
  for (int i = 0; expected[i] != -1; i++)
    bs_add(&want, expected[i]);
  int same = memcmp(&want, s, sizeof(BitSet)) == 0;
  if (!same) {
    printf("      got:");
    for (int t = 0; t < NUM_TOKENS; t++)
      if (bs_contains(s, t))
        printf(" %s", tokenTypeToString((TokenType)t));
    printf("\n");
  }
  return same;
}

#define END -1

/* ----------------------------------------------------------------- lexer */

TEST(test_bitset) {
  BitSet a, b;
  bs_clear(&a);
  bs_clear(&b);
  CHECK(bs_is_empty(&a));
  bs_add(&a, 0);
  bs_add(&a, 63);
  bs_add(&a, 64);
  bs_add(&a, NUM_TOKENS - 1);
  CHECK(bs_contains(&a, 0) && bs_contains(&a, 63) && bs_contains(&a, 64));
  CHECK(!bs_contains(&a, 1) && !bs_contains(&a, 62));
  CHECK(!bs_is_empty(&a));
  CHECK_EQ_INT(bs_union(&b, &a), 1);
  CHECK_EQ_INT(bs_union(&b, &a), 0); // nothing new the second time
  BitSet c;
  bs_clear(&c);
  bs_add(&a, TK_EPS);
  bs_union_no_eps(&c, &a);
  CHECK(!bs_contains(&c, TK_EPS));
  CHECK(bs_contains(&c, 64));
}

TEST(test_keywords) {
  Hashmap h = initializeKeywordMap();
  const char *words[] = {"with",     "parameters", "end",       "while",
                         "union",    "endunion",   "definetype", "as",
                         "type",     "global",     "parameter", "list",
                         "input",    "output",     "int",       "real",
                         "endwhile", "if",         "then",      "endif",
                         "read",     "write",      "return",    "call",
                         "record",   "endrecord",  "else"};
  TokenType toks[] = {TK_WITH,  TK_PARAMETERS, TK_END,    TK_WHILE,
                      TK_UNION, TK_ENDUNION,   TK_DEFINETYPE, TK_AS,
                      TK_TYPE,  TK_GLOBAL,     TK_PARAMETER, TK_LIST,
                      TK_INPUT, TK_OUTPUT,     TK_INT,    TK_REAL,
                      TK_ENDWHILE, TK_IF,      TK_THEN,   TK_ENDIF,
                      TK_READ,  TK_WRITE,      TK_RETURN, TK_CALL,
                      TK_RECORD, TK_ENDRECORD, TK_ELSE};
  for (unsigned i = 0; i < sizeof(words) / sizeof(*words); i++)
    CHECK_EQ_STR(tokenTypeToString(lookupKeyword(&h, words[i])),
                 tokenTypeToString(toks[i]));
  CHECK_EQ_INT(lookupKeyword(&h, "endrecorb"), TK_ERROR);
  CHECK_EQ_INT(lookupKeyword(&h, "maths"), TK_ERROR);
  CHECK_EQ_INT(lookupKeyword(&h, "x"), TK_ERROR);
  CHECK(hash("anything") < HASH_SIZE);
}

TEST(test_scan_token_stream) {
  int errors;
  TokenList tl = lex("b2 <--- c3bb * 12.50;\n% note\n_fun #rec", &errors);
  TokenType want[] = {TK_ID,   TK_ASSIGNOP, TK_ID,      TK_MUL,  TK_RNUM,
                      TK_SEM,  TK_COMMENT,  TK_FUNID,   TK_RUID, TK_DOLLAR};
  CHECK_EQ_INT(errors, 0);
  CHECK_EQ_INT(tl.size, (int)(sizeof(want) / sizeof(*want)));
  for (int i = 0; i < tl.size && i < 10; i++)
    CHECK_EQ_STR(tokenTypeToString(tl.buf[i].type),
                 tokenTypeToString(want[i]));
  CHECK_EQ_STR(tl.buf[2].lexeme, "c3bb");
  CHECK_EQ_STR(tl.buf[4].lexeme, "12.50");
  CHECK_EQ_INT(tl.buf[6].lineNo, 2);
  CHECK_EQ_INT(tl.buf[7].lineNo, 3);
  CHECK_EQ_STR(tokenTypeToLexeme(&tl.buf[1]), "<---");
  free(tl.buf);
}

TEST(test_scan_grows_buffer) {
  // the token list starts with capacity 10 and must grow
  char src[4096] = "";
  for (int i = 0; i < 500; i++)
    strcat(src, "+ ");
  int errors;
  TokenList tl = lex(src, &errors);
  CHECK_EQ_INT(tl.size, 501);
  CHECK(tl.capacity >= tl.size);
  CHECK_EQ_INT(tl.buf[499].type, TK_PLUS);
  CHECK_EQ_INT(tl.buf[500].type, TK_DOLLAR);
  free(tl.buf);
}

TEST(test_scan_missing_file) {
  // initializeState reports the missing file through perror (stderr)
  fflush(stdout);
  FILE *saved = fdopen(dup(fileno(stdout)), "w");
  freopen("/dev/null", "w", stdout);
  int savedErr = dup(2);
  int devnull = open("/dev/null", O_WRONLY);
  dup2(devnull, 2);
  State s = initializeState("tests/build/does-not-exist.txt", 0);
  TokenList tl = scan(&s);
  fflush(stdout);
  dup2(fileno(saved), fileno(stdout));
  fclose(saved);
  dup2(savedErr, 2);
  close(savedErr);
  close(devnull);
  CHECK(s.errorCount > 0);
  CHECK_EQ_INT(tl.size, 1); // just $
  free(tl.buf);
}

TEST(test_char_literal_value) {
  CHECK_EQ_INT(charLiteralValue("'a'"), 97);
  CHECK_EQ_INT(charLiteralValue("'\\n'"), 10);
  CHECK_EQ_INT(charLiteralValue("'\\t'"), 9);
  CHECK_EQ_INT(charLiteralValue("'\\r'"), 13);
  CHECK_EQ_INT(charLiteralValue("'\\0'"), 0);
  CHECK_EQ_INT(charLiteralValue("'\\\\'"), 92);
  CHECK_EQ_INT(charLiteralValue("'\\''"), 39);
  CHECK_EQ_INT(charLiteralValue("'\"'"), 34);
  CHECK_EQ_INT(charLiteralValue("'\\\"'"), 34);
  CHECK_EQ_INT(charLiteralValue("' '"), 32);
}

TEST(test_string_literal_table) {
  int errors;
  TokenList tl = lex("print(\"a\\tb\\n\");", &errors);
  CHECK_EQ_INT(errors, 0);
  int strings = 0;
  for (int i = 0; i < tl.size; i++) {
    if (tl.buf[i].type != TK_STR) {
      CHECK_EQ_INT(tl.buf[i].literal, -1);
      continue;
    }
    strings++;
    int len;
    const char *text = stringLiteralText(tl.buf[i].literal, &len);
    CHECK_EQ_INT(len, 4);
    CHECK(memcmp(text, "a\tb\n", 4) == 0);
    CHECK_EQ_STR(tl.buf[i].lexeme, "\"a\\tb\\n\"");
  }
  CHECK_EQ_INT(strings, 1);
  free(tl.buf);
}

TEST(test_new_keywords) {
  CHECK_EQ_INT(terminalFromString("TK_READCHAR"), TK_READCHAR);
  CHECK_EQ_INT(terminalFromString("TK_WRITECHAR"), TK_WRITECHAR);
  CHECK_EQ_INT(terminalFromString("TK_PRINT"), TK_PRINT);
  CHECK_EQ_INT(terminalFromString("TK_EXIT"), TK_EXIT);
  CHECK_EQ_INT(terminalFromString("TK_CHARLIT"), TK_CHARLIT);
  CHECK_EQ_INT(terminalFromString("TK_STR"), TK_STR);
  Hashmap h = initializeKeywordMap();
  CHECK_EQ_INT(lookupKeyword(&h, "readchar"), TK_READCHAR);
  CHECK_EQ_INT(lookupKeyword(&h, "writechar"), TK_WRITECHAR);
  CHECK_EQ_INT(lookupKeyword(&h, "print"), TK_PRINT);
  CHECK_EQ_INT(lookupKeyword(&h, "exit"), TK_EXIT);
  CHECK_EQ_INT(lookupKeyword(&h, "prints"), TK_ERROR);
}

/* ---------------------------------------------------------------- grammar */

TEST(test_terminal_names) {
  CHECK_EQ_INT(terminalFromString("TK_ID"), TK_ID);
  CHECK_EQ_INT(terminalFromString("TK_ASSIGNOP"), TK_ASSIGNOP);
  CHECK_EQ_INT(terminalFromString("TK_NE"), TK_NE);
  CHECK_EQ_INT(terminalFromString("eps"), TK_EPS);
  CHECK_EQ_INT(terminalFromString("TK_NOPE"), TK_ERROR);
  for (int t = 0; t < TK_EPS; t++)
    CHECK_EQ_INT(terminalFromString(tokenTypeToString((TokenType)t)), t);
}

TEST(test_grammar_loaded) {
  CHECK(grammar != NULL);
  CHECK_EQ_INT(grammar->numNT, 58);
  CHECK_EQ_INT(grammar->numRules, 113);
  CHECK_EQ_STR(getNTName(grammar, 0), "program");
  CHECK_EQ_STR(getNTName(grammar, -1), "?");
  // <program> ::= <otherFunctions> <mainFunction>
  GrammarRule *r = &grammar->rules[0];
  CHECK_EQ_INT(r->lhs, nt("program"));
  CHECK_EQ_INT(r->rhsLen, 2);
  CHECK_EQ_INT(r->rhs[0].kind, SYM_NON_TERMINAL);
  CHECK_EQ_INT(r->rhs[0].id, nt("otherFunctions"));
  // every eps alternative is a single TK_EPS symbol
  int epsRules = 0;
  for (int i = 0; i < grammar->numRules; i++)
    for (int j = 0; j < grammar->rules[i].rhsLen; j++)
      if (grammar->rules[i].rhs[j].kind == SYM_TERMINAL &&
          grammar->rules[i].rhs[j].id == TK_EPS) {
        epsRules++;
        CHECK_EQ_INT(grammar->rules[i].rhsLen, 1);
      }
  CHECK_EQ_INT(epsRules, 17);
  // no terminal in the grammar is unknown
  for (int i = 0; i < grammar->numRules; i++)
    for (int j = 0; j < grammar->rules[i].rhsLen; j++)
      if (grammar->rules[i].rhs[j].kind == SYM_TERMINAL)
        CHECK(grammar->rules[i].rhs[j].id != TK_ERROR);
}

TEST(test_first_sets) {
  int program[] = {TK_FUNID, TK_MAIN, END};
  CHECK(setIs(&ff.first[nt("program")], program));
  int stmt[] = {TK_ID, TK_WHILE, TK_IF, TK_READ, TK_WRITE, TK_READCHAR,
                TK_WRITECHAR, TK_PRINT, TK_SQL, TK_CALL, TK_EXIT, END};
  CHECK(setIs(&ff.first[nt("stmt")], stmt));
  int otherStmts[] = {TK_ID, TK_WHILE, TK_IF, TK_READ, TK_WRITE, TK_READCHAR,
                      TK_WRITECHAR, TK_PRINT, TK_SQL, TK_CALL, TK_EXIT,
                      TK_EPS, END};
  CHECK(setIs(&ff.first[nt("otherStmts")], otherStmts));
  int boolExpr[] = {TK_OP, TK_NOT, TK_ID, TK_NUM, TK_RNUM, TK_CHARLIT, END};
  CHECK(setIs(&ff.first[nt("booleanExpression")], boolExpr));
  int typeDefs[] = {TK_RECORD, TK_UNION, TK_DEFINETYPE, TK_EPS, END};
  CHECK(setIs(&ff.first[nt("typeDefinitions")], typeDefs));
  int stmts[] = {TK_RECORD, TK_UNION, TK_DEFINETYPE, TK_TYPE, TK_ID,
                 TK_WHILE, TK_IF, TK_READ, TK_WRITE, TK_READCHAR,
                 TK_WRITECHAR, TK_PRINT, TK_SQL, TK_CALL, TK_EXIT,
                 TK_RETURN, END};
  CHECK(setIs(&ff.first[nt("stmts")], stmts));
  int dataType[] = {TK_INT, TK_REAL, TK_RECORD, TK_UNION, TK_RUID, END};
  CHECK(setIs(&ff.first[nt("dataType")], dataType));
  int optSingle[] = {TK_DOT, TK_SQL, TK_EPS, END};
  CHECK(setIs(&ff.first[nt("option_single_constructed")], optSingle));
  int arith[] = {TK_OP, TK_ID, TK_NUM, TK_RNUM, TK_CHARLIT, END};
  CHECK(setIs(&ff.first[nt("arithmeticExpression")], arith));
}

TEST(test_follow_sets) {
  int program[] = {TK_DOLLAR, END};
  CHECK(setIs(&ff.follow[nt("program")], program));
  // arithmeticExpression and expPrime
  int arith[] = {TK_SEM, TK_CL, TK_SQR, TK_COMMA, END};
  CHECK(setIs(&ff.follow[nt("arithmeticExpression")], arith));
  CHECK(setIs(&ff.follow[nt("expPrime")], arith));
  int termPrime[] = {TK_PLUS, TK_MINUS, TK_SEM, TK_CL, TK_SQR, TK_COMMA, END};
  CHECK(setIs(&ff.follow[nt("termPrime")], termPrime));
  int otherStmts[] = {TK_RETURN, TK_ENDWHILE, TK_ENDIF, TK_ELSE, END};
  CHECK(setIs(&ff.follow[nt("otherStmts")], otherStmts));
  int decls[] = {TK_ID, TK_WHILE, TK_IF, TK_READ, TK_WRITE, TK_READCHAR,
                 TK_WRITECHAR, TK_PRINT, TK_EXIT, TK_SQL, TK_CALL,
                 TK_RETURN, END};
  CHECK(setIs(&ff.follow[nt("declarations")], decls));
  int singleOrRec[] = {TK_ASSIGNOP, TK_MUL, TK_DIV, TK_PLUS, TK_MINUS,
                       TK_SEM, TK_CL, TK_SQR, TK_COMMA, TK_LT, TK_LE,
                       TK_EQ, TK_GT, TK_GE, TK_NE, END};
  CHECK(setIs(&ff.follow[nt("singleOrRecId")], singleOrRec));
  int otherFunctions[] = {TK_MAIN, END};
  CHECK(setIs(&ff.follow[nt("otherFunctions")], otherFunctions));
  int moreIds[] = {TK_SQR, END};
  CHECK(setIs(&ff.follow[nt("more_ids")], moreIds));
  int remaining[] = {TK_SQR, END};
  CHECK(setIs(&ff.follow[nt("remaining_list")], remaining));
  int boolExpr[] = {TK_CL, END};
  CHECK(setIs(&ff.follow[nt("booleanExpression")], boolExpr));
}

// The grammar is LL(1): for every non-terminal the predict sets of its
// alternatives are pairwise disjoint.
TEST(test_grammar_is_ll1) {
  for (int a = 0; a < grammar->numNT; a++) {
    BitSet seen;
    bs_clear(&seen);
    for (int r = 0; r < grammar->numRules; r++) {
      GrammarRule *rule = &grammar->rules[r];
      if (rule->lhs != a)
        continue;
      BitSet predict;
      bs_clear(&predict);
      int nullable =
          firstOfString(grammar, &ff, rule->rhs, rule->rhsLen, &predict);
      if (nullable)
        bs_union(&predict, &ff.follow[a]);
      for (int t = 0; t < NUM_TOKENS; t++) {
        if (t == TK_EPS || !bs_contains(&predict, t))
          continue;
        if (bs_contains(&seen, t))
          printf("      conflict at <%s> on %s\n", grammar->ntNames[a],
                 tokenTypeToString((TokenType)t));
        CHECK(!bs_contains(&seen, t));
        bs_add(&seen, t);
      }
    }
  }
}

TEST(test_parse_table) {
  // <program> on TK_MAIN uses rule 0
  CHECK_EQ_INT(pt.table[nt("program")][TK_MAIN], 0);
  CHECK_EQ_INT(pt.table[nt("program")][TK_ID], -1);
  int r = pt.table[nt("stmt")][TK_SQL];
  CHECK(r >= 0);
  if (r >= 0)
    CHECK_EQ_INT(grammar->rules[r].rhs[0].id, nt("funCallStmt"));
  r = pt.table[nt("otherFunctions")][TK_MAIN]; // eps alternative
  CHECK(r >= 0);
  if (r >= 0)
    CHECK_EQ_INT(grammar->rules[r].rhs[0].id, TK_EPS);
  r = pt.table[nt("factor")][TK_OP];
  CHECK(r >= 0);
  if (r >= 0)
    CHECK_EQ_INT(grammar->rules[r].rhs[0].id, TK_OP);
  // every non-terminal has at least one entry
  for (int a = 0; a < grammar->numNT; a++) {
    int any = 0;
    for (int t = 0; t < NUM_TOKENS; t++)
      if (pt.table[a][t] != -1)
        any = 1;
    CHECK(any);
  }
}

TEST(test_stack) {
  Stack s;
  stackInit(&s);
  CHECK(stackEmpty(&s));
  Symbol a = {SYM_TERMINAL, TK_ID}, b = {SYM_NON_TERMINAL, 3};
  stackPush(&s, a, NULL);
  stackPush(&s, b, NULL);
  CHECK_EQ_INT(stackPeek(&s).sym.id, 3);
  CHECK_EQ_INT(stackPop(&s).sym.kind, SYM_NON_TERMINAL);
  CHECK_EQ_INT(stackPop(&s).sym.id, TK_ID);
  CHECK(stackEmpty(&s));
}

TEST(test_syntax_error_list) {
  SyntaxError *errs = NULL;
  addSyntaxError(&errs, 3, "first");
  addSyntaxError(&errs, 7, "second");
  CHECK(errs && errs->next && !errs->next->next);
  CHECK_EQ_INT(errs->next->lineNo, 7);
  CHECK_EQ_STR(errs->message, "first");
  freeSyntaxErrors(errs);
}

static int parseErrors(const char *text) {
  int lexErrors;
  TokenList tl = lex(text, &lexErrors);
  SyntaxError *errs = NULL;
  TreeNode *tree = parseTokens(&tl, grammar, &pt, &ff, &errs, 1);
  int n = 0;
  for (SyntaxError *e = errs; e; e = e->next)
    n++;
  freeSyntaxErrors(errs);
  freeParseTree(tree);
  free(tl.buf);
  return n;
}

TEST(test_parse_tokens) {
  CHECK_EQ_INT(parseErrors("_main return; end"), 0);
  CHECK_EQ_INT(parseErrors("_main type int : b2; b2 <--- 1; write(b2); "
                           "return; end"),
               0);
  CHECK(parseErrors("_main b2 <--- ; return; end") > 0);
  CHECK(parseErrors("_main return; end end") > 0); // junk after program
  CHECK(parseErrors("") > 0);                      // empty input
  CHECK(parseErrors("_main return end") > 0);      // missing ;
}

TEST(test_parse_tree_shape) {
  int lexErrors;
  TokenList tl = lex("_main return; end", &lexErrors);
  SyntaxError *errs = NULL;
  TreeNode *root = parseTokens(&tl, grammar, &pt, &ff, &errs, 1);
  CHECK(errs == NULL);
  CHECK_EQ_INT(root->sym.id, nt("program"));
  CHECK_EQ_INT(root->ruleIndex, 0);
  TreeNode *other = root->firstChild, *mainF = other->nextSibling;
  CHECK_EQ_INT(other->sym.id, nt("otherFunctions"));
  CHECK_EQ_INT(other->firstChild->sym.id, TK_EPS); // eps leaf
  CHECK_EQ_INT(mainF->sym.id, nt("mainFunction"));
  CHECK_EQ_STR(mainF->firstChild->lexeme, "_main");
  CHECK_EQ_INT(mainF->firstChild->lineNo, 1);
  freeParseTree(root);
  free(tl.buf);
}

/* -------------------------------------------------------------------- AST */

static const char *PRECEDENCE =
    "_main\n"
    "  type int : b2; type int : b3; type int : b4; type int : b5;\n"
    "  b2 <--- b3 + b4 * b5 - 7;\n"
    "  b2 <--- b3 / b4 / b5;\n"
    "  b2 <--- (b3 + b4) * b5;\n"
    "  return;\n"
    "end\n";

TEST(test_ast_precedence) {
  Compilation c;
  CHECK(analyse(PRECEDENCE, &c));
  Stmt *s = c.ast->functions->stmts;
  // b3 + b4 * b5 - 7  ==  (b3 + (b4 * b5)) - 7
  Expr *e = s->rhs;
  CHECK_EQ_INT(e->kind, EXPR_BINOP);
  CHECK_EQ_INT(e->op, TK_MINUS);
  CHECK_EQ_INT(e->right->kind, EXPR_NUM);
  CHECK_EQ_INT(e->right->ival, 7);
  CHECK_EQ_INT(e->left->op, TK_PLUS);
  CHECK_EQ_STR(e->left->left->var.name, "b3");
  CHECK_EQ_INT(e->left->right->op, TK_MUL);
  // b3 / b4 / b5  ==  (b3 / b4) / b5
  e = s->next->rhs;
  CHECK_EQ_INT(e->op, TK_DIV);
  CHECK_EQ_STR(e->right->var.name, "b5");
  CHECK_EQ_INT(e->left->op, TK_DIV);
  // parentheses win
  e = s->next->next->rhs;
  CHECK_EQ_INT(e->op, TK_MUL);
  CHECK_EQ_INT(e->left->op, TK_PLUS);
  freeCompilation(&c);
}

static const char *SHAPES =
    "_f input parameter list [int b3, real c3]\n"
    "output parameter list [int b4];\n"
    "  b4 <--- b3;\n"
    "  return [b4];\n"
    "end\n"
    "_main\n"
    "  record #pt type real : x; type real : y; endrecord\n"
    "  record #ln type #pt : p; type #pt : q; endrecord\n"
    "  definetype record #pt as #point\n"
    "  type record #ln : d2;\n"
    "  type int : b2 : global;\n"
    "  type real : c2;\n"
    "  d2.p.x <--- 1.50;\n"
    "  if ((b2 < 3) &&& (~(c2 >= 1.00))) then write(b2); else read(b2); "
    "write(c2); endif\n"
    "  while (b2 <= 10) b2 <--- b2 + 1; endwhile\n"
    "  [b2] <--- call _f with parameters [b2, c2];\n"
    "  return;\n"
    "end\n";

TEST(test_ast_shapes) {
  Compilation c;
  CHECK(analyse(SHAPES, &c));
  Function *f = c.ast->functions;
  CHECK_EQ_STR(f->name, "_f");
  CHECK(!f->isMain);
  CHECK_EQ_STR(f->inputs->name, "b3");
  CHECK_EQ_INT(f->inputs->type.kind, TREF_INT);
  CHECK_EQ_INT(f->inputs->next->type.kind, TREF_REAL);
  CHECK_EQ_STR(f->outputs->name, "b4");
  CHECK_EQ_INT(f->returns.count, 1);

  Function *m = f->next;
  CHECK(m->isMain && m->next == NULL);
  CHECK_EQ_STR(m->typeDefs->name, "#pt");
  CHECK_EQ_STR(m->typeDefs->fields->next->name, "y");
  CHECK_EQ_STR(m->aliases->alias, "#point");
  CHECK_EQ_INT(m->decls->type.keyword, TK_RECORD);
  CHECK(m->decls->next->isGlobal);
  CHECK(!m->decls->next->next->isGlobal);

  Stmt *s = m->stmts;
  CHECK_EQ_INT(s->kind, STMT_ASSIGN);
  CHECK_EQ_INT(s->lhs.numFields, 2);
  CHECK_EQ_STR(s->lhs.fields[1], "x");
  CHECK_EQ_INT(s->lhs.offset, 0);
  CHECK_EQ_INT(s->rhs->kind, EXPR_RNUM);

  s = s->next;
  CHECK_EQ_INT(s->kind, STMT_IF);
  CHECK_EQ_INT(s->cond->kind, BOOL_AND);
  CHECK_EQ_INT(s->cond->left->relop, TK_LT);
  CHECK_EQ_INT(s->cond->right->kind, BOOL_NOT);
  CHECK_EQ_INT(s->cond->right->left->relop, TK_GE);
  CHECK_EQ_INT(s->body->kind, STMT_WRITE);
  CHECK(s->body->next == NULL);
  CHECK_EQ_INT(s->elseBody->kind, STMT_READ);
  CHECK_EQ_INT(s->elseBody->next->kind, STMT_WRITE);

  s = s->next;
  CHECK_EQ_INT(s->kind, STMT_WHILE);
  CHECK_EQ_INT(s->body->kind, STMT_ASSIGN);

  s = s->next;
  CHECK_EQ_INT(s->kind, STMT_CALL);
  CHECK_EQ_STR(s->funName, "_f");
  CHECK_EQ_INT(s->outs.count, 1);
  CHECK_EQ_INT(s->ins.count, 2);
  CHECK_EQ_STR(s->ins.ids[1].name, "c2");
  CHECK(s->callee != NULL);
  freeCompilation(&c);
}

/* ---------------------------------------------------------- symbol table */

static const char *LAYOUT =
    "_f input parameter list [record #ln b2, int b3]\n"
    "output parameter list [real c2, record #pt c3];\n"
    "  type int : b4;\n"
    "  type record #ln : b5;\n"
    "  c2 <--- 1.00; c3 <--- b2.p;\n"
    "  return [c2, c3];\n"
    "end\n"
    "_main\n"
    "  record #pt type real : x; type real : y; endrecord\n"
    "  record #ln type #pt : p; type #pt : q; endrecord\n"
    "  union #u type #ln : l; type int : i; type #pt : p; endunion\n"
    "  record #v type int : tag; type #u : val; endrecord\n"
    "  definetype record #pt as #point\n"
    "  definetype record #point as #dot\n"
    "  type #v : d2;\n"
    "  type int : b7 : global;\n"
    "  type real : c4 : global;\n"
    "  type int : b6;\n"
    "  return;\n"
    "end\n";

TEST(test_type_layout) {
  Compilation c;
  CHECK(analyse(LAYOUT, &c));
  SymbolTable *st = c.symbols;
  Type *pt_ = findType(st, "#pt"), *ln = findType(st, "#ln");
  Type *u = findType(st, "#u"), *v = findType(st, "#v");
  CHECK(pt_ && ln && u && v);
  CHECK_EQ_INT(pt_->size, 16);
  CHECK_EQ_INT(findField(pt_, "y")->offset, 8);
  CHECK_EQ_INT(ln->size, 32);
  CHECK_EQ_INT(findField(ln, "q")->offset, 16);
  CHECK_EQ_INT(u->kind, TY_UNION);
  CHECK_EQ_INT(u->size, 32); // largest member
  CHECK_EQ_INT(findField(u, "i")->offset, 0);
  CHECK_EQ_INT(findField(u, "p")->offset, 0);
  CHECK(u->hasUnion && v->hasUnion && !ln->hasUnion);
  CHECK_EQ_INT(v->size, 40);
  CHECK_EQ_INT(findField(v, "val")->offset, 8);
  // aliases (even chained ones) are the very same type
  CHECK(findType(st, "#point") == pt_);
  CHECK(findType(st, "#dot") == pt_);
  CHECK(findType(st, "#nothing") == NULL);
  freeCompilation(&c);
}

TEST(test_variable_layout) {
  Compilation c;
  CHECK(analyse(LAYOUT, &c));
  SymbolTable *st = c.symbols;
  FuncEntry *f = findFunc(st, "_f"), *m = findFunc(st, "_main");
  CHECK(f && m);
  CHECK_EQ_INT(f->index, 0);
  CHECK_EQ_INT(m->index, 1);
  CHECK(m->isMain);
  CHECK_EQ_INT(f->numInputs, 2);
  CHECK_EQ_INT(f->numOutputs, 2);
  // inputs first, then outputs, packed in the caller's block
  CHECK_EQ_INT(f->inputs[0]->offset, 0);  // #ln, 32 bytes
  CHECK_EQ_INT(f->inputs[1]->offset, 32); // int
  CHECK_EQ_INT(f->inSize, 40);
  CHECK_EQ_INT(f->outputs[0]->offset, 40); // real
  CHECK_EQ_INT(f->outputs[1]->offset, 48); // #pt, 16 bytes
  CHECK_EQ_INT(f->paramSize, 64);          // 64 rounded to 16
  CHECK_EQ_INT(f->inputs[0]->kind, VAR_INPUT);
  CHECK_EQ_INT(f->outputs[1]->kind, VAR_OUTPUT);
  // locals grow down from rbp; frame rounded to 16
  VarEntry *b4 = findLocal(f, "b4"), *b5 = findLocal(f, "b5");
  CHECK(b4 && b5);
  CHECK_EQ_INT(b4->offset, 8);
  CHECK_EQ_INT(b5->offset, 40);
  CHECK_EQ_INT(f->localSize, 48);
  CHECK_EQ_INT(b4->kind, VAR_LOCAL);
  // globals are shared and found from any function
  VarEntry *g = findGlobal(st, "b7");
  CHECK(g && g->kind == VAR_GLOBAL && g->owner == NULL);
  CHECK(lookupVar(st, f, "c4") == findGlobal(st, "c4"));
  CHECK(findLocal(m, "b7") == NULL);
  CHECK(lookupVar(st, m, "b6") != NULL);
  CHECK_EQ_INT(m->localSize, 48); // #v (40) + int (8)
  CHECK_EQ_INT(m->paramSize, 0);
  // the returned record field b2.p is resolved with its offset
  Stmt *s = f->ast->stmts->next;
  CHECK_EQ_INT(s->rhs->var.offset, 0);
  CHECK(s->rhs->var.type == findType(st, "#pt"));
  freeCompilation(&c);
}

TEST(test_semantic_annotations) {
  Compilation c;
  CHECK(analyse("_main type int : b2; type real : c2;\n"
                "c2 <--- b2 + 1.50; b2 <--- b2 * 2; return; end",
                &c));
  Stmt *s = c.ast->functions->stmts;
  CHECK_EQ_INT(s->rhs->type->kind, TY_REAL);
  CHECK_EQ_INT(s->rhs->left->type->kind, TY_INT);
  CHECK_EQ_INT(s->next->rhs->type->kind, TY_INT);
  CHECK(s->lhs.entry == findLocal(c.symbols->funcs[0], "c2"));
  freeCompilation(&c);
}

TEST(test_semantic_error_counts) {
  Compilation c;
  CHECK(!analyse("_main b2 <--- 1; b2 <--- 2; return; end", &c));
  CHECK_EQ_INT(c.semanticErrors, 1); // one report per undeclared name
  freeCompilation(&c);
  CHECK(!analyse("_main type int : b2; b2 <--- 1.50; return; end", &c));
  CHECK_EQ_INT(c.semanticErrors, 1);
  freeCompilation(&c);
  // syntax errors stop the pipeline before semantic analysis
  CHECK(!analyse("_main b2 <--- ; return; end", &c));
  CHECK(c.syntaxErrors > 0);
  CHECK(c.ast == NULL);
  freeCompilation(&c);
}

static const char *ARRAYS =
    "_rec input parameter list [int b2]\n"
    "output parameter list [int b3];\n"
    "  type int[4] : c2;\n"
    "  [b3] <--- call _rec with parameters [b2];\n"
    "  return [b3];\n"
    "end\n"
    "_leaf input parameter list [int b2];\n"
    "  return;\n"
    "end\n"
    "_main\n"
    "  type int[10] : b2;\n"
    "  type int[10] : d2 : global;\n"
    "  type real[010] : c2;\n"
    "  type int : b3;\n"
    "  c2[b3 + 1] <--- b2[0] * 0 + 1;\n"
    "  call _leaf with parameters [b3];\n"
    "  [b3] <--- call _rec with parameters [b3];\n"
    "  return;\n"
    "end\n";

TEST(test_array_types) {
  Compilation c;
  CHECK(analyse(ARRAYS, &c));
  SymbolTable *st = c.symbols;
  FuncEntry *m = findFunc(st, "_main");
  VarEntry *b2 = findLocal(m, "b2"), *d2 = findGlobal(st, "d2");
  VarEntry *c2 = findLocal(m, "c2");
  CHECK(b2 && d2 && c2);
  // interned per (element type, length): both int[10] share one Type
  CHECK(b2->type == d2->type);
  CHECK(b2->type != c2->type);
  CHECK_EQ_INT(b2->type->kind, TY_ARRAY);
  CHECK_EQ_STR(b2->type->name, "int[10]");
  CHECK_EQ_STR(c2->type->name, "real[10]"); // leading zero in the length
  CHECK(b2->type->elem == st->intType);
  CHECK_EQ_INT(b2->type->size, 80);
  CHECK(!isScalar(b2->type) && !isAggregate(b2->type));
  CHECK_EQ_INT(b2->offset, 80); // laid out like a record
  CHECK_EQ_INT(m->localSize, 176);
  // the element access has the element type and keeps its index
  Stmt *s = m->ast->stmts;
  CHECK(s->lhs.index != NULL);
  CHECK(s->lhs.type == st->realType);
  CHECK_EQ_INT(s->lhs.index->kind, EXPR_BINOP);
  CHECK(s->rhs->left->left->var.type == st->intType);
  freeCompilation(&c);
}

TEST(test_stack_need) {
  Compilation c;
  CHECK(analyse(ARRAYS, &c));
  // need = 16 + locals + max(temporaries, largest parameter block)
  FuncEntry *rec = findFunc(c.symbols, "_rec");
  FuncEntry *leaf = findFunc(c.symbols, "_leaf");
  CHECK_EQ_INT(rec->stackNeed, 16 + 32 + 8 * (MAX_EXPR_DEPTH + 2));
  CHECK_EQ_INT(leaf->stackNeed, 16 + 0 + 8 * (MAX_EXPR_DEPTH + 2));
  freeCompilation(&c);
}

TEST(test_grammar_matches) {
  CHECK(grammarMatches(grammar));
  // a grammar of another shape (e.g. an older grammar.txt) is refused
  Grammar *other = loadGrammar(writeSource("<program> ::= TK_MAIN\n"));
  remove(tmpPath);
  CHECK(other != NULL);
  if (other)
    CHECK(!grammarMatches(other));
  freeGrammar(other);
}

TEST(test_stack_recursive_callee) {
  // one activation of a recursive callee counts in its caller: 4 MiB of
  // locals plus a recursive function with 2 MiB of locals is too much
  Compilation c;
  CHECK(!analyse("_rec input parameter list [int b2];\n"
                 "  type int[131072] : c2; type int[131072] : c3;\n"
                 "  call _rec with parameters [b2];\n"
                 "  return;\n"
                 "end\n"
                 "_main\n"
                 "  type int : d2 : global;\n"
                 "  type int[131072] : c2; type int[131072] : c3;\n"
                 "  type int[131072] : c4; type int[131072] : c5;\n"
                 "  call _rec with parameters [d2];\n"
                 "  return;\n"
                 "end\n",
                 &c));
  CHECK_EQ_INT(c.semanticErrors, 1);
  FuncEntry *rec = findFunc(c.symbols, "_rec");
  CHECK(rec != NULL);
  if (rec)
    CHECK_EQ_INT(rec->stackNeed, 16 + 2097152 + 8 * (MAX_EXPR_DEPTH + 2));
  freeCompilation(&c);
}

int main(void) {
  grammar = loadGrammar("grammar.txt");
  if (!grammar) {
    printf("run from the repository root (grammar.txt not found)\n");
    return 1;
  }
  computeFirstAndFollow(grammar, &ff);
  createParseTable(grammar, &ff, &pt);

  RUN(test_bitset);
  RUN(test_keywords);
  RUN(test_scan_token_stream);
  RUN(test_scan_grows_buffer);
  RUN(test_scan_missing_file);
  RUN(test_char_literal_value);
  RUN(test_string_literal_table);
  RUN(test_new_keywords);
  RUN(test_terminal_names);
  RUN(test_grammar_loaded);
  RUN(test_first_sets);
  RUN(test_follow_sets);
  RUN(test_grammar_is_ll1);
  RUN(test_parse_table);
  RUN(test_stack);
  RUN(test_syntax_error_list);
  RUN(test_parse_tokens);
  RUN(test_parse_tree_shape);
  RUN(test_ast_precedence);
  RUN(test_ast_shapes);
  RUN(test_type_layout);
  RUN(test_variable_layout);
  RUN(test_semantic_annotations);
  RUN(test_semantic_error_counts);
  RUN(test_array_types);
  RUN(test_stack_need);
  RUN(test_grammar_matches);
  RUN(test_stack_recursive_callee);

  freeGrammar(grammar);
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
