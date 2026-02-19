#include "lexerDef.h"
#include <stdio.h>
void removeComments();
void printTokens();

void printError(const char* msg);
void printLexerError(const char *msg,State* s);
State initializeState(const char* fileName);

void scan(State* S);
void function(State* S);
void number(State* S);
void record(State* S);



int match(char a,char b);


void appendToString(char c,String* s);
void clearString(String* s);
Token newToken(TokenType type,State* s);
//Token newToken(TokenType type,char* lexeme);

TokenList newTokenList(int size);
void appendToTokenList(Token c,TokenList t);
Token getTokenAt(int index);



void insertInHashmap(char* keyword,TokenType token_type);
int hash(char* s);
int isSmallAlpha(char c);
int isAlpha(char c);
int isAlphaNum(char c);





