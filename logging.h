//Group 51
//Ashutosh Desai - 2023A7PS0675P
//Anushka Doshi - 2023A7PS0597P
//Aarya Jain - 2023A7PS0618P
//Devansh Agarwal - 2023A7PS0570P
#ifndef LOGGING_H
#define LOGGING_H

#include "lexerDef.h"
const char *tokenTypeToString(TokenType type);
const char *tokenTypeToLexeme(Token* t);
void printTokenHeader();
void printToken(Token t);

#endif