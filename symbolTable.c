// Group 51
// Ashutosh Desai - 2023A7PS0675P
// Anushka Doshi - 2023A7PS0597P
// Aarya Jain - 2023A7PS0618P
// Devansh Agarwal - 2023A7PS0570P
#include "symbolTable.h"
#include <stdlib.h>
#include <string.h>

static void *xmalloc0(size_t size) {
  void *p = calloc(1, size);
  if (!p) {
    fprintf(stderr, "out of memory\n");
    exit(2);
  }
  return p;
}

static void *grow(void *buf, int *cap, size_t elem) {
  *cap = *cap ? *cap * 2 : 8;
  void *p = realloc(buf, (size_t)*cap * elem);
  if (!p) {
    fprintf(stderr, "out of memory\n");
    exit(2);
  }
  return p;
}

static Type *newScalar(TypeKind kind, const char *name) {
  Type *t = xmalloc0(sizeof(Type));
  t->kind = kind;
  snprintf(t->name, AST_NAME_LEN, "%s", name);
  t->size = SCALAR_SIZE;
  t->layoutState = 2;
  return t;
}

SymbolTable *newSymbolTable(void) {
  SymbolTable *st = xmalloc0(sizeof(SymbolTable));
  st->intType = newScalar(TY_INT, "int");
  st->realType = newScalar(TY_REAL, "real");
  st->errorType = newScalar(TY_ERROR, "<error>");
  return st;
}

static void freeVars(VarEntry *v) {
  while (v) {
    VarEntry *n = v->next;
    free(v);
    v = n;
  }
}

void freeSymbolTable(SymbolTable *st) {
  if (!st)
    return;
  for (int i = 0; i < st->numTypes; i++) {
    free(st->types[i]->fields);
    free(st->types[i]);
  }
  free(st->types);
  free(st->aliases);
  freeVars(st->globals);
  for (int i = 0; i < st->numFuncs; i++) {
    FuncEntry *f = st->funcs[i];
    free(f->inputs);
    free(f->outputs);
    freeVars(f->vars);
    free(f);
  }
  free(st->funcs);
  free(st->intType);
  free(st->realType);
  free(st->errorType);
  free(st);
}

Type *addType(SymbolTable *st, TypeKind kind, const char *name, int line) {
  if (st->numTypes == st->capTypes)
    st->types = grow(st->types, &st->capTypes, sizeof(Type *));
  Type *t = xmalloc0(sizeof(Type));
  t->kind = kind;
  snprintf(t->name, AST_NAME_LEN, "%s", name);
  t->line = line;
  st->types[st->numTypes++] = t;
  return t;
}

void addAlias(SymbolTable *st, const char *name, Type *type, int line) {
  if (st->numAliases == st->capAliases)
    st->aliases = grow(st->aliases, &st->capAliases, sizeof(TypeAlias));
  TypeAlias *a = &st->aliases[st->numAliases++];
  memset(a, 0, sizeof(*a));
  snprintf(a->name, AST_NAME_LEN, "%s", name);
  a->type = type;
  a->line = line;
}

TypeAlias *findAlias(SymbolTable *st, const char *name) {
  for (int i = 0; i < st->numAliases; i++)
    if (strcmp(st->aliases[i].name, name) == 0)
      return &st->aliases[i];
  return NULL;
}

Type *findType(SymbolTable *st, const char *name) {
  for (int i = 0; i < st->numTypes; i++)
    if (strcmp(st->types[i]->name, name) == 0)
      return st->types[i];
  TypeAlias *a = findAlias(st, name);
  return a ? a->type : NULL;
}

FuncEntry *addFunc(SymbolTable *st, const char *name, int line) {
  if (st->numFuncs == st->capFuncs)
    st->funcs = grow(st->funcs, &st->capFuncs, sizeof(FuncEntry *));
  FuncEntry *f = xmalloc0(sizeof(FuncEntry));
  snprintf(f->name, AST_NAME_LEN, "%s", name);
  f->line = line;
  f->index = st->numFuncs;
  st->funcs[st->numFuncs++] = f;
  return f;
}

FuncEntry *findFunc(SymbolTable *st, const char *name) {
  for (int i = 0; i < st->numFuncs; i++)
    if (strcmp(st->funcs[i]->name, name) == 0)
      return st->funcs[i];
  return NULL;
}

VarEntry *findGlobal(SymbolTable *st, const char *name) {
  for (VarEntry *v = st->globals; v; v = v->next)
    if (strcmp(v->name, name) == 0)
      return v;
  return NULL;
}

VarEntry *findLocal(FuncEntry *f, const char *name) {
  if (!f)
    return NULL;
  for (VarEntry *v = f->vars; v; v = v->next)
    if (strcmp(v->name, name) == 0)
      return v;
  return NULL;
}

VarEntry *lookupVar(SymbolTable *st, FuncEntry *f, const char *name) {
  VarEntry *v = findLocal(f, name);
  return v ? v : findGlobal(st, name);
}

FieldInfo *findField(Type *t, const char *name) {
  if (!t || (t->kind != TY_RECORD && t->kind != TY_UNION))
    return NULL;
  for (int i = 0; i < t->numFields; i++)
    if (strcmp(t->fields[i].name, name) == 0)
      return &t->fields[i];
  return NULL;
}

int isScalar(Type *t) { return t->kind == TY_INT || t->kind == TY_REAL; }
int isAggregate(Type *t) {
  return t->kind == TY_RECORD || t->kind == TY_UNION;
}

const char *typeName(Type *t) { return t ? t->name : "?"; }

/* -------------------------------------------------------------- printing */

static const char *varKindName(VarKind k) {
  switch (k) {
  case VAR_GLOBAL: return "global";
  case VAR_LOCAL: return "local";
  case VAR_INPUT: return "input";
  case VAR_OUTPUT: return "output";
  }
  return "?";
}

static void printVarRow(FILE *out, VarEntry *v) {
  char where[48];
  if (v->kind == VAR_GLOBAL)
    snprintf(where, sizeof(where), "G_%s", v->name);
  else if (v->kind == VAR_LOCAL)
    snprintf(where, sizeof(where), "rbp-%d", v->offset);
  else
    snprintf(where, sizeof(where), "rbp+%d", 16 + v->offset);
  fprintf(out, "  %-22s %-8s %-22s %-6d %-14s %d\n", v->name,
          varKindName(v->kind), typeName(v->type), v->type->size, where,
          v->line);
}

void printSymbolTable(SymbolTable *st, FILE *out) {
  fprintf(out, "Types\n");
  fprintf(out, "  %-22s %-7s %-6s %s\n", "name", "kind", "size",
          "fields (name:type@offset)");
  for (int i = 0; i < st->numTypes; i++) {
    Type *t = st->types[i];
    fprintf(out, "  %-22s %-7s %-6d", t->name,
            t->kind == TY_UNION ? "union" : "record", t->size);
    for (int j = 0; j < t->numFields; j++)
      fprintf(out, " %s:%s@%d", t->fields[j].name,
              typeName(t->fields[j].type), t->fields[j].offset);
    fputc('\n', out);
  }
  for (int i = 0; i < st->numAliases; i++)
    fprintf(out, "  %-22s alias of %s\n", st->aliases[i].name,
            typeName(st->aliases[i].type));

  fprintf(out, "\n  %-22s %-8s %-22s %-6s %-14s %s\n", "name", "scope",
          "type", "size", "address", "line");
  fprintf(out, "Globals\n");
  for (VarEntry *v = st->globals; v; v = v->next)
    printVarRow(out, v);
  for (int i = 0; i < st->numFuncs; i++) {
    FuncEntry *f = st->funcs[i];
    fprintf(out, "Function %s (locals %d bytes, parameter block %d bytes)\n",
            f->name, f->localSize, f->paramSize);
    for (VarEntry *v = f->vars; v; v = v->next)
      printVarRow(out, v);
  }
}
