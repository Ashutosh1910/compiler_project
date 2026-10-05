// Group 51
// Ashutosh Desai - 2023A7PS0675P
// Anushka Doshi - 2023A7PS0597P
// Aarya Jain - 2023A7PS0618P
// Devansh Agarwal - 2023A7PS0570P
//
// Abstract syntax tree. The LL(1) parse tree mirrors the grammar exactly
// (including <expPrime>, <more_ids>, eps leaves, ...). The AST throws that
// scaffolding away and keeps only what later phases need.
#ifndef AST_H
#define AST_H

#include "parserDef.h"
#include <stdio.h>

#define AST_NAME_LEN 32

// Deeper expressions are rejected by semantic analysis: every later pass
// recurses over expressions, and the generated code keeps one stack slot per
// level. The AST builder stops nesting one level past the limit, so no pass
// ever sees a deeper tree.
#define MAX_EXPR_DEPTH 1000

struct Type;    // defined in symbolTable.h
struct VarEntry;
struct FuncEntry;

// A type as written in the source: int, real, record #r, union #u, or #r.
typedef enum { TREF_INT, TREF_REAL, TREF_NAMED } TypeRefKind;

typedef struct {
  TypeRefKind kind;
  TokenType keyword; // TK_RECORD / TK_UNION if written explicitly, else TK_EPS
  char name[AST_NAME_LEN]; // record/union name (TREF_NAMED only)
  int line;
} AstTypeRef;

// b5c6.s.ln.beginpoint.x  ->  name = "b5c6", fields = {s, ln, beginpoint, x}
typedef struct {
  char name[AST_NAME_LEN];
  char (*fields)[AST_NAME_LEN];
  int numFields;
  int line;
  // filled in by semantic analysis
  struct VarEntry *entry; // root variable
  struct Type *type;      // type of the whole access path
  int offset;             // byte offset of the accessed field inside root
} AstVarRef;

typedef enum { EXPR_NUM, EXPR_RNUM, EXPR_VAR, EXPR_BINOP } ExprKind;

typedef struct Expr {
  ExprKind kind;
  int line;
  char text[AST_NAME_LEN]; // lexeme of a literal
  long long ival;          // EXPR_NUM
  double rval;             // EXPR_RNUM
  AstVarRef var;           // EXPR_VAR
  TokenType op;            // EXPR_BINOP: TK_PLUS / TK_MINUS / TK_MUL / TK_DIV
  struct Expr *left, *right;
  int depth;         // operators on the longest path to a leaf
  struct Type *type; // filled in by semantic analysis
} Expr;

typedef enum { BOOL_REL, BOOL_AND, BOOL_OR, BOOL_NOT } BoolKind;

typedef struct BoolExpr {
  BoolKind kind;
  int line;
  TokenType relop;   // BOOL_REL: TK_LT .. TK_NE
  Expr *lhs, *rhs;   // BOOL_REL operands (literals or variables)
  struct BoolExpr *left, *right; // AND / OR use both, NOT uses left
} BoolExpr;

typedef struct {
  char name[AST_NAME_LEN];
  int line;
  struct VarEntry *entry; // filled in by semantic analysis
} AstId;

typedef struct {
  AstId *ids;
  int count;
} IdList;

typedef enum {
  STMT_ASSIGN,
  STMT_WHILE,
  STMT_IF,
  STMT_READ,
  STMT_WRITE,
  STMT_CALL
} StmtKind;

typedef struct Stmt {
  StmtKind kind;
  int line;
  struct Stmt *next;

  AstVarRef lhs; // STMT_ASSIGN
  Expr *rhs;     // STMT_ASSIGN

  BoolExpr *cond;         // STMT_WHILE / STMT_IF
  struct Stmt *body;      // STMT_WHILE body / STMT_IF then-part
  struct Stmt *elseBody;  // STMT_IF else-part (NULL if absent)
  int endLine;            // line of endwhile / endif

  Expr *ioArg; // STMT_READ / STMT_WRITE (EXPR_VAR / EXPR_NUM / EXPR_RNUM)

  char funName[AST_NAME_LEN]; // STMT_CALL
  IdList outs, ins;           // STMT_CALL
  struct FuncEntry *callee;   // filled in by semantic analysis
} Stmt;

typedef struct FieldDef {
  AstTypeRef type;
  char name[AST_NAME_LEN];
  int line;
  struct FieldDef *next;
} FieldDef;

// record #r ... endrecord / union #u ... endunion
typedef struct TypeDef {
  int isUnion;
  char name[AST_NAME_LEN];
  FieldDef *fields;
  int line;
  struct TypeDef *next;
} TypeDef;

// definetype record #r as #alias
typedef struct AliasDef {
  int isUnion;
  char target[AST_NAME_LEN];
  char alias[AST_NAME_LEN];
  int line;
  struct AliasDef *next;
} AliasDef;

// "type <dataType> : name [: global];"  and function parameters
typedef struct Decl {
  AstTypeRef type;
  char name[AST_NAME_LEN];
  int isGlobal;
  int line;
  struct Decl *next;
} Decl;

typedef struct Function {
  char name[AST_NAME_LEN];
  int isMain;
  int line;
  Decl *inputs;   // parameters reuse Decl (isGlobal is always 0)
  Decl *outputs;
  TypeDef *typeDefs;
  AliasDef *aliases;
  Decl *decls;
  Stmt *stmts;
  IdList returns;
  int returnLine;
  int endLine;
  struct Function *next;
} Function;

typedef struct {
  Function *functions; // in source order; _main is always last
} Program;

// Builds an AST from a parse tree that was produced without syntax errors.
Program *buildAST(Grammar *g, TreeNode *root);
void freeAST(Program *p);
void printAST(Program *p, FILE *out);

const char *relopToString(TokenType t);
const char *arithopToString(TokenType t);

#endif
