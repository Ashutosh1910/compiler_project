// Group 51
// Ashutosh Desai - 2023A7PS0675P
// Anushka Doshi - 2023A7PS0597P
// Aarya Jain - 2023A7PS0618P
// Devansh Agarwal - 2023A7PS0570P
//
// Types, variables and functions known to the compiler after semantic
// analysis, together with the memory layout chosen for each of them.
//
// Memory model (all scalars are 8 bytes):
//   int     -> 64-bit signed integer
//   real    -> IEEE-754 double
//   record  -> fields laid out one after another, in declaration order
//   union   -> every field at offset 0; size of the largest field
//   global  -> a label G_<name> in .bss
//   local   -> [rbp - offset]
//   input / output parameter -> [rbp + 16 + offset], inside a block the
//              caller reserves on its stack (inputs first, then outputs)
#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include "ast.h"
#include <stdio.h>

#define SCALAR_SIZE 8
// Limits that keep offsets far from int overflow and frames well inside the
// default 8 MiB stack.
#define MAX_TYPE_SIZE (1 << 20)  // one record or union: 1 MiB
#define MAX_FRAME_SIZE (1 << 22) // locals, or parameters, of a function: 4 MiB
#define MAX_STACK_USE (6 << 20)  // deepest call chain, of the usual 8 MiB stack

typedef enum { TY_INT, TY_REAL, TY_RECORD, TY_UNION, TY_ERROR } TypeKind;

struct Type;

typedef struct {
  char name[AST_NAME_LEN];
  struct Type *type;
  int offset; // from the start of the enclosing record/union
  int line;
} FieldInfo;

typedef struct Type {
  TypeKind kind;
  char name[AST_NAME_LEN]; // "int", "real", or the defining #name
  FieldInfo *fields;
  int numFields;
  int size;        // bytes
  int line;        // line of the definition (records/unions)
  int layoutState; // 0 = not laid out, 1 = in progress, 2 = done
  int hasUnion;    // is a union or (transitively) contains one
  TypeDef *def;    // AST definition (records/unions)
} Type;

typedef struct {
  char name[AST_NAME_LEN]; // the alias (#point)
  Type *type;              // what it stands for
  int line;
} TypeAlias;

typedef enum { VAR_GLOBAL, VAR_LOCAL, VAR_INPUT, VAR_OUTPUT } VarKind;

typedef struct VarEntry {
  char name[AST_NAME_LEN];
  Type *type;
  VarKind kind;
  int offset; // see the memory model above (unused for globals)
  int line;
  struct FuncEntry *owner; // NULL for globals
  struct VarEntry *next;
} VarEntry;

typedef struct FuncEntry {
  char name[AST_NAME_LEN];
  int line;
  int index; // position in the source file (0-based)
  int isMain;
  VarEntry **inputs;
  int numInputs;
  VarEntry **outputs;
  int numOutputs;
  VarEntry *vars;    // parameters and locals, in declaration order
  int localSize;     // bytes reserved below rbp (multiple of 16)
  int inSize;        // bytes of input parameters
  int paramSize;     // bytes of the caller-reserved block (multiple of 16)
  Function *ast;
} FuncEntry;

typedef struct {
  Type *intType, *realType, *errorType;
  Type **types; // records and unions, in definition order
  int numTypes, capTypes;
  TypeAlias *aliases;
  int numAliases, capAliases;
  VarEntry *globals; // in declaration order
  FuncEntry **funcs; // in source order (_main last)
  int numFuncs, capFuncs;
} SymbolTable;

SymbolTable *newSymbolTable(void);
void freeSymbolTable(SymbolTable *st);

Type *addType(SymbolTable *st, TypeKind kind, const char *name, int line);
void addAlias(SymbolTable *st, const char *name, Type *type, int line);
Type *findType(SymbolTable *st, const char *name); // type or alias
TypeAlias *findAlias(SymbolTable *st, const char *name);

FuncEntry *addFunc(SymbolTable *st, const char *name, int line);
FuncEntry *findFunc(SymbolTable *st, const char *name);

VarEntry *findGlobal(SymbolTable *st, const char *name);
VarEntry *findLocal(FuncEntry *f, const char *name); // params + locals
VarEntry *lookupVar(SymbolTable *st, FuncEntry *f, const char *name);

FieldInfo *findField(Type *t, const char *name);
int isScalar(Type *t);
int isAggregate(Type *t);
const char *typeName(Type *t);

void printSymbolTable(SymbolTable *st, FILE *out);

#endif
