#ifndef LOGGING_H
#define LOGGING_H

#include "lexerDef.h"
const char *tokenTypeToString(TokenType type);
void printTokenHeader();
void printToken(Token t);

#endif