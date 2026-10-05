// Group 51
// Ashutosh Desai - 2023A7PS0675P
// Anushka Doshi - 2023A7PS0597P
// Aarya Jain - 2023A7PS0618P
// Devansh Agarwal - 2023A7PS0570P
//
// Parse tree -> AST. Every builder below handles exactly one non-terminal of
// grammar.txt; the comment above each one quotes the production(s) it reads.
#include "ast.h"
#include "logging.h"
#include <stdlib.h>
#include <string.h>

static Grammar *G; // grammar of the tree being converted (for NT names)

/* ---------------------------------------------------------------- helpers */

static void *xcalloc(size_t n, size_t size) {
  void *p = calloc(n, size);
  if (!p) {
    fprintf(stderr, "out of memory\n");
    exit(2);
  }
  return p;
}

static TreeNode *kid(TreeNode *n, int i) {
  TreeNode *c = n ? n->firstChild : NULL;
  while (c && i-- > 0)
    c = c->nextSibling;
  return c;
}

static int isNT(TreeNode *n, const char *name) {
  return n && n->sym.kind == SYM_NON_TERMINAL &&
         strcmp(G->ntNames[n->sym.id], name) == 0;
}

static int isTok(TreeNode *n, TokenType t) {
  return n && n->sym.kind == SYM_TERMINAL && n->sym.id == (int)t;
}

// true for a non-terminal that was expanded with its eps alternative
static int isEpsilon(TreeNode *n) { return n && isTok(kid(n, 0), TK_EPS); }

static void copyName(char *dst, const char *src) {
  int i = 0;
  for (; i < AST_NAME_LEN - 1 && src[i]; i++)
    dst[i] = src[i];
  dst[i] = '\0';
}

/* ------------------------------------------------------------------ types */

// <primitiveDatatype> ::= TK_INT | TK_REAL
// <constructedDatatype> ::= TK_RECORD TK_RUID | TK_UNION TK_RUID | TK_RUID
// <dataType> ::= <primitiveDatatype> | <constructedDatatype>
// <fieldType> ::= <primitiveDatatype> | TK_RUID
static AstTypeRef buildTypeRef(TreeNode *n) {
  AstTypeRef t;
  memset(&t, 0, sizeof(t));
  t.keyword = TK_EPS;
  if (isNT(n, "dataType") || isNT(n, "fieldType")) {
    TreeNode *c = kid(n, 0);
    if (isTok(c, TK_RUID)) { // <fieldType> ::= TK_RUID
      t.kind = TREF_NAMED;
      copyName(t.name, c->lexeme);
      t.line = c->lineNo;
      return t;
    }
    return buildTypeRef(c);
  }
  if (isNT(n, "primitiveDatatype")) {
    TreeNode *c = kid(n, 0);
    t.kind = isTok(c, TK_INT) ? TREF_INT : TREF_REAL;
    t.line = c->lineNo;
    return t;
  }
  // constructedDatatype
  TreeNode *c = kid(n, 0);
  t.kind = TREF_NAMED;
  if (isTok(c, TK_RECORD) || isTok(c, TK_UNION)) {
    t.keyword = (TokenType)c->sym.id;
    c = kid(n, 1);
  }
  copyName(t.name, c->lexeme);
  t.line = c->lineNo;
  return t;
}

/* ------------------------------------------------------------- variables */

// <singleOrRecId> ::= TK_ID <option_single_constructed>
// <option_single_constructed> ::= eps | <oneExpansion> <moreExpansions>
// <oneExpansion> ::= TK_DOT TK_FIELDID
// <moreExpansions> ::= <oneExpansion> <moreExpansions> | eps
static AstVarRef buildVarRef(TreeNode *n) {
  AstVarRef v;
  memset(&v, 0, sizeof(v));
  TreeNode *id = kid(n, 0);
  copyName(v.name, id->lexeme);
  v.line = id->lineNo;

  // count the fields first, then copy them
  TreeNode *opt = kid(n, 1);
  int count = 0;
  if (!isEpsilon(opt)) {
    count = 1;
    for (TreeNode *m = kid(opt, 1); !isEpsilon(m); m = kid(m, 1))
      count++;
  }
  if (count > 0) {
    v.fields = xcalloc(count, AST_NAME_LEN);
    v.numFields = count;
    TreeNode *one = kid(opt, 0);
    TreeNode *more = kid(opt, 1);
    for (int i = 0; i < count; i++) {
      copyName(v.fields[i], kid(one, 1)->lexeme);
      if (i + 1 < count) {
        one = kid(more, 0);
        more = kid(more, 1);
      }
    }
  }
  return v;
}

/* ------------------------------------------------------------ expressions */

static Expr *newExpr(ExprKind kind, int line) {
  Expr *e = xcalloc(1, sizeof(Expr));
  e->kind = kind;
  e->line = line;
  return e;
}

// <var> ::= <singleOrRecId> | TK_NUM | TK_RNUM
static Expr *buildVarExpr(TreeNode *n) {
  TreeNode *c = kid(n, 0);
  if (isTok(c, TK_NUM) || isTok(c, TK_RNUM)) {
    Expr *e = newExpr(isTok(c, TK_NUM) ? EXPR_NUM : EXPR_RNUM, c->lineNo);
    copyName(e->text, c->lexeme);
    if (e->kind == EXPR_NUM)
      e->ival = strtoll(c->lexeme, NULL, 10);
    else
      e->rval = strtod(c->lexeme, NULL);
    return e;
  }
  Expr *e = newExpr(EXPR_VAR, kid(c, 0)->lineNo);
  e->var = buildVarRef(c);
  return e;
}

static Expr *buildArith(TreeNode *n);

// <factor> ::= TK_OP <arithmeticExpression> TK_CL | <var>
static Expr *buildFactor(TreeNode *n) {
  TreeNode *c = kid(n, 0);
  if (isTok(c, TK_OP))
    return buildArith(kid(n, 1));
  return buildVarExpr(c);
}

static Expr *newBinop(TreeNode *opNode, Expr *l, Expr *r) {
  TreeNode *opTok = kid(opNode, 0); // <low/highPrecedenceOperators> ::= op
  Expr *e = newExpr(EXPR_BINOP, opTok->lineNo);
  e->op = (TokenType)opTok->sym.id;
  e->left = l;
  e->right = r;
  e->depth = 1 + (l->depth > r->depth ? l->depth : r->depth);
  return e;
}

// <term> ::= <factor> <termPrime>
// <termPrime> ::= <highPrecedenceOperators> <factor> <termPrime> | eps
// The primes are folded left so that a / b / c means (a / b) / c.
static Expr *buildTerm(TreeNode *n) {
  Expr *acc = buildFactor(kid(n, 0));
  for (TreeNode *p = kid(n, 1); !isEpsilon(p); p = kid(p, 2))
    acc = newBinop(kid(p, 0), acc, buildFactor(kid(p, 1)));
  return acc;
}

// <arithmeticExpression> ::= <term> <expPrime>
// <expPrime> ::= <lowPrecedenceOperators> <term> <expPrime> | eps
static Expr *buildArith(TreeNode *n) {
  Expr *acc = buildTerm(kid(n, 0));
  for (TreeNode *p = kid(n, 1); !isEpsilon(p); p = kid(p, 2))
    acc = newBinop(kid(p, 0), acc, buildTerm(kid(p, 1)));
  return acc;
}

// <booleanExpression> ::= TK_OP <booleanExpression> TK_CL <logicalOp>
//                         TK_OP <booleanExpression> TK_CL
//                       | <var> <relationalOp> <var>
//                       | TK_NOT TK_OP <booleanExpression> TK_CL
static BoolExpr *buildBool(TreeNode *n) {
  BoolExpr *b = xcalloc(1, sizeof(BoolExpr));
  TreeNode *c = kid(n, 0);
  if (isTok(c, TK_NOT)) {
    b->kind = BOOL_NOT;
    b->line = c->lineNo;
    b->left = buildBool(kid(n, 2));
  } else if (isTok(c, TK_OP)) {
    TreeNode *logical = kid(kid(n, 3), 0);
    b->kind = isTok(logical, TK_AND) ? BOOL_AND : BOOL_OR;
    b->line = logical->lineNo;
    b->left = buildBool(kid(n, 1));
    b->right = buildBool(kid(n, 5));
  } else {
    TreeNode *rel = kid(kid(n, 1), 0);
    b->kind = BOOL_REL;
    b->relop = (TokenType)rel->sym.id;
    b->line = rel->lineNo;
    b->lhs = buildVarExpr(c);
    b->rhs = buildVarExpr(kid(n, 2));
  }
  return b;
}

/* ------------------------------------------------------------- statements */

// <idList> ::= TK_ID <more_ids>
// <more_ids> ::= TK_COMMA <idList> | eps
static IdList buildIdList(TreeNode *n) {
  IdList l = {NULL, 0};
  for (TreeNode *p = n; p; p = isEpsilon(kid(p, 1)) ? NULL : kid(kid(p, 1), 1))
    l.count++;
  l.ids = xcalloc(l.count > 0 ? l.count : 1, sizeof(AstId));
  int i = 0;
  for (TreeNode *p = n; p; p = isEpsilon(kid(p, 1)) ? NULL : kid(kid(p, 1), 1)) {
    copyName(l.ids[i].name, kid(p, 0)->lexeme);
    l.ids[i].line = kid(p, 0)->lineNo;
    i++;
  }
  return l;
}

static Stmt *buildStmt(TreeNode *n);

// <otherStmts> ::= <stmt> <otherStmts> | eps
static Stmt *buildStmtList(TreeNode *first, TreeNode *others) {
  Stmt *head = first ? buildStmt(first) : NULL;
  Stmt *tail = head;
  for (TreeNode *p = others; !isEpsilon(p); p = kid(p, 1)) {
    Stmt *s = buildStmt(kid(p, 0));
    if (tail)
      tail->next = s;
    else
      head = s;
    tail = s;
  }
  return head;
}

static Stmt *newStmt(StmtKind kind, int line) {
  Stmt *s = xcalloc(1, sizeof(Stmt));
  s->kind = kind;
  s->line = line;
  return s;
}

// <stmt> ::= <assignmentStmt> | <iterativeStmt> | <conditionalStmt>
//          | <ioStmt> | <funCallStmt>
static Stmt *buildStmt(TreeNode *n) {
  TreeNode *c = kid(n, 0);
  Stmt *s;

  if (isNT(c, "assignmentStmt")) {
    // <singleOrRecId> TK_ASSIGNOP <arithmeticExpression> TK_SEM
    s = newStmt(STMT_ASSIGN, kid(kid(c, 0), 0)->lineNo);
    s->lhs = buildVarRef(kid(c, 0));
    s->rhs = buildArith(kid(c, 2));
  } else if (isNT(c, "iterativeStmt")) {
    // TK_WHILE TK_OP <booleanExpression> TK_CL <stmt> <otherStmts> TK_ENDWHILE
    s = newStmt(STMT_WHILE, kid(c, 0)->lineNo);
    s->cond = buildBool(kid(c, 2));
    s->body = buildStmtList(kid(c, 4), kid(c, 5));
    s->endLine = kid(c, 6)->lineNo;
  } else if (isNT(c, "conditionalStmt")) {
    // TK_IF TK_OP <booleanExpression> TK_CL TK_THEN <stmt> <otherStmts>
    // <elsePart>
    s = newStmt(STMT_IF, kid(c, 0)->lineNo);
    s->cond = buildBool(kid(c, 2));
    s->body = buildStmtList(kid(c, 5), kid(c, 6));
    // <elsePart> ::= TK_ELSE <stmt> <otherStmts> TK_ENDIF | TK_ENDIF
    TreeNode *e = kid(c, 7);
    if (isTok(kid(e, 0), TK_ELSE)) {
      s->elseBody = buildStmtList(kid(e, 1), kid(e, 2));
      s->endLine = kid(e, 3)->lineNo;
    } else {
      s->endLine = kid(e, 0)->lineNo;
    }
  } else if (isNT(c, "ioStmt")) {
    // TK_READ TK_OP <var> TK_CL TK_SEM | TK_WRITE TK_OP <var> TK_CL TK_SEM
    TreeNode *kw = kid(c, 0);
    s = newStmt(isTok(kw, TK_READ) ? STMT_READ : STMT_WRITE, kw->lineNo);
    s->ioArg = buildVarExpr(kid(c, 2));
  } else {
    // <funCallStmt> ::= <outputParameters> TK_CALL TK_FUNID TK_WITH
    //                   TK_PARAMETERS <inputParameters> TK_SEM
    // <outputParameters> ::= TK_SQL <idList> TK_SQR TK_ASSIGNOP | eps
    // <inputParameters> ::= TK_SQL <idList> TK_SQR
    TreeNode *fun = kid(c, 2);
    s = newStmt(STMT_CALL, kid(c, 1)->lineNo);
    copyName(s->funName, fun->lexeme);
    TreeNode *outs = kid(c, 0);
    if (!isEpsilon(outs))
      s->outs = buildIdList(kid(outs, 1));
    s->ins = buildIdList(kid(kid(c, 5), 1));
  }
  return s;
}

/* ----------------------------------------------------- definitions & decls */

static void appendDecl(Decl **head, Decl *d) {
  while (*head)
    head = &(*head)->next;
  *head = d;
}

// <parameter_list> ::= <dataType> TK_ID <remaining_list>
// <remaining_list> ::= TK_COMMA <parameter_list> | eps
static Decl *buildParams(TreeNode *n) {
  Decl *head = NULL;
  while (n) {
    Decl *d = xcalloc(1, sizeof(Decl));
    d->type = buildTypeRef(kid(n, 0));
    copyName(d->name, kid(n, 1)->lexeme);
    d->line = kid(n, 1)->lineNo;
    appendDecl(&head, d);
    TreeNode *rem = kid(n, 2);
    n = isEpsilon(rem) ? NULL : kid(rem, 1);
  }
  return head;
}

// <fieldDefinition> ::= TK_TYPE <fieldType> TK_COLON TK_FIELDID TK_SEM
static FieldDef *buildField(TreeNode *n) {
  FieldDef *f = xcalloc(1, sizeof(FieldDef));
  f->type = buildTypeRef(kid(n, 1));
  copyName(f->name, kid(n, 3)->lexeme);
  f->line = kid(n, 3)->lineNo;
  return f;
}

// <typeDefinition> ::= TK_RECORD TK_RUID <fieldDefinitions> TK_ENDRECORD
//                    | TK_UNION TK_RUID <fieldDefinitions> TK_ENDUNION
// <fieldDefinitions> ::= <fieldDefinition> <fieldDefinition> <moreFields>
// <moreFields> ::= <fieldDefinition> <moreFields> | eps
static TypeDef *buildTypeDef(TreeNode *n) {
  TypeDef *t = xcalloc(1, sizeof(TypeDef));
  t->isUnion = isTok(kid(n, 0), TK_UNION);
  copyName(t->name, kid(n, 1)->lexeme);
  t->line = kid(n, 1)->lineNo;
  TreeNode *fds = kid(n, 2);
  FieldDef **tail = &t->fields;
  *tail = buildField(kid(fds, 0));
  tail = &(*tail)->next;
  *tail = buildField(kid(fds, 1));
  tail = &(*tail)->next;
  for (TreeNode *m = kid(fds, 2); !isEpsilon(m); m = kid(m, 1)) {
    *tail = buildField(kid(m, 0));
    tail = &(*tail)->next;
  }
  return t;
}

// <definetypestmt> ::= TK_DEFINETYPE <A> TK_RUID TK_AS TK_RUID
// <A> ::= TK_RECORD | TK_UNION
static AliasDef *buildAlias(TreeNode *n) {
  AliasDef *a = xcalloc(1, sizeof(AliasDef));
  a->isUnion = isTok(kid(kid(n, 1), 0), TK_UNION);
  copyName(a->target, kid(n, 2)->lexeme);
  copyName(a->alias, kid(n, 4)->lexeme);
  a->line = kid(n, 0)->lineNo;
  return a;
}

// <stmts> ::= <typeDefinitions> <declarations> <otherStmts> <returnStmt>
static void buildBody(TreeNode *n, Function *f) {
  // <typeDefinitions> ::= <actualOrRedefined> <typeDefinitions> | eps
  // <actualOrRedefined> ::= <typeDefinition> | <definetypestmt>
  TypeDef **tdTail = &f->typeDefs;
  AliasDef **adTail = &f->aliases;
  for (TreeNode *p = kid(n, 0); !isEpsilon(p); p = kid(p, 1)) {
    TreeNode *def = kid(kid(p, 0), 0);
    if (isNT(def, "typeDefinition")) {
      *tdTail = buildTypeDef(def);
      tdTail = &(*tdTail)->next;
    } else {
      *adTail = buildAlias(def);
      adTail = &(*adTail)->next;
    }
  }

  // <declarations> ::= <declaration> <declarations> | eps
  // <declaration> ::= TK_TYPE <dataType> TK_COLON TK_ID <global_or_not> TK_SEM
  // <global_or_not> ::= TK_COLON TK_GLOBAL | eps
  for (TreeNode *p = kid(n, 1); !isEpsilon(p); p = kid(p, 1)) {
    TreeNode *d = kid(p, 0);
    Decl *decl = xcalloc(1, sizeof(Decl));
    decl->type = buildTypeRef(kid(d, 1));
    copyName(decl->name, kid(d, 3)->lexeme);
    decl->line = kid(d, 3)->lineNo;
    decl->isGlobal = !isEpsilon(kid(d, 4));
    appendDecl(&f->decls, decl);
  }

  f->stmts = buildStmtList(NULL, kid(n, 2));

  // <returnStmt> ::= TK_RETURN <optionalReturn> TK_SEM
  // <optionalReturn> ::= TK_SQL <idList> TK_SQR | eps
  TreeNode *ret = kid(n, 3);
  f->returnLine = kid(ret, 0)->lineNo;
  TreeNode *opt = kid(ret, 1);
  if (!isEpsilon(opt))
    f->returns = buildIdList(kid(opt, 1));
}

// <function> ::= TK_FUNID <input_par> <output_par> TK_SEM <stmts> TK_END
// <input_par> ::= TK_INPUT TK_PARAMETER TK_LIST TK_SQL <parameter_list> TK_SQR
// <output_par> ::= TK_OUTPUT TK_PARAMETER TK_LIST TK_SQL <parameter_list>
//                  TK_SQR | eps
static Function *buildFunction(TreeNode *n) {
  Function *f = xcalloc(1, sizeof(Function));
  copyName(f->name, kid(n, 0)->lexeme);
  f->line = kid(n, 0)->lineNo;
  f->inputs = buildParams(kid(kid(n, 1), 4));
  TreeNode *out = kid(n, 2);
  if (!isEpsilon(out))
    f->outputs = buildParams(kid(out, 4));
  buildBody(kid(n, 4), f);
  f->endLine = kid(n, 5)->lineNo;
  return f;
}

// <program> ::= <otherFunctions> <mainFunction>
// <otherFunctions> ::= <function> <otherFunctions> | eps
// <mainFunction> ::= TK_MAIN <stmts> TK_END
Program *buildAST(Grammar *g, TreeNode *root) {
  G = g;
  Program *p = xcalloc(1, sizeof(Program));
  Function **tail = &p->functions;
  for (TreeNode *o = kid(root, 0); !isEpsilon(o); o = kid(o, 1)) {
    *tail = buildFunction(kid(o, 0));
    tail = &(*tail)->next;
  }
  TreeNode *m = kid(root, 1);
  Function *mainF = xcalloc(1, sizeof(Function));
  copyName(mainF->name, "_main");
  mainF->isMain = 1;
  mainF->line = kid(m, 0)->lineNo;
  buildBody(kid(m, 1), mainF);
  mainF->endLine = kid(m, 2)->lineNo;
  *tail = mainF;
  return p;
}

/* -------------------------------------------------------------- freeing */

static void freeExpr(Expr *e) {
  if (!e)
    return;
  freeExpr(e->left);
  freeExpr(e->right);
  free(e->var.fields);
  free(e);
}

static void freeBool(BoolExpr *b) {
  if (!b)
    return;
  freeExpr(b->lhs);
  freeExpr(b->rhs);
  freeBool(b->left);
  freeBool(b->right);
  free(b);
}

static void freeStmts(Stmt *s) {
  while (s) {
    Stmt *next = s->next;
    free(s->lhs.fields);
    freeExpr(s->rhs);
    freeBool(s->cond);
    freeStmts(s->body);
    freeStmts(s->elseBody);
    freeExpr(s->ioArg);
    free(s->outs.ids);
    free(s->ins.ids);
    free(s);
    s = next;
  }
}

static void freeDecls(Decl *d) {
  while (d) {
    Decl *n = d->next;
    free(d);
    d = n;
  }
}

void freeAST(Program *p) {
  if (!p)
    return;
  Function *f = p->functions;
  while (f) {
    Function *nf = f->next;
    freeDecls(f->inputs);
    freeDecls(f->outputs);
    freeDecls(f->decls);
    for (TypeDef *t = f->typeDefs; t;) {
      TypeDef *nt = t->next;
      for (FieldDef *fd = t->fields; fd;) {
        FieldDef *nfd = fd->next;
        free(fd);
        fd = nfd;
      }
      free(t);
      t = nt;
    }
    for (AliasDef *a = f->aliases; a;) {
      AliasDef *na = a->next;
      free(a);
      a = na;
    }
    freeStmts(f->stmts);
    free(f->returns.ids);
    free(f);
    f = nf;
  }
  free(p);
}

/* -------------------------------------------------------------- printing */

const char *relopToString(TokenType t) {
  switch (t) {
  case TK_LT: return "<";
  case TK_LE: return "<=";
  case TK_EQ: return "==";
  case TK_GT: return ">";
  case TK_GE: return ">=";
  case TK_NE: return "!=";
  default: return "?";
  }
}

const char *arithopToString(TokenType t) {
  switch (t) {
  case TK_PLUS: return "+";
  case TK_MINUS: return "-";
  case TK_MUL: return "*";
  case TK_DIV: return "/";
  default: return "?";
  }
}

static void indent(FILE *out, int depth) {
  for (int i = 0; i < depth; i++)
    fputs("  ", out);
}

static void printTypeRef(FILE *out, AstTypeRef *t) {
  if (t->kind == TREF_INT)
    fputs("int", out);
  else if (t->kind == TREF_REAL)
    fputs("real", out);
  else if (t->keyword == TK_RECORD)
    fprintf(out, "record %s", t->name);
  else if (t->keyword == TK_UNION)
    fprintf(out, "union %s", t->name);
  else
    fputs(t->name, out);
}

static void printVarRef(FILE *out, AstVarRef *v) {
  fputs(v->name, out);
  for (int i = 0; i < v->numFields; i++)
    fprintf(out, ".%s", v->fields[i]);
}

// expressions are printed fully parenthesised, which makes precedence and
// associativity visible
static void printExpr(FILE *out, Expr *e) {
  switch (e->kind) {
  case EXPR_NUM:
  case EXPR_RNUM:
    fputs(e->text, out);
    break;
  case EXPR_VAR:
    printVarRef(out, &e->var);
    break;
  case EXPR_BINOP:
    fputc('(', out);
    printExpr(out, e->left);
    fprintf(out, " %s ", arithopToString(e->op));
    printExpr(out, e->right);
    fputc(')', out);
    break;
  }
}

static void printBool(FILE *out, BoolExpr *b) {
  switch (b->kind) {
  case BOOL_REL:
    printExpr(out, b->lhs);
    fprintf(out, " %s ", relopToString(b->relop));
    printExpr(out, b->rhs);
    break;
  case BOOL_NOT:
    fputs("~(", out);
    printBool(out, b->left);
    fputc(')', out);
    break;
  default:
    fputc('(', out);
    printBool(out, b->left);
    fputs(b->kind == BOOL_AND ? ") &&& (" : ") @@@ (", out);
    printBool(out, b->right);
    fputc(')', out);
  }
}

static void printIdList(FILE *out, IdList *l) {
  fputc('[', out);
  for (int i = 0; i < l->count; i++)
    fprintf(out, "%s%s", i ? ", " : "", l->ids[i].name);
  fputc(']', out);
}

static void printStmts(FILE *out, Stmt *s, int depth) {
  for (; s; s = s->next) {
    indent(out, depth);
    switch (s->kind) {
    case STMT_ASSIGN:
      fprintf(out, "Assign (line %d): ", s->line);
      printVarRef(out, &s->lhs);
      fputs(" <--- ", out);
      printExpr(out, s->rhs);
      fputc('\n', out);
      break;
    case STMT_READ:
    case STMT_WRITE:
      fprintf(out, "%s (line %d): ", s->kind == STMT_READ ? "Read" : "Write",
              s->line);
      printExpr(out, s->ioArg);
      fputc('\n', out);
      break;
    case STMT_CALL:
      fprintf(out, "Call (line %d): ", s->line);
      if (s->outs.count) {
        printIdList(out, &s->outs);
        fputs(" <--- ", out);
      }
      fprintf(out, "%s with ", s->funName);
      printIdList(out, &s->ins);
      fputc('\n', out);
      break;
    case STMT_WHILE:
      fprintf(out, "While (line %d): ", s->line);
      printBool(out, s->cond);
      fputc('\n', out);
      printStmts(out, s->body, depth + 1);
      break;
    case STMT_IF:
      fprintf(out, "If (line %d): ", s->line);
      printBool(out, s->cond);
      fputc('\n', out);
      indent(out, depth);
      fputs("Then:\n", out);
      printStmts(out, s->body, depth + 1);
      if (s->elseBody) {
        indent(out, depth);
        fputs("Else:\n", out);
        printStmts(out, s->elseBody, depth + 1);
      }
      break;
    }
  }
}

static void printDecls(FILE *out, const char *label, Decl *d, int depth) {
  for (; d; d = d->next) {
    indent(out, depth);
    fprintf(out, "%s %s : ", label, d->name);
    printTypeRef(out, &d->type);
    if (d->isGlobal)
      fputs(" (global)", out);
    fprintf(out, " (line %d)\n", d->line);
  }
}

void printAST(Program *p, FILE *out) {
  fputs("Program\n", out);
  for (Function *f = p->functions; f; f = f->next) {
    fprintf(out, "  Function %s (line %d)\n", f->name, f->line);
    printDecls(out, "Input", f->inputs, 2);
    printDecls(out, "Output", f->outputs, 2);
    for (TypeDef *t = f->typeDefs; t; t = t->next) {
      fprintf(out, "    %s %s (line %d)\n", t->isUnion ? "Union" : "Record",
              t->name, t->line);
      for (FieldDef *fd = t->fields; fd; fd = fd->next) {
        fprintf(out, "      Field %s : ", fd->name);
        printTypeRef(out, &fd->type);
        fputc('\n', out);
      }
    }
    for (AliasDef *a = f->aliases; a; a = a->next)
      fprintf(out, "    Definetype %s %s as %s (line %d)\n",
              a->isUnion ? "union" : "record", a->target, a->alias, a->line);
    printDecls(out, "Declare", f->decls, 2);
    printStmts(out, f->stmts, 2);
    fprintf(out, "    Return (line %d): ", f->returnLine);
    printIdList(out, &f->returns);
    fputc('\n', out);
  }
}
