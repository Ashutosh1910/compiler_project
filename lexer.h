#include "lexerDef.h"
#include <stdio.h>
void removeComments();
void printTokens();

void printError(const char* msg);

void scan(State* S);

int match(char a,char b);


void appendToString(char c,String* s);
void appendToTokenList(Token c,Token* t);

void insertInHashmap(char* keyword,TokenType token_type);
int hash(char* s);




