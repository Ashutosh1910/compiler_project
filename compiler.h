// Group 51
// Ashutosh Desai - 2023A7PS0675P
// Anushka Doshi - 2023A7PS0597P
// Aarya Jain - 2023A7PS0618P
// Devansh Agarwal - 2023A7PS0570P
//
// The compilation pipeline: source -> tokens -> parse tree -> AST ->
// annotated AST + symbol table -> NASM assembly. Each stage only runs if the
// previous ones reported no errors.
#ifndef COMPILER_H
#define COMPILER_H

#include "ast.h"
#include "lexerDef.h"
#include "parserDef.h"
#include "symbolTable.h"

typedef struct {
  TokenList tokens;
  int lexErrors;
  Grammar *grammar;
  FirstFollowSets ff;
  ParseTable pt;
  TreeNode *tree;
  int syntaxErrors;
  Program *ast;
  SymbolTable *symbols;
  int semanticErrors;
} Compilation;

// Lexes and parses `file`, printing lexical and syntax errors to stdout.
// Returns 1 if the source is free of lexical and syntax errors.
int runFrontEnd(const char *file, Compilation *c);

// Runs the front end, builds the AST and runs semantic analysis, printing
// all errors. Returns 1 if the program can be compiled.
int runAnalysis(const char *file, Compilation *c);

// Full compilation to an assembly file. Returns 1 on success.
int compileToAssembly(const char *file, const char *asmFile);

// Compiles, then assembles and links with nasm + gcc. Returns 1 on success.
int buildExecutable(const char *file, const char *exeFile);

void freeCompilation(Compilation *c);

#endif
