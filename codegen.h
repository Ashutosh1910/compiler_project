// Group 51
// Ashutosh Desai - 2023A7PS0675P
// Anushka Doshi - 2023A7PS0597P
// Aarya Jain - 2023A7PS0618P
// Devansh Agarwal - 2023A7PS0570P
#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"
#include "symbolTable.h"
#include <stdio.h>

// Writes x86-64 NASM assembly for a program that passed semantic analysis.
// Build the result with:
//   nasm -f elf64 out.asm -o out.o && gcc -no-pie out.o -o program
void generateCode(Program *p, SymbolTable *st, FILE *out);

#endif
