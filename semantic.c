// Group 51
// Ashutosh Desai - 2023A7PS0675P
// Anushka Doshi - 2023A7PS0597P
// Aarya Jain - 2023A7PS0618P
// Devansh Agarwal - 2023A7PS0570P
//
// Semantic analysis runs in this order:
//   1. collect every record/union definition (types are global, so a type may
//      be used in a function that appears before its definition)
//   2. resolve definetype aliases (an alias may name a later alias)
//   3. lay out records/unions (field offsets, sizes, recursion check)
//   4. collect global variables from every function
//   5. collect function signatures and locals, assign stack offsets
//   6. type check every function body, in source order
#include "semantic.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------- errors */

typedef struct {
  int line;
  int seq; // keeps the sort stable
  char msg[320];
} SemError;

static SemError *errs;
static int numErrs, capErrs;

static void semError(int line, const char *fmt, ...) {
  if (numErrs == capErrs) {
    capErrs = capErrs ? capErrs * 2 : 16;
    errs = realloc(errs, sizeof(SemError) * capErrs);
  }
  SemError *e = &errs[numErrs];
  e->line = line;
  e->seq = numErrs;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e->msg, sizeof(e->msg), fmt, ap);
  va_end(ap);
  numErrs++;
}

static int cmpErr(const void *a, const void *b) {
  const SemError *x = a, *y = b;
  if (x->line != y->line)
    return x->line - y->line;
  return x->seq - y->seq;
}

/* ---------------------------------------------------------------- context */

static SymbolTable *st;
static FuncEntry *cur; // function whose body is being checked

// names already reported as undeclared in the current function, so that a
// missing declaration produces one error instead of one per use
static char (*undeclared)[AST_NAME_LEN];
static int numUndeclared, capUndeclared;

// A piece of a variable: bytes [offset, offset + size) of `var`. Writing
// d2.x and reading d2.y touch different pieces of the same variable.
typedef struct {
  VarEntry *var;
  int offset, size;
} Access;

typedef struct {
  Access *items;
  int n, cap;
} AccessSet;

static void setAdd(AccessSet *s, VarEntry *v, int offset, int size) {
  if (!v)
    return;
  if (s->n == s->cap) {
    s->cap = s->cap ? s->cap * 2 : 8;
    s->items = realloc(s->items, sizeof(Access) * s->cap);
  }
  s->items[s->n++] = (Access){v, offset, size};
}

static void setAddRef(AccessSet *s, AstVarRef *r) {
  if (!r->entry)
    return;
  if (r->type)
    setAdd(s, r->entry, r->offset, r->type->size);
  else // a field that failed to resolve (already reported): assume the
       // whole variable may change, so no follow-on error is reported
    setAdd(s, r->entry, 0, r->entry->type->size > 0 ? r->entry->type->size
                                                     : SCALAR_SIZE);
}

static void setAddWhole(AccessSet *s, VarEntry *v) {
  if (v)
    setAdd(s, v, 0, v->type->size);
}

// does any access in s overlap a?
static int setOverlaps(AccessSet *s, Access *a) {
  for (int i = 0; i < s->n; i++) {
    Access *b = &s->items[i];
    if (b->var == a->var && b->offset < a->offset + a->size &&
        a->offset < b->offset + b->size)
      return 1;
  }
  return 0;
}

// does s write any part of v?
static int setTouches(AccessSet *s, VarEntry *v) {
  for (int i = 0; i < s->n; i++)
    if (s->items[i].var == v)
      return 1;
  return 0;
}

static int round16(int n) { return (n + 15) / 16 * 16; }

/* --------------------------------------------------------- types & layout */

static int definitionLine(const char *name) {
  for (int i = 0; i < st->numTypes; i++)
    if (strcmp(st->types[i]->name, name) == 0)
      return st->types[i]->line;
  TypeAlias *a = findAlias(st, name);
  return a ? a->line : 0;
}

static void collectTypes(Program *p) {
  for (Function *f = p->functions; f; f = f->next)
    for (TypeDef *td = f->typeDefs; td; td = td->next) {
      if (findType(st, td->name)) {
        semError(td->line, "type %s is already defined at line %d", td->name,
                 definitionLine(td->name));
        continue;
      }
      Type *t = addType(st, td->isUnion ? TY_UNION : TY_RECORD, td->name,
                        td->line);
      t->def = td;
    }
}

static void collectAliases(Program *p) {
  int count = 0;
  for (Function *f = p->functions; f; f = f->next)
    for (AliasDef *a = f->aliases; a; a = a->next)
      count++;
  if (count == 0)
    return;
  AliasDef **pending = calloc(count, sizeof(AliasDef *));
  int n = 0;
  for (Function *f = p->functions; f; f = f->next)
    for (AliasDef *a = f->aliases; a; a = a->next)
      pending[n++] = a;

  // An alias may refer to another alias defined later, so keep resolving
  // until a full pass makes no progress.
  int progress = 1;
  while (progress) {
    progress = 0;
    for (int i = 0; i < n; i++) {
      AliasDef *a = pending[i];
      if (!a)
        continue;
      if (findType(st, a->alias)) {
        semError(a->line, "type name %s is already defined at line %d",
                 a->alias, definitionLine(a->alias));
        pending[i] = NULL;
        progress = 1;
        continue;
      }
      Type *t = findType(st, a->target);
      if (!t)
        continue;
      if ((t->kind == TY_UNION) != a->isUnion)
        semError(a->line, "definetype %s %s: %s is a %s",
                 a->isUnion ? "union" : "record", a->target, a->target,
                 t->kind == TY_UNION ? "union" : "record");
      else
        addAlias(st, a->alias, t, a->line);
      pending[i] = NULL;
      progress = 1;
    }
  }
  for (int i = 0; i < n; i++)
    if (pending[i])
      semError(pending[i]->line, "undefined type %s in definetype",
               pending[i]->target);
  free(pending);
}

static void layoutType(Type *t) {
  if (t->layoutState == 2)
    return;
  t->layoutState = 1;

  int count = 0;
  for (FieldDef *fd = t->def->fields; fd; fd = fd->next)
    count++;
  t->fields = calloc(count, sizeof(FieldInfo));

  long long offset = 0, size = 0; // wide enough not to overflow before the
                                  // MAX_TYPE_SIZE check below
  for (FieldDef *fd = t->def->fields; fd; fd = fd->next) {
    if (findField(t, fd->name)) {
      semError(fd->line, "duplicate field %s in %s", fd->name, t->name);
      continue;
    }
    Type *ft;
    if (fd->type.kind == TREF_INT)
      ft = st->intType;
    else if (fd->type.kind == TREF_REAL)
      ft = st->realType;
    else {
      ft = findType(st, fd->type.name);
      if (!ft) {
        semError(fd->line, "undefined type %s for field %s of %s",
                 fd->type.name, fd->name, t->name);
        ft = st->errorType;
      } else if (ft->layoutState == 1) {
        semError(fd->line,
                 "recursive type: field %s of %s has type %s, which "
                 "contains %s",
                 fd->name, t->name, ft->name, t->name);
        ft = st->errorType;
      } else {
        layoutType(ft);
      }
    }
    FieldInfo *fi = &t->fields[t->numFields++];
    snprintf(fi->name, AST_NAME_LEN, "%s", fd->name);
    fi->type = ft;
    fi->line = fd->line;
    if (t->kind == TY_RECORD) {
      fi->offset = offset > MAX_TYPE_SIZE ? MAX_TYPE_SIZE : (int)offset;
      offset += ft->size;
      size = offset;
    } else {
      fi->offset = 0;
      if (ft->size > size)
        size = ft->size;
    }
    if (ft->hasUnion)
      t->hasUnion = 1;
  }
  if (t->kind == TY_UNION)
    t->hasUnion = 1;
  if (size > MAX_TYPE_SIZE) {
    semError(t->line, "type %s is too large (%lld bytes; the limit is %d)",
             t->name, size, MAX_TYPE_SIZE);
    size = SCALAR_SIZE; // keeps types that contain it from overflowing too
  }
  t->size = (int)size;
  t->layoutState = 2;
}

// type of a declaration or parameter
static Type *resolveTypeRef(AstTypeRef *r) {
  if (r->kind == TREF_INT)
    return st->intType;
  if (r->kind == TREF_REAL)
    return st->realType;
  Type *t = findType(st, r->name);
  if (!t) {
    semError(r->line, "undefined type %s", r->name);
    return st->errorType;
  }
  if (r->keyword == TK_RECORD && t->kind != TY_RECORD) {
    semError(r->line, "%s is a union, not a record", r->name);
    return st->errorType;
  }
  if (r->keyword == TK_UNION && t->kind != TY_UNION) {
    semError(r->line, "%s is a record, not a union", r->name);
    return st->errorType;
  }
  return t;
}

// variables may not have a union type directly: a union is only usable as a
// field of a (variant) record
static Type *variableType(Decl *d) {
  Type *t = resolveTypeRef(&d->type);
  if (t->kind == TY_UNION) {
    semError(d->line,
             "variable %s cannot have union type %s; a union must be a "
             "field of a record",
             d->name, t->name);
    return st->errorType;
  }
  return t;
}

/* ------------------------------------------------------ variables & funcs */

static VarEntry *newVar(Decl *d, Type *t, VarKind kind, FuncEntry *owner) {
  VarEntry *v = calloc(1, sizeof(VarEntry));
  snprintf(v->name, AST_NAME_LEN, "%s", d->name);
  v->type = t;
  v->kind = kind;
  v->line = d->line;
  v->owner = owner;
  return v;
}

static void collectGlobals(Program *p) {
  VarEntry **tail = &st->globals;
  for (Function *f = p->functions; f; f = f->next)
    for (Decl *d = f->decls; d; d = d->next) {
      if (!d->isGlobal)
        continue;
      Type *t = variableType(d);
      VarEntry *old = findGlobal(st, d->name);
      if (old) {
        semError(d->line, "global variable %s is already declared at line %d",
                 d->name, old->line);
        continue;
      }
      *tail = newVar(d, t, VAR_GLOBAL, NULL);
      tail = &(*tail)->next;
    }
}

// adds a parameter or local to f; returns NULL if the name is taken
static VarEntry *declareInFunction(FuncEntry *f, Decl *d, VarKind kind) {
  Type *t = variableType(d);
  VarEntry *g = findGlobal(st, d->name);
  if (g) {
    semError(d->line, "%s is already declared as a global variable at line %d",
             d->name, g->line);
    return NULL;
  }
  VarEntry *old = findLocal(f, d->name);
  if (old) {
    semError(d->line, "variable %s is already declared in %s at line %d",
             d->name, f->name, old->line);
    return NULL;
  }
  VarEntry *v = newVar(d, t, kind, f);
  VarEntry **tail = &f->vars;
  while (*tail)
    tail = &(*tail)->next;
  *tail = v;
  return v;
}

static void collectParams(FuncEntry *fe, Decl *list, VarKind kind,
                          VarEntry ***arr, int *count, int *offset) {
  int n = 0;
  for (Decl *d = list; d; d = d->next)
    n++;
  *arr = calloc(n > 0 ? n : 1, sizeof(VarEntry *));
  *count = 0;
  for (Decl *d = list; d; d = d->next) {
    // a rejected parameter stays as NULL so positions still line up with the
    // signature when calls are checked
    VarEntry *v = declareInFunction(fe, d, kind);
    (*arr)[(*count)++] = v;
    if (!v)
      continue;
    if (*offset + v->type->size > MAX_FRAME_SIZE) {
      semError(d->line, "the parameters of %s need more than %d bytes",
               fe->name, MAX_FRAME_SIZE);
      v->type = st->errorType; // stop counting, report once
      continue;
    }
    v->offset = *offset;
    *offset += v->type->size;
  }
}

static void collectFunctions(Program *p) {
  for (Function *f = p->functions; f; f = f->next) {
    FuncEntry *old = findFunc(st, f->name);
    if (old)
      semError(f->line, "function %s is already defined at line %d", f->name,
               old->line);
    FuncEntry *fe = addFunc(st, f->name, f->line);
    fe->ast = f;
    fe->isMain = f->isMain;

    int offset = 0;
    collectParams(fe, f->inputs, VAR_INPUT, &fe->inputs, &fe->numInputs,
                  &offset);
    fe->inSize = offset;
    collectParams(fe, f->outputs, VAR_OUTPUT, &fe->outputs, &fe->numOutputs,
                  &offset);
    fe->paramSize = round16(offset);

    int local = 0, tooBig = 0;
    for (Decl *d = f->decls; d; d = d->next) {
      if (d->isGlobal)
        continue;
      VarEntry *v = declareInFunction(fe, d, VAR_LOCAL);
      if (!v)
        continue;
      if (local + v->type->size > MAX_FRAME_SIZE) {
        if (!tooBig)
          semError(d->line, "the local variables of %s need more than %d "
                            "bytes",
                   fe->name, MAX_FRAME_SIZE);
        tooBig = 1;
        v->type = st->errorType;
        continue;
      }
      local += v->type->size;
      v->offset = local;
    }
    fe->localSize = round16(local);
  }
}

/* ------------------------------------------------------------ expressions */

static void varText(AstVarRef *v, char *buf, size_t n) {
  snprintf(buf, n, "%s", v->name);
  for (int i = 0; i < v->numFields; i++) {
    size_t len = strlen(buf);
    snprintf(buf + len, n - len, ".%s", v->fields[i]);
  }
}

static Type *checkVarRef(AstVarRef *v) {
  VarEntry *e = lookupVar(st, cur, v->name);
  if (!e) {
    for (int i = 0; i < numUndeclared; i++)
      if (strcmp(undeclared[i], v->name) == 0)
        return st->errorType;
    if (numUndeclared == capUndeclared) {
      capUndeclared = capUndeclared ? capUndeclared * 2 : 8;
      undeclared = realloc(undeclared, AST_NAME_LEN * capUndeclared);
    }
    strcpy(undeclared[numUndeclared++], v->name);
    semError(v->line, "variable %s is not declared", v->name);
    return st->errorType;
  }
  v->entry = e;
  Type *t = e->type;
  int offset = 0;
  for (int i = 0; i < v->numFields; i++) {
    if (t->kind == TY_ERROR)
      return t;
    char path[256];
    snprintf(path, sizeof(path), "%s", v->name);
    for (int j = 0; j < i; j++) {
      size_t len = strlen(path);
      snprintf(path + len, sizeof(path) - len, ".%s", v->fields[j]);
    }
    if (!isAggregate(t)) {
      semError(v->line, "%s has type %s and has no field %s", path,
               t->name, v->fields[i]);
      return st->errorType;
    }
    FieldInfo *fi = findField(t, v->fields[i]);
    if (!fi) {
      semError(v->line, "%s (type %s) has no field named %s", path, t->name,
               v->fields[i]);
      return st->errorType;
    }
    offset += fi->offset;
    t = fi->type;
  }
  v->type = t;
  v->offset = offset;
  return t;
}

static int hasIntLeaf(Type *t) {
  if (t->kind == TY_INT)
    return 1;
  if (isAggregate(t))
    for (int i = 0; i < t->numFields; i++)
      if (hasIntLeaf(t->fields[i].type))
        return 1;
  return 0;
}

static int checkIntLiteral(Expr *e) {
  errno = 0;
  long long v = strtoll(e->text, NULL, 10);
  if (errno == ERANGE) {
    semError(e->line, "integer constant %s is too large", e->text);
    return 0;
  }
  e->ival = v;
  return 1;
}

static Type *recordScalar(Expr *e, Type *rec, Type *scalar) {
  if (rec->hasUnion) {
    semError(e->line,
             "arithmetic is not allowed on %s because it contains a union",
             rec->name);
    return st->errorType;
  }
  if (scalar->kind == TY_REAL && hasIntLeaf(rec)) {
    semError(e->line,
             "cannot %s %s by a real value because it has int fields",
             e->op == TK_MUL ? "multiply" : "divide", rec->name);
    return st->errorType;
  }
  return rec;
}

static Type *checkExpr(Expr *e) {
  Type *t = st->errorType;
  switch (e->kind) {
  case EXPR_NUM:
    t = checkIntLiteral(e) ? st->intType : st->errorType;
    break;
  case EXPR_RNUM:
    t = st->realType;
    break;
  case EXPR_VAR: {
    t = checkVarRef(&e->var);
    if (t->kind == TY_UNION) {
      char buf[256];
      varText(&e->var, buf, sizeof(buf));
      semError(e->line,
               "union %s cannot be used as a value; use one of its fields",
               buf);
      t = st->errorType;
    }
    break;
  }
  case EXPR_BINOP: {
    Type *l = checkExpr(e->left);
    Type *r = checkExpr(e->right);
    const char *op = arithopToString(e->op);
    if (l->kind == TY_ERROR || r->kind == TY_ERROR) {
      t = st->errorType;
    } else if (isScalar(l) && isScalar(r)) {
      t = (l->kind == TY_INT && r->kind == TY_INT) ? st->intType
                                                   : st->realType;
    } else if (e->op == TK_PLUS || e->op == TK_MINUS) {
      if (l == r) {
        if (l->hasUnion) {
          semError(e->line,
                   "arithmetic is not allowed on %s because it contains a "
                   "union",
                   l->name);
          t = st->errorType;
        } else {
          t = l;
        }
      } else {
        semError(e->line, "operator %s cannot be applied to %s and %s", op,
                 l->name, r->name);
      }
    } else if (e->op == TK_MUL) {
      if (isAggregate(l) && isScalar(r))
        t = recordScalar(e, l, r);
      else if (isScalar(l) && isAggregate(r))
        t = recordScalar(e, r, l);
      else
        semError(e->line, "operator * cannot be applied to %s and %s",
                 l->name, r->name);
    } else { // TK_DIV
      if (isAggregate(l) && isScalar(r))
        t = recordScalar(e, l, r);
      else
        semError(e->line, "operator / cannot be applied to %s and %s",
                 l->name, r->name);
    }
    break;
  }
  }
  e->type = t;
  return t;
}

static void checkBool(BoolExpr *b) {
  switch (b->kind) {
  case BOOL_REL: {
    Type *l = checkExpr(b->lhs);
    Type *r = checkExpr(b->rhs);
    if (l->kind == TY_ERROR || r->kind == TY_ERROR)
      return;
    if (!isScalar(l) || !isScalar(r))
      semError(b->line,
               "relational operator %s needs int or real operands, not %s "
               "and %s",
               relopToString(b->relop), l->name, r->name);
    break;
  }
  case BOOL_NOT:
    checkBool(b->left);
    break;
  default:
    checkBool(b->left);
    checkBool(b->right);
  }
}

/* ------------------------------------------------------------- statements */

static int assignable(Type *dst, Type *src) {
  if (dst->kind == TY_ERROR || src->kind == TY_ERROR)
    return 1;
  if (dst == src)
    return 1;
  return dst->kind == TY_REAL && src->kind == TY_INT;
}

// read/write work on scalars and on records made only of scalars/records
static int isIOType(Type *t) {
  return t->kind == TY_ERROR || isScalar(t) ||
         (t->kind == TY_RECORD && !t->hasUnion);
}

static VarEntry *checkId(AstId *id) {
  AstVarRef v;
  memset(&v, 0, sizeof(v));
  strcpy(v.name, id->name);
  v.line = id->line;
  checkVarRef(&v);
  id->entry = v.entry;
  return v.entry;
}

static void checkCall(Stmt *s) {
  FuncEntry *callee = findFunc(st, s->funName);
  if (!callee)
    semError(s->line, "function %s is not defined", s->funName);
  else if (callee == cur || strcmp(callee->name, cur->name) == 0)
    semError(s->line, "function %s cannot call itself (recursion is not "
                      "allowed)",
             s->funName);
  else if (callee->index > cur->index)
    semError(s->line, "function %s is called before it is defined",
             s->funName);
  s->callee = callee;

  for (int i = 0; i < s->ins.count; i++)
    checkId(&s->ins.ids[i]);
  for (int i = 0; i < s->outs.count; i++) {
    VarEntry *v = checkId(&s->outs.ids[i]);
    int earlier = 0;
    for (int j = 0; v && j < i; j++)
      earlier += s->outs.ids[j].entry == v;
    if (earlier == 1) // report each variable once, at its second use
      semError(s->line, "%s receives more than one result of %s", v->name,
               s->funName);
  }
  if (!callee)
    return;

  if (s->ins.count != callee->numInputs) {
    semError(s->line,
             "function %s expects %d input parameter(s), but %d given",
             s->funName, callee->numInputs, s->ins.count);
  } else {
    for (int i = 0; i < s->ins.count; i++) {
      VarEntry *actual = s->ins.ids[i].entry, *formal = callee->inputs[i];
      if (actual && formal && actual->type->kind != TY_ERROR &&
          formal->type->kind != TY_ERROR && actual->type != formal->type)
        semError(s->line,
                 "input parameter %d of %s must have type %s, but %s has "
                 "type %s",
                 i + 1, s->funName, formal->type->name, actual->name,
                 actual->type->name);
    }
  }

  if (s->outs.count != callee->numOutputs) {
    semError(s->line, "function %s returns %d value(s), but %d variable(s) "
                      "receive them",
             s->funName, callee->numOutputs, s->outs.count);
  } else {
    for (int i = 0; i < s->outs.count; i++) {
      VarEntry *actual = s->outs.ids[i].entry, *formal = callee->outputs[i];
      if (actual && formal && actual->type->kind != TY_ERROR &&
          formal->type->kind != TY_ERROR && actual->type != formal->type)
        semError(s->line,
                 "output parameter %d of %s has type %s, but %s has type %s",
                 i + 1, s->funName, formal->type->name, actual->name,
                 actual->type->name);
    }
  }
}

// every piece of a variable that a statement list may change
static void collectAssigned(Stmt *s, AccessSet *set) {
  for (; s; s = s->next) {
    switch (s->kind) {
    case STMT_ASSIGN:
      setAddRef(set, &s->lhs);
      break;
    case STMT_READ:
      if (s->ioArg->kind == EXPR_VAR)
        setAddRef(set, &s->ioArg->var);
      break;
    case STMT_CALL:
      for (int i = 0; i < s->outs.count; i++)
        setAddWhole(set, s->outs.ids[i].entry);
      break;
    case STMT_WHILE:
    case STMT_IF:
      collectAssigned(s->body, set);
      collectAssigned(s->elseBody, set);
      break;
    case STMT_WRITE:
      break;
    }
  }
}

static void collectCondVars(BoolExpr *b, AccessSet *set) {
  if (!b)
    return;
  if (b->kind == BOOL_REL) {
    if (b->lhs->kind == EXPR_VAR)
      setAddRef(set, &b->lhs->var);
    if (b->rhs->kind == EXPR_VAR)
      setAddRef(set, &b->rhs->var);
    return;
  }
  collectCondVars(b->left, set);
  collectCondVars(b->right, set);
}

static int condHasUndeclared(BoolExpr *b) {
  if (!b)
    return 0;
  if (b->kind == BOOL_REL)
    return (b->lhs->kind == EXPR_VAR && !b->lhs->var.type) ||
           (b->rhs->kind == EXPR_VAR && !b->rhs->var.type);
  return condHasUndeclared(b->left) || condHasUndeclared(b->right);
}

static void checkStmts(Stmt *s);

static void checkStmt(Stmt *s) {
  char buf[256];
  switch (s->kind) {
  case STMT_ASSIGN: {
    Type *lt = checkVarRef(&s->lhs);
    Type *rt = st->errorType;
    if (s->rhs->depth > MAX_EXPR_DEPTH)
      semError(s->line, "expression is too long or too deeply nested (more "
                        "than %d operators deep)",
               MAX_EXPR_DEPTH);
    else
      rt = checkExpr(s->rhs);
    varText(&s->lhs, buf, sizeof(buf));
    if (lt->kind == TY_UNION) {
      semError(s->line, "cannot assign to union %s; assign one of its fields",
               buf);
    } else if (!assignable(lt, rt)) {
      semError(s->line, "type mismatch: cannot assign a value of type %s to "
                        "%s of type %s",
               rt->name, buf, lt->name);
    }
    break;
  }
  case STMT_READ:
  case STMT_WRITE: {
    const char *what = s->kind == STMT_READ ? "read" : "write";
    if (s->ioArg->kind != EXPR_VAR) {
      if (s->kind == STMT_READ)
        semError(s->line, "read needs a variable, not the constant %s",
                 s->ioArg->text);
      else
        checkExpr(s->ioArg);
      break;
    }
    Type *t = checkVarRef(&s->ioArg->var);
    s->ioArg->type = t;
    if (!isIOType(t)) {
      varText(&s->ioArg->var, buf, sizeof(buf));
      semError(s->line, "cannot %s %s of type %s%s", what, buf, t->name,
               t->kind == TY_RECORD ? " because it contains a union" : "");
    }
    break;
  }
  case STMT_CALL:
    checkCall(s);
    break;
  case STMT_IF:
    checkBool(s->cond);
    checkStmts(s->body);
    checkStmts(s->elseBody);
    break;
  case STMT_WHILE: {
    checkBool(s->cond);
    checkStmts(s->body);
    // The loop can only terminate if the body changes something the
    // condition reads: the same variable, and for a record field an
    // overlapping part of it (writing d2.x does not update d2.y). Skipped
    // when an operand failed to resolve (already reported).
    if (!condHasUndeclared(s->cond)) {
      AccessSet condVars = {0}, changed = {0};
      collectCondVars(s->cond, &condVars);
      collectAssigned(s->body, &changed);
      int updated = 0;
      for (int i = 0; i < condVars.n; i++)
        if (setOverlaps(&changed, &condVars.items[i]))
          updated = 1;
      if (!updated)
        semError(s->line, "none of the variables in the while condition is "
                          "updated inside the loop");
      free(condVars.items);
      free(changed.items);
    }
    break;
  }
  }
}

static void checkStmts(Stmt *s) {
  for (; s; s = s->next)
    checkStmt(s);
}

static void checkReturn(FuncEntry *fe) {
  Function *f = fe->ast;
  for (int i = 0; i < f->returns.count; i++)
    checkId(&f->returns.ids[i]);

  if (fe->isMain) {
    if (f->returns.count > 0)
      semError(f->returnLine, "_main cannot return values");
    return;
  }
  if (f->returns.count != fe->numOutputs) {
    semError(f->returnLine,
             "function %s must return %d value(s) (its output parameters), "
             "but returns %d",
             fe->name, fe->numOutputs, f->returns.count);
    return;
  }

  AccessSet assigned = {0};
  collectAssigned(f->stmts, &assigned);
  for (int i = 0; i < f->returns.count; i++) {
    VarEntry *v = f->returns.ids[i].entry, *formal = fe->outputs[i];
    if (!v)
      continue;
    if (formal && v->type->kind != TY_ERROR &&
        formal->type->kind != TY_ERROR && v->type != formal->type)
      semError(f->returnLine,
               "returned variable %s has type %s, but output parameter %s "
               "has type %s",
               v->name, v->type->name, formal->name, formal->type->name);
    if ((v->kind == VAR_OUTPUT || v->kind == VAR_LOCAL) &&
        !setTouches(&assigned, v))
      semError(f->returnLine,
               "%s is returned by %s but is never assigned a value", v->name,
               fe->name);
  }
  free(assigned.items);
}

/* ------------------------------------------------------------ stack use */

// Bytes a statement list may push temporarily: record assignments stage
// every field on the stack, and expressions keep one slot per level.
static long long tempStack(Stmt *s) {
  long long most = 8LL * (MAX_EXPR_DEPTH + 2);
  for (; s; s = s->next) {
    if (s->kind == STMT_ASSIGN && s->lhs.type && isAggregate(s->lhs.type) &&
        s->lhs.type->size > most)
      most = s->lhs.type->size;
    long long inner = tempStack(s->body), other = tempStack(s->elseBody);
    if (inner > most)
      most = inner;
    if (other > most)
      most = other;
  }
  return most;
}

// Largest parameter block plus callee stack use over the calls in s.
static long long callStack(Stmt *s, long long *use, int *calleeTooBig) {
  long long most = 0;
  for (; s; s = s->next) {
    if (s->kind == STMT_CALL && s->callee && s->callee->index < cur->index) {
      long long need = s->callee->paramSize + use[s->callee->index];
      if (need > most)
        most = need;
      if (use[s->callee->index] > MAX_STACK_USE)
        *calleeTooBig = 1;
    }
    long long inner = callStack(s->body, use, calleeTooBig);
    long long other = callStack(s->elseBody, use, calleeTooBig);
    if (inner > most)
      most = inner;
    if (other > most)
      most = other;
  }
  return most;
}

// Recursion is not allowed and callees come earlier in the file, so the
// deepest stack use of each function can be computed in source order:
// return address + saved rbp + locals + the larger of its temporaries and
// (parameter block + stack use) of any function it calls.
static void checkStackUse(void) {
  long long *use = calloc(st->numFuncs, sizeof(long long));
  for (int i = 0; i < st->numFuncs; i++) {
    cur = st->funcs[i];
    long long temp = tempStack(cur->ast->stmts);
    long long returned = 0;
    for (int k = 0; k < cur->numOutputs; k++)
      if (cur->outputs[k])
        returned += cur->outputs[k]->type->size;
    if (returned > temp)
      temp = returned;
    int calleeTooBig = 0;
    long long calls = callStack(cur->ast->stmts, use, &calleeTooBig);
    use[i] = 16 + cur->localSize + (calls > temp ? calls : temp);
    if (use[i] > MAX_STACK_USE && !calleeTooBig)
      semError(cur->line,
               "%s needs about %lld bytes of stack, counting the functions "
               "it calls; the limit is %d",
               cur->name, use[i], MAX_STACK_USE);
  }
  free(use);
}

/* ------------------------------------------------------------ entry point */

SymbolTable *semanticAnalysis(Program *p, int *numErrors, FILE *out) {
  errs = NULL;
  numErrs = capErrs = 0;
  st = newSymbolTable();

  collectTypes(p);
  collectAliases(p);
  for (int i = 0; i < st->numTypes; i++)
    layoutType(st->types[i]);
  collectGlobals(p);
  collectFunctions(p);

  for (int i = 0; i < st->numFuncs; i++) {
    cur = st->funcs[i];
    numUndeclared = 0;
    checkStmts(cur->ast->stmts);
    checkReturn(cur);
  }
  checkStackUse();
  free(undeclared);
  undeclared = NULL;
  numUndeclared = capUndeclared = 0;

  if (numErrs > 0)
    qsort(errs, numErrs, sizeof(SemError), cmpErr);
  if (out)
    for (int i = 0; i < numErrs; i++)
      fprintf(out, "[SEMANTIC-ERROR] at line %d: %s\n", errs[i].line,
              errs[i].msg);
  *numErrors = numErrs;
  free(errs);
  errs = NULL;
  cur = NULL;
  SymbolTable *result = st;
  st = NULL;
  return result;
}
