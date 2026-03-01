/* ================================================================
 *  parser.c — Table-driven LL(1) Predictive Parser
 *  Reads grammar from file, computes FIRST/FOLLOW, builds parse
 *  table, then parses a token stream in a single pass producing
 *  a parse tree (or syntax error list).
 * ================================================================ */

#include "parser.h"
#include "logging.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CUR_TOKEN()                                                            \
  (tokenIdx < tokens->size ? tokens->buf[tokenIdx].type : TK_DOLLAR)
#define CUR_LINE()                                                             \
  (tokenIdx < tokens->size ? (int)tokens->buf[tokenIdx].lineNo : -1)
#define CUR_LEXEME()                                                           \
  (tokenIdx < tokens->size ? tokens->buf[tokenIdx].lexeme : "$")

#define SKIP_NOISE()                                                           \
  do {                                                                         \
    while (tokenIdx < tokens->size &&                                          \
           (CUR_TOKEN() == TK_COMMENT || CUR_TOKEN() == TK_ERROR)) {           \
      if (CUR_TOKEN() == TK_ERROR) {                                           \
        addSyntaxErrorNoDup(errors, &lastErrLine, CUR_LINE(),                  \
                            "Unknown pattern <%s>", CUR_LEXEME());             \
      }                                                                        \
      tokenIdx++;                                                              \
    }                                                                          \
  } while (0)
/* ────────────────────────────────────────────────────────────────
 *  BitSet helpers
 * ──────────────────────────────────────────────────────────────── */

static void bs_clear(BitSet *s) {
  for (int i = 0; i < BITSET_WORDS; i++)
    s->bits[i] = 0;
}

static void bs_add(BitSet *s, int bit) {
  s->bits[bit / 64] |= (1ULL << (bit % 64));
}

static int bs_contains(const BitSet *s, int bit) {
  return (s->bits[bit / 64] >> (bit % 64)) & 1;
}

static int bs_union(BitSet *dst, const BitSet *src) {
  /* returns 1 if dst changed */
  int changed = 0;
  for (int i = 0; i < BITSET_WORDS; i++) {
    unsigned long long old = dst->bits[i];
    dst->bits[i] |= src->bits[i];
    if (dst->bits[i] != old)
      changed = 1;
  }
  return changed;
}

/* union of src into dst, excluding TK_EPS */
static int bs_union_no_eps(BitSet *dst, const BitSet *src) {
  BitSet tmp;
  for (int i = 0; i < BITSET_WORDS; i++)
    tmp.bits[i] = src->bits[i];
  /* remove eps */
  tmp.bits[TK_EPS / 64] &= ~(1ULL << (TK_EPS % 64));
  return bs_union(dst, &tmp);
}

static int bs_is_empty(const BitSet *s) {
  for (int i = 0; i < BITSET_WORDS; i++)
    if (s->bits[i])
      return 0;
  return 1;
}

/* ────────────────────────────────────────────────────────────────
 *  Terminal name → TokenType mapping (reverse of tokenTypeToString)
 * ──────────────────────────────────────────────────────────────── */

static TokenType terminalFromString(const char *name) {
  if (strcmp(name, "eps") == 0)
    return TK_EPS;
  /* brute-force compare against all token names */
  for (int i = 0; i < NUM_TOKENS; i++) {
    if (strcmp(tokenTypeToString((TokenType)i), name) == 0)
      return (TokenType)i;
  }
  return TK_ERROR; /* should not happen for valid grammar */
}

/* ────────────────────────────────────────────────────────────────
 *  Grammar loading from grammer.txt
 *
 *  Format:  <nonTerminal> ::= sym1 sym2 ... | sym3 sym4 ...
 *  Continuation lines start with whitespace or '|'
 * ──────────────────────────────────────────────────────────────── */

static int findOrAddNT(Grammar *g, const char *name) {
  for (int i = 0; i < g->numNT; i++) {
    if (strcmp(g->ntNames[i], name) == 0)
      return i;
  }
  /* add */
  int idx = g->numNT++;
  strncpy(g->ntNames[idx], name, MAX_SYMBOL_NAME - 1);
  g->ntNames[idx][MAX_SYMBOL_NAME - 1] = '\0';
  return idx;
}

static Symbol makeSymbol(Grammar *g, const char *tok) {
  Symbol s;
  if (tok[0] == '<') {
    /* non-terminal: strip angle brackets */
    char name[MAX_SYMBOL_NAME];
    int len = (int)strlen(tok);
    /* copy without < and > */
    int j = 0;
    for (int i = 1; i < len - 1 && j < MAX_SYMBOL_NAME - 1; i++)
      name[j++] = tok[i];
    name[j] = '\0';
    s.kind = SYM_NON_TERMINAL;
    s.id = findOrAddNT(g, name);
  } else {
    s.kind = SYM_TERMINAL;
    s.id = (int)terminalFromString(tok);
  }
  return s;
}

/* Add a single rule (one alternative) to the grammar */
static void addRule(Grammar *g, int lhsNT, Symbol *rhs, int rhsLen) {
  if (g->numRules >= MAX_RULES) {
    fprintf(stderr, "Too many grammar rules (max %d)\n", MAX_RULES);
    return;
  }
  GrammarRule *r = &g->rules[g->numRules];
  r->lhs = lhsNT;
  r->rhsLen = rhsLen;
  for (int i = 0; i < rhsLen; i++)
    r->rhs[i] = rhs[i];
  g->numRules++;
}

Grammar *loadGrammar(const char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    perror("Cannot open grammar file");
    return NULL;
  }

  Grammar *g = (Grammar *)calloc(1, sizeof(Grammar));
  char line[MAX_LINE_LEN];
  int currentLHS = -1;

  while (fgets(line, sizeof(line), fp)) {
    /* strip trailing newline */
    int len = (int)strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';

    /* skip blank lines */
    if (len == 0)
      continue;

    /* Determine if this is a new rule or a continuation */
    char *pos = line;
    int isContinuation = 0;

    /* A continuation line starts with whitespace or '|' only
       (no '<' at the start after trimming) */
    if (line[0] == ' ' || line[0] == '\t') {
      /* check if it has ::= => new rule; otherwise continuation */
      if (strstr(line, "::=") == NULL) {
        isContinuation = 1;
      }
    }

    if (!isContinuation) {
      /* New rule: parse LHS */
      /* find <...> */
      char *open = strchr(pos, '<');
      char *close = strchr(pos, '>');
      if (!open || !close)
        continue;

      char ntName[MAX_SYMBOL_NAME];
      int j = 0;
      for (char *p = open + 1; p < close && j < MAX_SYMBOL_NAME - 1; p++)
        ntName[j++] = *p;
      ntName[j] = '\0';
      currentLHS = findOrAddNT(g, ntName);

      /* skip past ::= */
      pos = strstr(pos, "::=");
      if (!pos)
        continue;
      pos += 3;
    } else {
      /* continuation: skip leading whitespace */
      while (*pos == ' ' || *pos == '\t')
        pos++;
      /* skip leading | */
      if (*pos == '|')
        pos++;
    }

    /* Now parse RHS alternatives separated by | */
    /* We need to be careful: first split by top-level '|' */
    /* tokenize manually */
    Symbol rhsBuf[MAX_RHS];
    int rhsLen = 0;

    char *saveptr = NULL;
    /* We can't simply strtok by '|' because that would split within tokens.
       Instead, tokenize by whitespace and handle '|' as separator. */
    char linecopy[MAX_LINE_LEN];
    strncpy(linecopy, pos, MAX_LINE_LEN - 1);
    linecopy[MAX_LINE_LEN - 1] = '\0';

    char *token = strtok_r(linecopy, " \t", &saveptr);
    while (token) {
      if (strcmp(token, "|") == 0) {
        /* flush current alternative */
        if (rhsLen > 0 || currentLHS >= 0) {
          addRule(g, currentLHS, rhsBuf, rhsLen);
          rhsLen = 0;
        }
      } else {
        rhsBuf[rhsLen++] = makeSymbol(g, token);
      }
      token = strtok_r(NULL, " \t", &saveptr);
    }
    /* flush last alternative */
    if (rhsLen > 0 && currentLHS >= 0) {
      addRule(g, currentLHS, rhsBuf, rhsLen);
    }
  }
  fclose(fp);
  return g;
}

void freeGrammar(Grammar *g) {
  if (g)
    free(g);
}

const char *getNTName(Grammar *g, int ntIndex) {
  if (ntIndex >= 0 && ntIndex < g->numNT)
    return g->ntNames[ntIndex];
  return "?";
}

/* ────────────────────────────────────────────────────────────────
 *  FIRST set computation
 * ──────────────────────────────────────────────────────────────── */

/* Compute FIRST of a string of symbols and store into dst.
   Returns 1 if eps is derivable from the entire string. */
static int firstOfString(Grammar *g, FirstFollowSets *ff, Symbol *syms,
                         int count, BitSet *dst) {
  for (int i = 0; i < count; i++) {
    if (syms[i].kind == SYM_TERMINAL) {
      bs_add(dst, syms[i].id);
      if (syms[i].id == (int)TK_EPS) {
        /* eps itself – continue to see if rest matters */
        continue;
      }
      return 0; /* terminal stops the chain */
    } else {
      /* non-terminal */
      bs_union_no_eps(dst, &ff->first[syms[i].id]);
      if (!bs_contains(&ff->first[syms[i].id], TK_EPS))
        return 0; /* cannot derive eps, stop */
                  /* else continue */
    }
  }
  /* entire string can derive eps */
  bs_add(dst, TK_EPS);
  return 1;
}

void computeFirstAndFollow(Grammar *g, FirstFollowSets *ff) {
  /* initialise */
  for (int i = 0; i < g->numNT; i++) {
    bs_clear(&ff->first[i]);
    bs_clear(&ff->follow[i]);
  }

  /* ── FIRST ── iterative fixed-point */
  int changed = 1;
  while (changed) {
    changed = 0;
    for (int r = 0; r < g->numRules; r++) {
      GrammarRule *rule = &g->rules[r];
      BitSet contrib;
      bs_clear(&contrib);
      firstOfString(g, ff, rule->rhs, rule->rhsLen, &contrib);
      if (bs_union(&ff->first[rule->lhs], &contrib))
        changed = 1;
    }
  }

  /* ── FOLLOW ── */
  /* Rule 1: $ ∈ FOLLOW(start symbol) */
  bs_add(&ff->follow[0], TK_DOLLAR);

  changed = 1;
  while (changed) {
    changed = 0;
    for (int r = 0; r < g->numRules; r++) {
      GrammarRule *rule = &g->rules[r];
      int A = rule->lhs;
      for (int i = 0; i < rule->rhsLen; i++) {
        if (rule->rhs[i].kind != SYM_NON_TERMINAL)
          continue;
        int B = rule->rhs[i].id;

        /* β = rhs[i+1 .. rhsLen-1] */
        BitSet firstBeta;
        bs_clear(&firstBeta);
        int betaDerivesEps = firstOfString(g, ff, &rule->rhs[i + 1],
                                           rule->rhsLen - i - 1, &firstBeta);

        /* add FIRST(β) \ {eps} to FOLLOW(B) */
        if (bs_union_no_eps(&ff->follow[B], &firstBeta))
          changed = 1;

        /* if β ⇒* eps, add FOLLOW(A) to FOLLOW(B) */
        if (betaDerivesEps) {
          if (bs_union(&ff->follow[B], &ff->follow[A]))
            changed = 1;
        }
      }
    }
  }
}

/* ────────────────────────────────────────────────────────────────
 *  Parse table construction
 * ──────────────────────────────────────────────────────────────── */

void createParseTable(Grammar *g, FirstFollowSets *ff, ParseTable *pt) {
  /* initialise to -1 (error) */
  for (int i = 0; i < MAX_NON_TERMINALS; i++)
    for (int j = 0; j < NUM_TOKENS; j++)
      pt->table[i][j] = -1;

  for (int r = 0; r < g->numRules; r++) {
    GrammarRule *rule = &g->rules[r];
    int A = rule->lhs;

    BitSet firstAlpha;
    bs_clear(&firstAlpha);
    int alphaDerivesEps =
        firstOfString(g, ff, rule->rhs, rule->rhsLen, &firstAlpha);

    /* For each terminal a in FIRST(α), table[A][a] = r */
    for (int t = 0; t < NUM_TOKENS; t++) {
      if (t == (int)TK_EPS)
        continue;
      if (bs_contains(&firstAlpha, t)) {
        if (pt->table[A][t] != -1 && pt->table[A][t] != r) {
          fprintf(stderr,
                  "[PARSER] LL(1) conflict at <%s>, %s: rule %d vs %d\n",
                  g->ntNames[A], tokenTypeToString((TokenType)t),
                  pt->table[A][t], r);
        }
        pt->table[A][t] = r;
      }
    }

    /* If eps ∈ FIRST(α), for each b in FOLLOW(A), table[A][b] = r */
    if (alphaDerivesEps) {
      for (int t = 0; t < NUM_TOKENS; t++) {
        if (t == (int)TK_EPS)
          continue;
        if (bs_contains(&ff->follow[A], t)) {
          if (pt->table[A][t] != -1 && pt->table[A][t] != r) {
            fprintf(stderr,
                    "[PARSER] LL(1) conflict at <%s>, %s: rule %d vs %d\n",
                    g->ntNames[A], tokenTypeToString((TokenType)t),
                    pt->table[A][t], r);
          }
          pt->table[A][t] = r;
        }
      }
    }
  }
}

/* ────────────────────────────────────────────────────────────────
 *  Parse tree node helpers
 * ──────────────────────────────────────────────────────────────── */

static TreeNode *newTreeNode(Symbol sym) {
  TreeNode *n = (TreeNode *)calloc(1, sizeof(TreeNode));
  n->sym = sym;
  n->ruleIndex = -1;
  n->lineNo = -1;
  n->lexeme[0] = '\0';
  return n;
}

/* Add child as the last child of parent */
static void addChild(TreeNode *parent, TreeNode *child) {
  if (!parent->firstChild) {
    parent->firstChild = child;
  } else {
    TreeNode *c = parent->firstChild;
    while (c->nextSibling)
      c = c->nextSibling;
    c->nextSibling = child;
  }
}

/* ────────────────────────────────────────────────────────────────
 *  Error reporting helpers
 * ──────────────────────────────────────────────────────────────── */

static void addSyntaxError(SyntaxError **head, int lineNo, const char *fmt,
                           ...) {
  SyntaxError *err = (SyntaxError *)calloc(1, sizeof(SyntaxError));
  err->lineNo = lineNo;

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err->message, sizeof(err->message), fmt, ap);
  va_end(ap);

  /* append to end */
  if (*head == NULL) {
    *head = err;
  } else {
    SyntaxError *p = *head;
    while (p->next)
      p = p->next;
    p->next = err;
  }
}

void printSyntaxErrors(SyntaxError *errors) {
  if (!errors)
    return;
  printf("\n======== SYNTAX ERRORS ========\n");
  int count = 0;
  for (SyntaxError *e = errors; e; e = e->next) {
    count++;
    printf("  Line %d: %s\n", e->lineNo, e->message);
  }
  printf("  Total: %d error(s)\n", count);
  printf("===============================\n");
}

void freeSyntaxErrors(SyntaxError *errors) {
  while (errors) {
    SyntaxError *tmp = errors;
    errors = errors->next;
    free(tmp);
  }
}

/* ────────────────────────────────────────────────────────────────
 *  Stack-based LL(1) parser with panic-mode error recovery
 * ──────────────────────────────────────────────────────────────── */

/* Simple stack of (Symbol, TreeNode*) pairs */
#define STACK_CAP 4096
typedef struct {
  Symbol sym;
  TreeNode *node;
} StackEntry;

typedef struct {
  StackEntry data[STACK_CAP];
  int top; /* index of top element, -1 if empty */
} Stack;

static void stackInit(Stack *s) { s->top = -1; }
static int stackEmpty(Stack *s) { return s->top < 0; }
static void stackPush(Stack *s, Symbol sym, TreeNode *node) {
  if (s->top >= STACK_CAP - 1) {
    fprintf(stderr, "[PARSER] Stack overflow!\n");
    return;
  }
  s->top++;
  s->data[s->top].sym = sym;
  s->data[s->top].node = node;
}
static StackEntry stackPop(Stack *s) { return s->data[s->top--]; }
static StackEntry stackPeek(Stack *s) { return s->data[s->top]; }

/* ── Synchronization set: tokens that anchor error recovery ── */
static int isSyncToken(int tok) {
  switch ((TokenType)tok) {
  case TK_SEM:
  case TK_ENDRECORD:
  case TK_ENDUNION:
  case TK_ENDIF:
  case TK_ENDWHILE:
  case TK_ELSE:
  case TK_CL:
  case TK_SQR:
  case TK_END:
  case TK_DOLLAR:
    return 1;
  default:
    return 0;
  }
}

/* ── Check if a token is in the FOLLOW set or sync set for a NT ── */
static int isInSyncOrFollow(FirstFollowSets *ff, int nt, int tok) {
  if (isSyncToken(tok))
    return 1;
  if (bs_contains(&ff->follow[nt], tok))
    return 1;
  return 0;
}

/* ── Helper: add error, suppressing exact duplicates (same line + same msg) ──
 */
static void addSyntaxErrorNoDup(SyntaxError **head, int *lastErrLine,
                                int lineNo, const char *fmt, ...) {
  /* Build the message first */
  char msgBuf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msgBuf, sizeof(msgBuf), fmt, ap);
  va_end(ap);

  /* Check for exact duplicate (same line AND same message) */
  for (SyntaxError *p = *head; p; p = p->next) {
    if (p->lineNo == lineNo && strcmp(p->message, msgBuf) == 0)
      return; /* exact dup, skip */
  }

  /* Suppress cascading: if we already reported on this line from the parser
     and the new error is also from the parser (not a lexer "Unknown" error),
     skip to avoid flooding. But allow lexer errors to coexist. */
  int isLexerError = (strncmp(msgBuf, "Unknown", 7) == 0);
  if (!isLexerError && *lastErrLine == lineNo)
    return;

  if (!isLexerError)
    *lastErrLine = lineNo;

  SyntaxError *err = (SyntaxError *)calloc(1, sizeof(SyntaxError));
  err->lineNo = lineNo;
  strncpy(err->message, msgBuf, sizeof(err->message) - 1);

  /* append to end */
  if (*head == NULL) {
    *head = err;
  } else {
    SyntaxError *p = *head;
    while (p->next)
      p = p->next;
    p->next = err;
  }
}

TreeNode *parseTokens(TokenList *tokens, Grammar *g, ParseTable *pt,
                      FirstFollowSets *ff, SyntaxError **errors) {
  Stack stack;
  stackInit(&stack);

  /* Create root node for start symbol */
  Symbol startSym = {SYM_NON_TERMINAL, 0};
  TreeNode *root = newTreeNode(startSym);

  /* Push $ then start symbol */
  Symbol dollarSym = {SYM_TERMINAL, TK_DOLLAR};
  stackPush(&stack, dollarSym, NULL);
  stackPush(&stack, startSym, root);

  int tokenIdx = 0;
  int lastErrLine = -1; /* for duplicate suppression */

  /* Current token helpers */

  /* Advance past comments and lexer-error tokens */

  /* Skip initial noise */
  SKIP_NOISE();

  while (!stackEmpty(&stack)) {
    StackEntry top = stackPeek(&stack);
    TokenType curTok = CUR_TOKEN();

    /* ── Terminal on stack top ── */
    if (top.sym.kind == SYM_TERMINAL) {

      if (top.sym.id == (int)TK_EPS) {
        /* epsilon: just pop, mark node */
        stackPop(&stack);
        if (top.node) {
          top.node->lineNo = CUR_LINE();
          strncpy(top.node->lexeme, "----", 30);
        }
        continue;
      }

      if (top.sym.id == (int)TK_DOLLAR) {
        if (curTok == TK_DOLLAR) {
          stackPop(&stack);
          break;
        } else {
          addSyntaxErrorNoDup(errors, &lastErrLine, CUR_LINE(),
                              "Unexpected token '%s' (%s) after end of program",
                              CUR_LEXEME(), tokenTypeToString(curTok));
          break;
        }
      }

      /* Terminal match */
      if (top.sym.id == (int)curTok) {
        stackPop(&stack);
        if (top.node) {
          top.node->lineNo = CUR_LINE();
          strncpy(top.node->lexeme, CUR_LEXEME(), 30);
          top.node->lexeme[30] = '\0';
        }
        tokenIdx++;
        SKIP_NOISE();
      } else {
        /* ── Terminal mismatch error ── */
        addSyntaxErrorNoDup(
            errors, &lastErrLine, CUR_LINE(),
            "The token %s for lexeme %s does not match with the expected "
            "token %s",
            tokenTypeToString(curTok), CUR_LEXEME(),
            tokenTypeToString((TokenType)top.sym.id));
        /* Pop the expected terminal from the stack (don't consume input) */
        stackPop(&stack);
      }
    } else {
      /* ── Non-terminal on stack top ── */
      int nt = top.sym.id;
      int ruleIdx = pt->table[nt][(int)curTok];

      if (ruleIdx == -1) {
        /* No entry in parse table for (NT, curTok) */
        addSyntaxErrorNoDup(
            errors, &lastErrLine, CUR_LINE(),
            "Invalid token %s encountered with value %s stack top %s",
            tokenTypeToString(curTok), CUR_LEXEME(), g->ntNames[nt]);

        /* ── Panic-mode error recovery with sync set ──
           Strategy: if the current token is in FOLLOW(NT) or sync set,
           pop the NT (it produced epsilon effectively).
           Otherwise, skip the current token and retry. */
        if (isInSyncOrFollow(ff, nt, (int)curTok)) {
          /* The current token belongs to a higher-level construct.
             Pop this NT so parsing can continue with the parent. */
          stackPop(&stack);
        } else {
          /* Skip the offending token and try again with the same NT. */
          if (curTok == TK_DOLLAR) {
            stackPop(&stack); /* can't skip $, just abandon NT */
          } else {
            tokenIdx++;
            SKIP_NOISE();
          }
        }
        continue;
      }

      /* Pop the non-terminal and expand the rule */
      stackPop(&stack);

      GrammarRule *rule = &g->rules[ruleIdx];
      TreeNode *parentNode = top.node;
      if (parentNode)
        parentNode->ruleIndex = ruleIdx;

      /* Create child nodes and push in reverse order */
      TreeNode *childNodes[MAX_RHS];
      for (int i = 0; i < rule->rhsLen; i++) {
        childNodes[i] = newTreeNode(rule->rhs[i]);
        addChild(parentNode, childNodes[i]);
      }

      /* push in reverse so leftmost symbol is on top */
      for (int i = rule->rhsLen - 1; i >= 0; i--) {
        stackPush(&stack, rule->rhs[i], childNodes[i]);
      }
    }
  }

  return root;
}

/* ────────────────────────────────────────────────────────────────
 *  Parse tree printing — Inorder traversal
 *
 *  Format per line:
 *  lexeme  lineno  tokenName  valueIfNumber  parentNodeSymbol  isLeafNode
 * NodeSymbol
 *
 *  Inorder for multi-child tree:
 *    visit first child → print current node → visit remaining children
 * ──────────────────────────────────────────────────────────────── */

/* Print a single node's row */
static void printNodeRow(Grammar *g, TreeNode *node, const char *parentSymbol,
                         FILE *out) {
  int isLeaf = (node->firstChild == NULL);
  const char *lexeme;
  const char *tokenName;
  const char *valueIfNumber = "----";
  const char *nodeSymbol = "----";
  char valueBuf[32];

  if (isLeaf) {
    /* Leaf node: print actual lexeme */
    if (node->sym.kind == SYM_TERMINAL && node->sym.id == (int)TK_EPS) {
      lexeme = "----";
      tokenName = "TK_EPS";
    } else if (node->lexeme[0] && strcmp(node->lexeme, "----") != 0) {
      lexeme = node->lexeme;
      tokenName = tokenTypeToString((TokenType)node->sym.id);
    } else {
      lexeme = "----";
      tokenName = tokenTypeToString((TokenType)node->sym.id);
    }

    /* If the token is a number, print its value */
    if (node->sym.id == (int)TK_NUM || node->sym.id == (int)TK_RNUM) {
      strncpy(valueBuf, node->lexeme, sizeof(valueBuf) - 1);
      valueBuf[sizeof(valueBuf) - 1] = '\0';
      valueIfNumber = valueBuf;
    }
  } else {
    /* Non-leaf (internal) node */
    lexeme = "----";
    tokenName = "----";
    /* NodeSymbol = the non-terminal name */
    if (node->sym.kind == SYM_NON_TERMINAL)
      nodeSymbol = g->ntNames[node->sym.id];
  }

  fprintf(out, "%-25s %-6d %-18s %-15s %-25s %-5s %s\n", lexeme, node->lineNo,
          tokenName, valueIfNumber, parentSymbol, isLeaf ? "yes" : "no",
          nodeSymbol);
}

/* Inorder traversal helper */
static void printTreeInorder(Grammar *g, TreeNode *node,
                             const char *parentSymbol, FILE *out) {
  if (!node)
    return;

  /* Visit first child */
  if (node->firstChild) {
    /* Parent symbol for children = this node's symbol name */
    const char *mySymbol;
    if (node->sym.kind == SYM_NON_TERMINAL)
      mySymbol = g->ntNames[node->sym.id];
    else
      mySymbol = tokenTypeToString((TokenType)node->sym.id);

    printTreeInorder(g, node->firstChild, mySymbol, out);
  }

  /* Print current node */
  printNodeRow(g, node, parentSymbol, out);

  /* Visit remaining children (2nd child onward) */
  if (node->firstChild) {
    const char *mySymbol;
    if (node->sym.kind == SYM_NON_TERMINAL)
      mySymbol = g->ntNames[node->sym.id];
    else
      mySymbol = tokenTypeToString((TokenType)node->sym.id);

    for (TreeNode *c = node->firstChild->nextSibling; c; c = c->nextSibling)
      printTreeInorder(g, c, mySymbol, out);
  }
}

void printParseTree(TreeNode *root, int depth, FILE *out) {
  /* Legacy — redirects to printParseTreeFull logic if called directly */
  (void)depth;
  if (!root)
    return;
  fprintf(out, "%-25s %-6s %-18s %-15s %-25s %-5s %s\n", "lexeme", "line",
          "tokenName", "value", "parentNodeSymbol", "leaf", "NodeSymbol");
  fprintf(out,
          "------------------------------------------------------------------"
          "-----------------------------------------------------\n");
  /* Can't print inorder without Grammar, just do preorder as fallback */
}

void printParseTreeFull(Grammar *g, TreeNode *root, const char *outfile) {
  FILE *out = fopen(outfile, "w");
  if (!out) {
    perror("Cannot open output file for parse tree");
    return;
  }
  if (!root) {
    fprintf(out, "(empty tree)\n");
    fclose(out);
    return;
  }

  /* Print header */
  fprintf(out, "%-25s %-6s %-18s %-15s %-25s %-5s %s\n", "lexeme", "line",
          "tokenName", "value", "parentNodeSymbol", "leaf", "NodeSymbol");
  fprintf(out,
          "------------------------------------------------------------------"
          "-----------------------------------------------------\n");

  /* Inorder traversal */
  printTreeInorder(g, root, "ROOT", out);

  fclose(out);
}

/* ────────────────────────────────────────────────────────────────
 *  Free parse tree
 * ──────────────────────────────────────────────────────────────── */

void freeParseTree(TreeNode *node) {
  if (!node)
    return;
  freeParseTree(node->firstChild);
  freeParseTree(node->nextSibling);
  free(node);
}
