// Group 51
// Ashutosh Desai - 2023A7PS0675P
// Anushka Doshi - 2023A7PS0597P
// Aarya Jain - 2023A7PS0618P
// Devansh Agarwal - 2023A7PS0570P
#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"
#include "symbolTable.h"
#include <stdio.h>

// Deeper expressions are rejected: the checker and code generator recurse
// over expressions, and the generated code keeps one stack slot per level.
#define MAX_EXPR_DEPTH 1000

// Builds the symbol table, lays out memory and type checks the program.
// Errors are printed to `out` sorted by line, one per line:
//   [SEMANTIC-ERROR] at line N: message
// The AST is annotated in place (variable entries, types, offsets, callees).
// The returned table is always non-NULL; *numErrors tells whether the program
// may be handed to the code generator.
SymbolTable *semanticAnalysis(Program *p, int *numErrors, FILE *out);

#endif
