//Group 51
//Ashutosh Desai - 2023A7PS0675P
//Anushka Doshi - 2023A7PS0597P
//Aarya Jain - 2023A7PS0618P
//Devansh Agarwal - 2023A7PS0570P
#include "lexerDef.h"
#include <stdio.h>
void removeComments(const char *filename);
void printTokens(const char *filename);

void printError(const char *msg);
void printLexerError(const char *msg, State *s);
State initializeState(const char *fileName,int logging);

TokenList scan(State *S);

int match(char a, char b, State *s);

Token newToken(TokenType type, State *s);
TokenList newTokenList(int size);
void appendToTokenList(Token c, TokenList t);

Token getTokenAt(int index);

Hashmap initializeKeywordMap();
unsigned int hash(const char *s);
void insertInHashmap(Hashmap *h, const char *key, TokenType token);

int isSmallAlpha(char c);
int isAlpha(char c);
int isAlphaNum(char c);
int isNum(char c);


