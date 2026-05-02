//Group 51
// Ashutosh Desai - 2023A7PS0675P
// Anushka Doshi - 2023A7PS0597P
// Aarya Jain - 2023A7PS0618P
// Devansh Agarwal - 2023A7PS0570P
#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct AllocNode {
  char *value;
  struct AllocNode *next;
} AllocNode;

typedef struct {
  FILE *out;
  Grammar *grammar;
  int tempCounter;
  int labelCounter;
  int indent;
  AllocNode *allocations;
} CodeGenContext;

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} StringBuilder;

static void emitLine(CodeGenContext *ctx, const char *fmt, ...) {
  for (int i = 0; i < ctx->indent; i++)
    fputs("  ", ctx->out);
  va_list args;
  va_start(args, fmt);
  vfprintf(ctx->out, fmt, args);
  va_end(args);
  fputc('\n', ctx->out);
}

static void sbInit(StringBuilder *sb) {
  sb->cap = 64;
  sb->len = 0;
  sb->data = (char *)malloc(sb->cap);
  if (!sb->data) {
    fprintf(stderr, "Out of memory\n");
    exit(1);
  }
  sb->data[0] = '\0';
}

static void sbAppend(StringBuilder *sb, const char *text) {
  size_t addLen = strlen(text);
  if (sb->len + addLen + 1 > sb->cap) {
    while (sb->len + addLen + 1 > sb->cap)
      sb->cap *= 2;
    char *newBuf = (char *)realloc(sb->data, sb->cap);
    if (!newBuf) {
      fprintf(stderr, "Out of memory\n");
      exit(1);
    }
    sb->data = newBuf;
  }
  memcpy(sb->data + sb->len, text, addLen + 1);
  sb->len += addLen;
}

static char *trackAlloc(CodeGenContext *ctx, char *value) {
  AllocNode *node = (AllocNode *)malloc(sizeof(AllocNode));
  if (!node) {
    fprintf(stderr, "Out of memory\n");
    exit(1);
  }
  node->value = value;
  node->next = ctx->allocations;
  ctx->allocations = node;
  return value;
}

static char *sbFinish(CodeGenContext *ctx, StringBuilder *sb) {
  return trackAlloc(ctx, sb->data);
}

static char *cg_strdup(CodeGenContext *ctx, const char *s) {
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (!copy) {
    fprintf(stderr, "Out of memory\n");
    exit(1);
  }
  memcpy(copy, s, len + 1);
  return trackAlloc(ctx, copy);
}

static void freeAllocations(CodeGenContext *ctx) {
  AllocNode *node = ctx->allocations;
  while (node) {
    AllocNode *next = node->next;
    free(node->value);
    free(node);
    node = next;
  }
}

static TreeNode *childAt(TreeNode *node, int index) {
  TreeNode *child = node ? node->firstChild : NULL;
  for (int i = 0; child && i < index; i++)
    child = child->nextSibling;
  return child;
}

static int isEpsNode(TreeNode *node) {
  TreeNode *child = node ? node->firstChild : NULL;
  return child && child->sym.kind == SYM_TERMINAL &&
         child->sym.id == (int)TK_EPS;
}

static const char *ntName(CodeGenContext *ctx, TreeNode *node) {
  if (!node || node->sym.kind != SYM_NON_TERMINAL)
    return NULL;
  return ctx->grammar->ntNames[node->sym.id];
}

static int isNT(CodeGenContext *ctx, TreeNode *node, const char *name) {
  const char *nt = ntName(ctx, node);
  return nt && strcmp(nt, name) == 0;
}

static const char *opLexeme(TokenType tok) {
  switch (tok) {
  case TK_PLUS:
    return "+";
  case TK_MINUS:
    return "-";
  case TK_MUL:
    return "*";
  case TK_DIV:
    return "/";
  case TK_LT:
    return "<";
  case TK_LE:
    return "<=";
  case TK_EQ:
    return "==";
  case TK_GT:
    return ">";
  case TK_GE:
    return ">=";
  case TK_NE:
    return "!=";
  case TK_AND:
    return "&&";
  case TK_OR:
    return "||";
  case TK_NOT:
    return "!";
  default:
    return "?";
  }
}

static char *newTemp(CodeGenContext *ctx) {
  char buf[32];
  snprintf(buf, sizeof(buf), "t%d", ctx->tempCounter++);
  return cg_strdup(ctx, buf);
}

static char *newLabel(CodeGenContext *ctx) {
  char buf[32];
  snprintf(buf, sizeof(buf), "L%d", ctx->labelCounter++);
  return cg_strdup(ctx, buf);
}

static void appendExpansions(CodeGenContext *ctx, TreeNode *node,
                             StringBuilder *sb) {
  (void)ctx;
  if (!node || isEpsNode(node))
    return;
  TreeNode *oneExpansion = childAt(node, 0);
  TreeNode *fieldId = oneExpansion ? childAt(oneExpansion, 1) : NULL;
  if (fieldId && fieldId->lexeme[0]) {
    sbAppend(sb, ".");
    sbAppend(sb, fieldId->lexeme);
  }
  TreeNode *more = childAt(node, 1);
  appendExpansions(ctx, more, sb);
}

static char *buildSingleOrRecId(CodeGenContext *ctx, TreeNode *node) {
  TreeNode *idNode = childAt(node, 0);
  StringBuilder sb;
  sbInit(&sb);
  if (idNode && idNode->lexeme[0])
    sbAppend(&sb, idNode->lexeme);
  TreeNode *option = childAt(node, 1);
  appendExpansions(ctx, option, &sb);
  return sbFinish(ctx, &sb);
}

static void appendIdList(CodeGenContext *ctx, TreeNode *node,
                         StringBuilder *sb) {
  (void)ctx;
  if (!node)
    return;
  TreeNode *idNode = childAt(node, 0);
  if (idNode && idNode->lexeme[0])
    sbAppend(sb, idNode->lexeme);
  TreeNode *more = childAt(node, 1);
  if (!more || isEpsNode(more))
    return;
  TreeNode *nextList = childAt(more, 1);
  if (nextList) {
    sbAppend(sb, ", ");
    appendIdList(ctx, nextList, sb);
  }
}

static char *buildIdList(CodeGenContext *ctx, TreeNode *node) {
  StringBuilder sb;
  sbInit(&sb);
  appendIdList(ctx, node, &sb);
  return sbFinish(ctx, &sb);
}

static void appendParamList(CodeGenContext *ctx, TreeNode *node,
                            StringBuilder *sb) {
  (void)ctx;
  if (!node)
    return;
  TreeNode *idNode = childAt(node, 1);
  if (idNode && idNode->lexeme[0])
    sbAppend(sb, idNode->lexeme);
  TreeNode *remaining = childAt(node, 2);
  if (!remaining || isEpsNode(remaining))
    return;
  TreeNode *nextParam = childAt(remaining, 1);
  if (nextParam) {
    sbAppend(sb, ", ");
    appendParamList(ctx, nextParam, sb);
  }
}

static char *buildParamList(CodeGenContext *ctx, TreeNode *node) {
  StringBuilder sb;
  sbInit(&sb);
  appendParamList(ctx, node, &sb);
  return sbFinish(ctx, &sb);
}

static char *genVar(CodeGenContext *ctx, TreeNode *node);
static char *genArithmeticExpression(CodeGenContext *ctx, TreeNode *node);
static char *genBooleanExpression(CodeGenContext *ctx, TreeNode *node);

static char *genFactor(CodeGenContext *ctx, TreeNode *node) {
  TreeNode *child = childAt(node, 0);
  if (child && child->sym.kind == SYM_TERMINAL &&
      child->sym.id == (int)TK_OP) {
    return genArithmeticExpression(ctx, childAt(node, 1));
  }
  return genVar(ctx, child);
}

static char *genTermPrime(CodeGenContext *ctx, TreeNode *node, char *inherited) {
  if (!node || isEpsNode(node))
    return inherited;
  TreeNode *opNode = childAt(node, 0);
  TreeNode *factorNode = childAt(node, 1);
  TreeNode *next = childAt(node, 2);
  TreeNode *opToken = opNode ? childAt(opNode, 0) : NULL;
  TokenType op = opToken ? (TokenType)opToken->sym.id : TK_ERROR;
  char *right = genFactor(ctx, factorNode);
  char *temp = newTemp(ctx);
  emitLine(ctx, "%s = %s %s %s", temp, inherited, opLexeme(op), right);
  return genTermPrime(ctx, next, temp);
}

static char *genTerm(CodeGenContext *ctx, TreeNode *node) {
  TreeNode *factorNode = childAt(node, 0);
  TreeNode *termPrime = childAt(node, 1);
  char *left = genFactor(ctx, factorNode);
  return genTermPrime(ctx, termPrime, left);
}

static char *genExpPrime(CodeGenContext *ctx, TreeNode *node, char *inherited) {
  if (!node || isEpsNode(node))
    return inherited;
  TreeNode *opNode = childAt(node, 0);
  TreeNode *termNode = childAt(node, 1);
  TreeNode *next = childAt(node, 2);
  TreeNode *opToken = opNode ? childAt(opNode, 0) : NULL;
  TokenType op = opToken ? (TokenType)opToken->sym.id : TK_ERROR;
  char *right = genTerm(ctx, termNode);
  char *temp = newTemp(ctx);
  emitLine(ctx, "%s = %s %s %s", temp, inherited, opLexeme(op), right);
  return genExpPrime(ctx, next, temp);
}

static char *genArithmeticExpression(CodeGenContext *ctx, TreeNode *node) {
  TreeNode *termNode = childAt(node, 0);
  TreeNode *expPrime = childAt(node, 1);
  char *left = genTerm(ctx, termNode);
  return genExpPrime(ctx, expPrime, left);
}

static char *genVar(CodeGenContext *ctx, TreeNode *node) {
  if (!node)
    return cg_strdup(ctx, "<?>");
  if (node->sym.kind == SYM_TERMINAL) {
    if (node->lexeme[0])
      return cg_strdup(ctx, node->lexeme);
  }
  if (node->sym.kind == SYM_NON_TERMINAL &&
      isNT(ctx, node, "singleOrRecId")) {
    return buildSingleOrRecId(ctx, node);
  }
  TreeNode *child = childAt(node, 0);
  if (!child)
    return cg_strdup(ctx, "<?>");
  if (child->sym.kind == SYM_TERMINAL && child->lexeme[0])
    return cg_strdup(ctx, child->lexeme);
  if (isNT(ctx, child, "singleOrRecId"))
    return buildSingleOrRecId(ctx, child);
  return cg_strdup(ctx, "<?>");
}

static char *genBooleanExpression(CodeGenContext *ctx, TreeNode *node) {
  TreeNode *child = childAt(node, 0);
  if (!child)
    return cg_strdup(ctx, "<?>");
  if (child->sym.kind == SYM_TERMINAL && child->sym.id == (int)TK_OP) {
    char *left = genBooleanExpression(ctx, childAt(node, 1));
    TreeNode *logicalOp = childAt(node, 3);
    TreeNode *opToken = logicalOp ? childAt(logicalOp, 0) : NULL;
    TokenType op = opToken ? (TokenType)opToken->sym.id : TK_ERROR;
    char *right = genBooleanExpression(ctx, childAt(node, 5));
    char *temp = newTemp(ctx);
    emitLine(ctx, "%s = %s %s %s", temp, left, opLexeme(op), right);
    return temp;
  }
  if (child->sym.kind == SYM_TERMINAL && child->sym.id == (int)TK_NOT) {
    char *inner = genBooleanExpression(ctx, childAt(node, 2));
    char *temp = newTemp(ctx);
    emitLine(ctx, "%s = %s%s", temp, opLexeme(TK_NOT), inner);
    return temp;
  }
  char *left = genVar(ctx, child);
  TreeNode *relOp = childAt(node, 1);
  TreeNode *opToken = relOp ? childAt(relOp, 0) : NULL;
  TokenType op = opToken ? (TokenType)opToken->sym.id : TK_ERROR;
  char *right = genVar(ctx, childAt(node, 2));
  char *temp = newTemp(ctx);
  emitLine(ctx, "%s = %s %s %s", temp, left, opLexeme(op), right);
  return temp;
}

static void genStmt(CodeGenContext *ctx, TreeNode *node);

static void genOtherStmts(CodeGenContext *ctx, TreeNode *node) {
  if (!node || isEpsNode(node))
    return;
  TreeNode *stmtNode = childAt(node, 0);
  TreeNode *rest = childAt(node, 1);
  genStmt(ctx, stmtNode);
  genOtherStmts(ctx, rest);
}

static void genAssignmentStmt(CodeGenContext *ctx, TreeNode *node) {
  char *target = buildSingleOrRecId(ctx, childAt(node, 0));
  char *value = genArithmeticExpression(ctx, childAt(node, 2));
  emitLine(ctx, "%s = %s", target, value);
}

static void genIterativeStmt(CodeGenContext *ctx, TreeNode *node) {
  char *startLabel = newLabel(ctx);
  char *endLabel = newLabel(ctx);
  emitLine(ctx, "%s:", startLabel);
  char *cond = genBooleanExpression(ctx, childAt(node, 2));
  emitLine(ctx, "ifFalse %s goto %s", cond, endLabel);
  ctx->indent++;
  genStmt(ctx, childAt(node, 4));
  genOtherStmts(ctx, childAt(node, 5));
  ctx->indent--;
  emitLine(ctx, "goto %s", startLabel);
  emitLine(ctx, "%s:", endLabel);
}

static void genElsePart(CodeGenContext *ctx, TreeNode *node, char *endLabel) {
  if (!node || isEpsNode(node)) {
    emitLine(ctx, "%s:", endLabel);
    return;
  }
  TreeNode *first = childAt(node, 0);
  if (first && first->sym.kind == SYM_TERMINAL &&
      first->sym.id == (int)TK_ELSE) {
    ctx->indent++;
    genStmt(ctx, childAt(node, 1));
    genOtherStmts(ctx, childAt(node, 2));
    ctx->indent--;
  }
  emitLine(ctx, "%s:", endLabel);
}

static void genConditionalStmt(CodeGenContext *ctx, TreeNode *node) {
  char *elseLabel = newLabel(ctx);
  char *endLabel = newLabel(ctx);
  char *cond = genBooleanExpression(ctx, childAt(node, 2));
  emitLine(ctx, "ifFalse %s goto %s", cond, elseLabel);
  ctx->indent++;
  genStmt(ctx, childAt(node, 5));
  genOtherStmts(ctx, childAt(node, 6));
  ctx->indent--;
  emitLine(ctx, "goto %s", endLabel);
  emitLine(ctx, "%s:", elseLabel);
  genElsePart(ctx, childAt(node, 7), endLabel);
}

static void genIoStmt(CodeGenContext *ctx, TreeNode *node) {
  TreeNode *first = childAt(node, 0);
  TreeNode *varNode = childAt(node, 2);
  char *value = genVar(ctx, varNode);
  if (first && first->sym.kind == SYM_TERMINAL &&
      first->sym.id == (int)TK_READ) {
    emitLine(ctx, "read %s", value);
  } else {
    emitLine(ctx, "write %s", value);
  }
}

static void genFunCallStmt(CodeGenContext *ctx, TreeNode *node) {
  TreeNode *outputParams = childAt(node, 0);
  TreeNode *funNode = childAt(node, 2);
  TreeNode *inputParams = childAt(node, 5);
  char *args = buildIdList(ctx, childAt(inputParams, 1));
  if (!outputParams || isEpsNode(outputParams)) {
    emitLine(ctx, "call %s(%s)", funNode->lexeme, args);
    return;
  }
  char *outs = buildIdList(ctx, childAt(outputParams, 1));
  emitLine(ctx, "call %s(%s) -> %s", funNode->lexeme, args, outs);
}

static void genReturnStmt(CodeGenContext *ctx, TreeNode *node) {
  TreeNode *optional = childAt(node, 1);
  if (!optional || isEpsNode(optional)) {
    emitLine(ctx, "return");
    return;
  }
  char *list = buildIdList(ctx, childAt(optional, 1));
  emitLine(ctx, "return %s", list);
}

static void genStmt(CodeGenContext *ctx, TreeNode *node) {
  if (!node)
    return;
  TreeNode *child = childAt(node, 0);
  if (isNT(ctx, child, "assignmentStmt")) {
    genAssignmentStmt(ctx, child);
  } else if (isNT(ctx, child, "iterativeStmt")) {
    genIterativeStmt(ctx, child);
  } else if (isNT(ctx, child, "conditionalStmt")) {
    genConditionalStmt(ctx, child);
  } else if (isNT(ctx, child, "ioStmt")) {
    genIoStmt(ctx, child);
  } else if (isNT(ctx, child, "funCallStmt")) {
    genFunCallStmt(ctx, child);
  }
}

static void genStmts(CodeGenContext *ctx, TreeNode *node) {
  genOtherStmts(ctx, childAt(node, 2));
  genReturnStmt(ctx, childAt(node, 3));
}

static void genFunction(CodeGenContext *ctx, TreeNode *node) {
  TreeNode *funNode = childAt(node, 0);
  TreeNode *inputPar = childAt(node, 1);
  TreeNode *outputPar = childAt(node, 2);
  TreeNode *stmts = childAt(node, 4);
  emitLine(ctx, "func %s", funNode->lexeme);
  ctx->indent++;
  if (inputPar) {
    char *params = buildParamList(ctx, childAt(inputPar, 4));
    if (params[0])
      emitLine(ctx, "params %s", params);
  }
  if (outputPar && !isEpsNode(outputPar)) {
    char *returns = buildParamList(ctx, childAt(outputPar, 4));
    if (returns[0])
      emitLine(ctx, "returns %s", returns);
  }
  genStmts(ctx, stmts);
  ctx->indent--;
  emitLine(ctx, "endfunc");
}

static void genOtherFunctions(CodeGenContext *ctx, TreeNode *node) {
  if (!node || isEpsNode(node))
    return;
  genFunction(ctx, childAt(node, 0));
  genOtherFunctions(ctx, childAt(node, 1));
}

static void genMainFunction(CodeGenContext *ctx, TreeNode *node) {
  TreeNode *stmts = childAt(node, 1);
  emitLine(ctx, "func _main");
  ctx->indent++;
  genStmts(ctx, stmts);
  ctx->indent--;
  emitLine(ctx, "endfunc");
}

static void genProgram(CodeGenContext *ctx, TreeNode *node) {
  genOtherFunctions(ctx, childAt(node, 0));
  genMainFunction(ctx, childAt(node, 1));
}

void generateCode(const char *filename, const char *outputfile) {
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
  TreeNode *tree = parseTokens(&tl, grammar, &pt, &ff, &errors, 1);

  if (errors) {
    printSyntaxErrors(errors);
    freeSyntaxErrors(errors);
  }

  if (!tree || errors) {
    freeGrammar(grammar);
    free(tl.buf);
    return;
  }

  FILE *out = fopen(outputfile, "w");
  if (!out) {
    perror("Cannot open output file for code generation");
    freeParseTree(tree);
    freeGrammar(grammar);
    free(tl.buf);
    return;
  }

  CodeGenContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.out = out;
  ctx.grammar = grammar;

  genProgram(&ctx, tree);

  fclose(out);
  freeAllocations(&ctx);
  freeParseTree(tree);
  freeGrammar(grammar);
  free(tl.buf);
}
