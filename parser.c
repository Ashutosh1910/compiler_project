// Ashutosh Desai - 2023A7PS0675P
// Anushka Doshi - 2023A7PS0597P
// Aarya Jain - 2023A7PS0618P
// Devansh Agarwal - 2023A7PS0570P
#include "parser.h"
#include "lexer.h"
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TermMapEntry termMap[TERM_MAP_CAP];
int termMapReady = 0;

unsigned int termHash(const char *s) {
  unsigned int h = 0;
  while (*s)
    h += (unsigned int)*s++;
  return h % TERM_MAP_CAP;
}

void termMapInit(void) {
  for (int i = 0; i < TERM_MAP_CAP; i++)
    termMap[i].key = NULL;

  for (int i = 0; i < NUM_TOKENS; i++) {
    const char *name = tokenTypeToString((TokenType)i);
    unsigned int idx = termHash(name);
    while (termMap[idx].key != NULL)
      idx = (idx + 1) % TERM_MAP_CAP;
    termMap[idx].key = name;
    termMap[idx].val = (TokenType)i;
  }

  unsigned int idx = termHash("eps");
  while (termMap[idx].key != NULL)
    idx = (idx + 1) % TERM_MAP_CAP;
  termMap[idx].key = "eps";
  termMap[idx].val = TK_EPS;

  termMapReady = 1;
}

TokenType terminalFromString(const char *name) {
  if (!termMapReady)
    termMapInit();

  unsigned int idx = termHash(name);
  while (termMap[idx].key != NULL) {
    if (strcmp(termMap[idx].key, name) == 0)
      return termMap[idx].val;
    idx = (idx + 1) % TERM_MAP_CAP;
  }
  return TK_ERROR;
}

TokenType curToken(TokenList *tokens, int tokenIdx) {
  return tokenIdx < tokens->size ? tokens->buf[tokenIdx].type : TK_DOLLAR;
}

int curLine(TokenList *tokens, int tokenIdx) {
  return tokenIdx < tokens->size ? (int)tokens->buf[tokenIdx].lineNo : -1;
}

const char *curLexeme(TokenList *tokens, int tokenIdx) {
  return tokenIdx < tokens->size ? tokenTypeToLexeme(&tokens->buf[tokenIdx])
                                 : "$";
}

void bs_clear(BitSet *s) {
  for (int i = 0; i < BITSET_WORDS; i++)
    s->bits[i] = 0;
}

void bs_add(BitSet *s, int bit) { s->bits[bit / 64] |= (1ULL << (bit % 64)); }

int bs_contains(const BitSet *s, int bit) {
  return (s->bits[bit / 64] >> (bit % 64)) & 1;
}

int bs_union(BitSet *dst, const BitSet *src) {
  int changed = 0;
  for (int i = 0; i < BITSET_WORDS; i++) {
    unsigned long long old = dst->bits[i];
    dst->bits[i] |= src->bits[i];
    if (dst->bits[i] != old)
      changed = 1;
  }
  return changed;
}

int bs_union_no_eps(BitSet *dst, const BitSet *src) {
  BitSet tmp;
  for (int i = 0; i < BITSET_WORDS; i++)
    tmp.bits[i] = src->bits[i];
  tmp.bits[TK_EPS / 64] &= ~(1ULL << (TK_EPS % 64));
  return bs_union(dst, &tmp);
}

int bs_is_empty(const BitSet *s) {
  for (int i = 0; i < BITSET_WORDS; i++)
    if (s->bits[i])
      return 0;
  return 1;
}

int findOrAddNT(Grammar *g, const char *name) {
  for (int i = 0; i < g->numNT; i++) {
    if (strcmp(g->ntNames[i], name) == 0)
      return i;
  }
  int idx = g->numNT++;
  strncpy(g->ntNames[idx], name, MAX_SYMBOL_NAME - 1);
  g->ntNames[idx][MAX_SYMBOL_NAME - 1] = '\0';
  return idx;
}

Symbol makeSymbol(Grammar *g, const char *tok) {
  Symbol s;
  if (tok[0] == '<') {
    char name[MAX_SYMBOL_NAME];
    int len = (int)strlen(tok);
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

void addRule(Grammar *g, int lhsNT, Symbol *rhs, int rhsLen) {

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
    int len = (int)strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';

    if (len == 0)
      continue;

    char *pos = line;

    char *open = strchr(pos, '<'); // find start of non-terminal
    char *close = strchr(pos, '>');
    if (!open || !close)
      continue;

    char ntName[MAX_SYMBOL_NAME];
    int j = 0;
    for (char *p = open + 1; p < close && j < MAX_SYMBOL_NAME - 1; p++)
      ntName[j++] = *p;
    ntName[j] = '\0';
    currentLHS = findOrAddNT(g, ntName);

    pos = strstr(pos, "::=");
    if (!pos)
      continue;
    pos += 3;

    Symbol rhsBuf[MAX_RHS];
    int rhsLen = 0;

    char *saveptr = NULL;
    char linecopy[MAX_LINE_LEN];
    strncpy(linecopy, pos, MAX_LINE_LEN - 1);
    linecopy[MAX_LINE_LEN - 1] = '\0';

    char *token = strtok_r(linecopy, " \t", &saveptr);
    while (token) {
      if (strcmp(token, "|") == 0) {
        if (rhsLen > 0 || currentLHS >= 0) {
          addRule(g, currentLHS, rhsBuf, rhsLen);
          rhsLen = 0;
        }
      } else {
        rhsBuf[rhsLen++] = makeSymbol(g, token);
      }
      token = strtok_r(NULL, " \t", &saveptr);
    }
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

int firstOfString(Grammar *g, FirstFollowSets *ff, Symbol *syms, int count,
                  BitSet *dst) {
  for (int i = 0; i < count; i++) {
    if (syms[i].kind == SYM_TERMINAL) {
      bs_add(dst, syms[i].id);
      if (syms[i].id == (int)TK_EPS) {
        continue;
      }
      return 0;
    } else {
      bs_union_no_eps(dst, &ff->first[syms[i].id]);
      if (!bs_contains(&ff->first[syms[i].id], TK_EPS))
        return 0;
    }
  }
  bs_add(dst, TK_EPS);
  return 1;
}

void computeFirstDFS(Grammar *g, FirstFollowSets *ff, int nt, int *visited) {
  if (visited[nt] == 2)
    return;
  visited[nt] = 1;

  for (int r = 0; r < g->numRules; r++) {
    GrammarRule *rule = &g->rules[r];
    if (rule->lhs != nt)
      continue;

    for (int i = 0; i < rule->rhsLen; i++) {
      Symbol sym = rule->rhs[i];
      if (sym.kind == SYM_TERMINAL) {
        bs_add(&ff->first[nt], sym.id);
        if (sym.id == (int)TK_EPS)
          continue;
        break;
      } else {
        if (visited[sym.id] == 0)
          computeFirstDFS(g, ff, sym.id, visited);
        bs_union_no_eps(&ff->first[nt], &ff->first[sym.id]);
        if (!bs_contains(&ff->first[sym.id], TK_EPS))
          break;
        if (i == rule->rhsLen - 1)
          bs_add(&ff->first[nt], TK_EPS);
      }
    }

    if (rule->rhsLen == 0)
      bs_add(&ff->first[nt], TK_EPS);
  }

  visited[nt] = 2;
}

void computeFollowDFS(Grammar *g, FirstFollowSets *ff, int nt, int *visited) {
  if (visited[nt] == 2)
    return;
  visited[nt] = 1;

  for (int r = 0; r < g->numRules; r++) {
    GrammarRule *rule = &g->rules[r];
    for (int i = 0; i < rule->rhsLen; i++) {
      if (rule->rhs[i].kind != SYM_NON_TERMINAL || rule->rhs[i].id != nt)
        continue;

      BitSet firstBeta;
      bs_clear(&firstBeta);
      int betaDerivesEps = firstOfString(g, ff, &rule->rhs[i + 1],
                                         rule->rhsLen - i - 1, &firstBeta);
      bs_union_no_eps(&ff->follow[nt], &firstBeta);

      if (betaDerivesEps) {
        int A = rule->lhs;
        if (A != nt) {
          if (visited[A] == 0)
            computeFollowDFS(g, ff, A, visited);
          bs_union(&ff->follow[nt], &ff->follow[A]);
        }
      }
    }
  }

  visited[nt] = 2;
}

void computeFirstAndFollow(Grammar *g, FirstFollowSets *ff) {
  int visited[MAX_NON_TERMINALS];

  for (int i = 0; i < g->numNT; i++) {
    bs_clear(&ff->first[i]);
    bs_clear(&ff->follow[i]);
  }

  for (int i = 0; i < MAX_NON_TERMINALS; i++)
    visited[i] = 0;
  for (int i = 0; i < g->numNT; i++) {
    if (visited[i] == 0)
      computeFirstDFS(g, ff, i, visited);
  }

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

  bs_add(&ff->follow[0], TK_DOLLAR);

  for (int i = 0; i < MAX_NON_TERMINALS; i++)
    visited[i] = 0;
  for (int i = 0; i < g->numNT; i++) {
    if (visited[i] == 0)
      computeFollowDFS(g, ff, i, visited);
  }

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

        BitSet firstBeta;
        bs_clear(&firstBeta);
        int betaDerivesEps = firstOfString(g, ff, &rule->rhs[i + 1],
                                           rule->rhsLen - i - 1, &firstBeta);

        if (bs_union_no_eps(&ff->follow[B], &firstBeta))
          changed = 1;

        if (betaDerivesEps) {
          if (bs_union(&ff->follow[B], &ff->follow[A]))
            changed = 1;
        }
      }
    }
  }
}

void createParseTable(Grammar *g, FirstFollowSets *ff, ParseTable *pt) {
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

    for (int t = 0; t < NUM_TOKENS; t++) {
      if (t == (int)TK_EPS)
        continue;
      if (bs_contains(&firstAlpha, t)) {
        if (pt->table[A][t] != -1 && pt->table[A][t] != r) {
          fprintf(stderr, "LL(1) conflict at <%s>, %s: rule %d vs %d\n",
                  g->ntNames[A], tokenTypeToString((TokenType)t),
                  pt->table[A][t], r);
        }
        pt->table[A][t] = r;
      }
    }
    // if language if ll(1) compatible
    if (alphaDerivesEps) {
      for (int t = 0; t < NUM_TOKENS; t++) {
        if (t == (int)TK_EPS)
          continue;
        if (bs_contains(&ff->follow[A], t)) {
          if (pt->table[A][t] != -1 && pt->table[A][t] != r) {
            fprintf(stderr, "LL(1) conflict at <%s>, %s: rule %d vs %d\n",
                    g->ntNames[A], tokenTypeToString((TokenType)t),
                    pt->table[A][t], r);
          }
          pt->table[A][t] = r;
        }
      }
    }
  }
}

TreeNode *newTreeNode(Symbol sym) {
  TreeNode *n = (TreeNode *)calloc(1, sizeof(TreeNode));
  n->sym = sym;
  n->ruleIndex = -1;
  n->lineNo = -1;
  n->lexeme[0] = '\0';
  return n;
}

void addChild(TreeNode *parent, TreeNode *child) {
  if (!parent->firstChild) {
    parent->firstChild = child;
  } else {
    TreeNode *c = parent->firstChild;
    while (c->nextSibling)
      c = c->nextSibling;
    c->nextSibling = child;
  }
}

void addSyntaxError(SyntaxError **head, int lineNo, const char *message) {
  SyntaxError *err = (SyntaxError *)calloc(1, sizeof(SyntaxError));
  err->lineNo = lineNo;
  strncpy(err->message, message, sizeof(err->message) - 1);

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
  
  int count = 0;
  for (SyntaxError *e = errors; e; e = e->next) {
    count++;
    printf("[SYNTAX-ERROR] at line %d: %s\n", e->lineNo, e->message);
  }
  printf("Total: %d syntax error(s)\n", count);
}

void freeSyntaxErrors(SyntaxError *errors) {
  while (errors) {
    SyntaxError *tmp = errors;
    errors = errors->next;
    free(tmp);
  }
}

void stackInit(Stack *s) { s->top = -1; }
int stackEmpty(Stack *s) { return s->top < 0; }

void stackPush(Stack *s, Symbol sym, TreeNode *node) {
  if (s->top >= STACK_CAP - 1) {
    fprintf(stderr, "[PARSER] Stack overflow!\n");
    return;
  }
  s->top++;
  s->data[s->top].sym = sym;
  s->data[s->top].node = node;
}

StackEntry stackPop(Stack *s) { return s->data[s->top--]; }
StackEntry stackPeek(Stack *s) { return s->data[s->top]; }

int isSyncToken(int tok) {
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

int isInSyncOrFollow(FirstFollowSets *ff, int nt, int tok) {
  if (isSyncToken(tok))
    return 1;
  if (bs_contains(&ff->follow[nt], tok))
    return 1;
  return 0;
}

void addSyntaxErrorNoDup(SyntaxError **head, int *lastErrLine, int lineNo,
                         const char *message) {
  for (SyntaxError *p = *head; p; p = p->next) {
    if (p->lineNo == lineNo && strcmp(p->message, message) == 0)
      return;
  }

  int isLexerError = (strncmp(message, "Unknown", 7) == 0);
  if (!isLexerError && *lastErrLine == lineNo)
    return;

  if (!isLexerError)
    *lastErrLine = lineNo;

  SyntaxError *err = (SyntaxError *)calloc(1, sizeof(SyntaxError));
  err->lineNo = lineNo;
  strncpy(err->message, message, sizeof(err->message) - 1);

  if (*head == NULL) {
    *head = err;
  } else {
    SyntaxError *p = *head;
    while (p->next)
      p = p->next;
    p->next = err;
  }
}

void skipComments(TokenList *tokens, int *tokenIdx, SyntaxError **errors,
                  int *lastErrLine) {
  while (*tokenIdx < tokens->size &&
         (curToken(tokens, *tokenIdx) == TK_COMMENT)) {
    (*tokenIdx)++;
  }
}

TreeNode *parseTokens(TokenList *tokens, Grammar *g, ParseTable *pt,
                      FirstFollowSets *ff, SyntaxError **errors,
                      int buildTree) {
  Stack stack;
  stackInit(&stack);

  Symbol startSym = {SYM_NON_TERMINAL, 0};
  TreeNode *root = buildTree ? newTreeNode(startSym) : NULL;

  Symbol dollarSym = {SYM_TERMINAL, TK_DOLLAR};
  stackPush(&stack, dollarSym, NULL);
  stackPush(&stack, startSym, root);

  int tokenIdx = 0;
  int lastErrLine = -1;

  skipComments(tokens, &tokenIdx, errors, &lastErrLine);

  while (!stackEmpty(&stack)) {
    StackEntry top = stackPeek(&stack);
    TokenType curTok = curToken(tokens, tokenIdx);

    if (top.sym.kind == SYM_TERMINAL) {

      if (top.sym.id == (int)TK_EPS) {
        stackPop(&stack);
        if (top.node) {
          top.node->lineNo = curLine(tokens, tokenIdx);
          strncpy(top.node->lexeme, "----", 30);
        }
        continue;
      }

      if (top.sym.id == (int)TK_DOLLAR) {
        if (curTok == TK_DOLLAR) {
          stackPop(&stack);
          break;
        } else {
          char msgBuf[256];
          snprintf(msgBuf, sizeof(msgBuf),
                   "Unexpected token '%s' (%s) after end of program",
                   curLexeme(tokens, tokenIdx), tokenTypeToString(curTok));
          addSyntaxErrorNoDup(errors, &lastErrLine, curLine(tokens, tokenIdx),
                              msgBuf);
          break;
        }
      }

      if (top.sym.id == (int)curTok) {
        stackPop(&stack);
        if (top.node) {
          top.node->lineNo = curLine(tokens, tokenIdx);
          strncpy(top.node->lexeme, curLexeme(tokens, tokenIdx), 30);
          top.node->lexeme[30] = '\0';
        }
        tokenIdx++;
        skipComments(tokens, &tokenIdx, errors, &lastErrLine);
      } else {
        char msgBuf[256];
        snprintf(msgBuf, sizeof(msgBuf),
                 "The token %s for lexeme %s does not match with the expected "
                 "token %s",
                 tokenTypeToString(curTok), curLexeme(tokens, tokenIdx),
                 tokenTypeToString((TokenType)top.sym.id));
        addSyntaxErrorNoDup(errors, &lastErrLine, curLine(tokens, tokenIdx),
                            msgBuf);
        stackPop(&stack);
      }
    } else {
      int nt = top.sym.id;
      int ruleIdx = pt->table[nt][(int)curTok];

      if (ruleIdx == -1) {
        char msgBuf[256];
        snprintf(msgBuf, sizeof(msgBuf),
                 "Invalid token %s encountered with value %s stack top %s",
                 tokenTypeToString(curTok), curLexeme(tokens, tokenIdx),
                 g->ntNames[nt]);
        addSyntaxErrorNoDup(errors, &lastErrLine, curLine(tokens, tokenIdx),
                            msgBuf);

        if (isInSyncOrFollow(ff, nt, (int)curTok)) {
          stackPop(&stack);
        } else {
          if (curTok == TK_DOLLAR) {
            stackPop(&stack);
          } else {
            tokenIdx++;
            skipComments(tokens, &tokenIdx, errors, &lastErrLine);
          }
        }
        continue;
      }

      stackPop(&stack);

      GrammarRule *rule = &g->rules[ruleIdx];
      if (buildTree) {
        TreeNode *parentNode = top.node;
        if (parentNode)
          parentNode->ruleIndex = ruleIdx;

        TreeNode *childNodes[MAX_RHS];
        for (int i = 0; i < rule->rhsLen; i++) {
          childNodes[i] = newTreeNode(rule->rhs[i]);
          addChild(parentNode, childNodes[i]);
        }

        for (int i = rule->rhsLen - 1; i >= 0; i--) {
          stackPush(&stack, rule->rhs[i], childNodes[i]);
        }
      } else {
        for (int i = rule->rhsLen - 1; i >= 0; i--) {
          stackPush(&stack, rule->rhs[i], NULL);
        }
      }
    }
  }

  return root;
}

void printNodeRow(Grammar *g, TreeNode *node, const char *parentSymbol,
                  FILE *out) {
  int isLeaf = (node->firstChild == NULL);
  const char *lexeme;
  const char *tokenName;
  const char *valueIfNumber = "----";
  const char *nodeSymbol = "----";
  char valueBuf[32];

  if (isLeaf) {
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

    if (node->sym.id == (int)TK_NUM || node->sym.id == (int)TK_RNUM) {
      strncpy(valueBuf, node->lexeme, sizeof(valueBuf) - 1);
      valueBuf[sizeof(valueBuf) - 1] = '\0';
      valueIfNumber = valueBuf;
    }
  } else {
    lexeme = "----";
    tokenName = "----";
    if (node->sym.kind == SYM_NON_TERMINAL)
      nodeSymbol = g->ntNames[node->sym.id];
  }

  fprintf(out, "%-25s %-6d %-18s %-15s %-25s %-5s %s\n", lexeme, node->lineNo,
          tokenName, valueIfNumber, parentSymbol, isLeaf ? "yes" : "no",
          nodeSymbol);
}

void printTreeInorder(Grammar *g, TreeNode *node, const char *parentSymbol,
                      FILE *out) {
  if (!node)
    return;

  if (node->firstChild) {
    const char *mySymbol;
    if (node->sym.kind == SYM_NON_TERMINAL)
      mySymbol = g->ntNames[node->sym.id];
    else
      mySymbol = tokenTypeToString((TokenType)node->sym.id);

    printTreeInorder(g, node->firstChild, mySymbol, out);
  }

  printNodeRow(g, node, parentSymbol, out);

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
  (void)depth;
  if (!root)
    return;
  fprintf(out, "%-25s %-6s %-18s %-15s %-25s %-5s %s\n", "lexeme", "line",
          "tokenName", "value", "parentNodeSymbol", "leaf", "NodeSymbol");
  fprintf(out,
          "------------------------------------------------------------------"
          "-----------------------------------------------------\n");
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

  fprintf(out, "%-25s %-6s %-18s %-15s %-25s %-5s %s\n", "lexeme", "line",
          "tokenName", "value", "parentNodeSymbol", "leaf", "NodeSymbol");
  fprintf(out,
          "------------------------------------------------------------------"
          "-----------------------------------------------------\n");

  printTreeInorder(g, root, "ROOT", out);

  fclose(out);
}

void freeParseTree(TreeNode *node) {
  if (!node)
    return;
  freeParseTree(node->firstChild);
  freeParseTree(node->nextSibling);
  free(node);
}

void parseWithPrinting(const char *filename, const char *outputfile) {
  State state = initializeState(filename, 0);
  TokenList tl = scan(&state);
  printf("Lexing complete: %d tokens\n", tl.size);

  Grammar *grammar = loadGrammar("grammar.txt");
  if (!grammar) {
    printf("Failed to load grammar\n");
    return;
  }
  printf("Grammar loaded: %d rules, %d non-terminals\n", grammar->numRules,
         grammar->numNT);

  FirstFollowSets ff;
  computeFirstAndFollow(grammar, &ff);
  printf("FIRST and FOLLOW sets computed\n");

  ParseTable pt;
  createParseTable(grammar, &ff, &pt);
  printf("Parse table constructed\n");

  SyntaxError *errors = NULL;
  TreeNode *tree = parseTokens(&tl, grammar, &pt, &ff, &errors, 1);

  if (errors) {
    printSyntaxErrors(errors);
    freeSyntaxErrors(errors);
  }else{
    printf("No syntax errors in this %s\n",filename);
  }

  if (tree) {
    printParseTreeFull(grammar, tree, outputfile);
    printf("Parse tree written to %s\n", outputfile);
    freeParseTree(tree);
  }

  freeGrammar(grammar);
  free(tl.buf);
}

void parseWithoutPrinting(const char *filename) {
  State state = initializeState(filename, 0);
  TokenList tl = scan(&state);
  Grammar *grammar = loadGrammar("grammar.txt");
  if (!grammar) {
    printf("Failed to load grammar\n");
    return;
  }
  FirstFollowSets ff;
  computeFirstAndFollow(grammar, &ff);
  ParseTable pt;
  createParseTable(grammar, &ff, &pt);
  SyntaxError *errors = NULL;
  TreeNode *tree = parseTokens(&tl, grammar, &pt, &ff, &errors, 0);
  if (errors) {
    printSyntaxErrors(errors);
    freeSyntaxErrors(errors);
  }
  freeGrammar(grammar);
  free(tl.buf);
}