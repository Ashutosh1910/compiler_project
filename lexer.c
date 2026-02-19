#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexerDef.h"


Token newToken(TokenType type,State* s){
    Token t={
        .type=type,
        .is_error=0,
        .lexeme={0},
        .lexemeSize=0,
        .lineNo=s->line
    };
    return t;
}

TokenList newTokenList(int size){
    Token* buf=(Token*)malloc(sizeof(Token)*size);
    TokenList tl={
        .buf=buf,
        .size=size
    };
    return tl;
}

void printError(const char *msg){
    perror(msg);
}

void printLexerError(const char *msg,State* s){
    printf("[LEXER-ERROR] at line %d: %s\n",s->line,msg);
}

State initializeState(const char *fileName){
    FILE* file=fopen(fileName, "r");
    if(!file) printError("File not found");
    State s={
        .file=file,
        .isAtEnd=0,
        .line=1,
        .tokenList=newTokenList(10),
        
    };
    return s;
}

void appendToTokenList(Token c, TokenList t){
    if(t.size<t.capacity){
        t.buf[t.size]=c;
        t.size++;   
    }else{
        t.buf=(Token* )realloc(t.buf, t.capacity*2);
        t.capacity=t.capacity*2;
        t.buf[t.size]=c;
        t.size++;  
    }
}

void scan(State *s){
    
    char c;
    
    while(!s->isAtEnd){
        c=fgetc(s->file);
        switch (c) {
            case '\n': {s->line++; break;}
            case ' ':
            case '\t':break;
            case '+':{ appendToTokenList(newToken(TK_PLUS,s),s->tokenList); break;}
            
            case EOF: {s->isAtEnd=1; break;}
            
               
            
        }
    }
}

void removeComments(){
    
}

void printTokens(){
    State state=initializeState("test.txt");
    
    scan(&state);
    
    
    printf("done\n");
    
    
}