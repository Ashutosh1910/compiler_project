#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexerDef.h"
#define MAX_VARIABLE_LEN 20


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

TokenList newTokenList(int initialCapacity){
    Token* buf=(Token*)malloc(sizeof(Token)*initialCapacity);
    TokenList tl={
        .buf=buf,
        .capacity=initialCapacity
    };
    return tl;
}

void printError(const char *msg){
    perror(msg);
}


void printLexerError(const char *msg,State* s){
    printf("[LEXER-ERROR] at line %d: %s\n",s->line,msg);
}

int match(char a, char b,const char * msg,State* s){
    if(a==b) return 1;
    else printLexerError(msg, s);
    s->scanNext=0;
    return 0;
}
int isSmallAlpha(char c){
    return c>='a'&&c<='z';
}
int isNum(char c){
    return c>='0'&&c<='9';
}

State initializeState(const char *fileName){
    FILE* file=fopen(fileName, "r");
    if(!file) printError("File not found");
    State s={
        .file=file,
        .isAtEnd=0,
        .line=1,
        .scanNext=1,
        .tokenList=newTokenList(10),
        
    };
    return s;
}

void appendToTokenList(Token c, TokenList t){
    printf("starting to add\n");
    if(t.size<t.capacity){
        t.buf[t.size]=c;
        t.size++;   
    }else{
        t.buf=(Token* )realloc(t.buf, t.capacity*2);
        t.capacity=t.capacity*2;
        t.buf[t.size]=c;
        t.size++;  
    }
    printf("added new token\n");
}

void scan(State *s){
    
    char c;
    
    while(!s->isAtEnd){
        if(s->scanNext) c=fgetc(s->file);
        else s->scanNext=1;
        
        printf("scanning %c\n",c);
        switch (c) {
            case '\n': {s->line++; break;}
            case ' ':
            case '\t':break;
            case '+':{ appendToTokenList(newToken(TK_PLUS,s),s->tokenList); break;}
            case ',':{ appendToTokenList(newToken(TK_COMMA,s),s->tokenList); break;}
            case ';':{ appendToTokenList(newToken(TK_SEM,s),s->tokenList); break;}
            case ':':{ appendToTokenList(newToken(TK_COLON,s),s->tokenList); break;}
            case '.':{ appendToTokenList(newToken(TK_DOT,s),s->tokenList); break;}
            case '-':{ appendToTokenList(newToken(TK_MINUS,s),s->tokenList); break;}
            case '*':{ appendToTokenList(newToken(TK_MUL,s),s->tokenList); break;}
            case '/':{ appendToTokenList(newToken(TK_DIV,s),s->tokenList); break;}
            case '(':{ appendToTokenList(newToken(TK_OP,s),s->tokenList); break;}
            case ')':{ appendToTokenList(newToken(TK_CL,s),s->tokenList); break;}
            case '[':{ appendToTokenList(newToken(TK_SQL,s),s->tokenList); break;}
            case ']':{ appendToTokenList(newToken(TK_SQR,s),s->tokenList); break;}
            case '~':{ appendToTokenList(newToken(TK_NOT,s),s->tokenList); break;}
            
            case '!':
                c=fgetc(s->file);
                if(match('=',c,"expected !=",s))  appendToTokenList(newToken(TK_NE,s),s->tokenList);
                break;
            case '=':
                c=fgetc(s->file);
                if(match('=',c,"expected ==",s))  appendToTokenList(newToken(TK_EQ,s),s->tokenList);
                break;
            case '@':
                c=fgetc(s->file);
                
                if(match('@',c,"expected @@@",s))  {
                    c=fgetc(s->file);
                    if(match('@',c,"expected @@@",s)) appendToTokenList(newToken(TK_OR ,s),s->tokenList);
                }
                break;
            case '&':
                c=fgetc(s->file);
                
                if(match('&',c,"expected &&&",s))  {
                    c=fgetc(s->file);
                    if(match('&',c,"expected &&&",s)) appendToTokenList(newToken(TK_AND ,s),s->tokenList);
                }
                break;
            case '<':
                c=fgetc(s->file);
                if(c!='='&&c!='-') 
                {   appendToTokenList(newToken(TK_LT ,s),s->tokenList);
                    s->scanNext=0;
                    break;
                }
                
                if(c=='='){
                    appendToTokenList(newToken(TK_LE ,s),s->tokenList);
                    break;
                }
                
                if(c=='-'){
                    c=fgetc(s->file);
                    if(match('-',c,"expected <--",s)){
                        c=fgetc(s->file);
                        if(match('-',c,"expected <--",s)) appendToTokenList(newToken(TK_ASSIGNOP, s), s->tokenList);
                    }
                }
                break;
            case '>':
                c=fgetc(s->file);
                if(c!='=') 
                {   appendToTokenList(newToken(TK_GT,s),s->tokenList);
                    s->scanNext=0; 
                }
                else{
                    appendToTokenList(newToken(TK_GE,s),s->tokenList);
                }   
                break;
            
            case '#':
                printf("\n");
                Token rtoken = newToken(TK_RUID, s);
                do{
                    rtoken.lexeme[rtoken.lexemeSize++]=c;
                    c=fgetc(s->file);
                }while(isSmallAlpha(c)&&rtoken.lexemeSize<MAX_VARIABLE_LEN);
                
                if(rtoken.lexemeSize<2){
                    printLexerError("expected record identifier of atleast len 1", s);
                    s->scanNext=0;
                    break;
                }
                
                if(isSmallAlpha(c)&&rtoken.lexemeSize==MAX_VARIABLE_LEN){
                    printLexerError("exceeded max size of identifier (20)", s);
                    while(isSmallAlpha(c)){
                        c=fgetc(s->file);
                    }
                    s->scanNext=0;
                    break;
                }        
                appendToTokenList(rtoken, s->tokenList);
                s->scanNext=0;
                break;
                           
            case '_':
                
            case '%':
                while(c!='\n'){
                    c=fgetc(s->file);
                }
                break;
            
            case EOF: {s->isAtEnd=1; break;}
            
            default:
                if(isNum(c)){
                    Token num=newToken(TK_NUM, s);
                    int num_digits=0;
                    do{
                        num_digits++;
                        num.lexeme[num.lexemeSize++]=c;
                        c=getc(s->file);
                    }while(isNum(c)&&num_digits<MAX_VARIABLE_LEN);
                    
                    if(c!='.'){
                        appendToTokenList(num, s->tokenList);
                        s->scanNext=0;  
                    }else{
                        num.type=TK_RNUM;
                        
                        appendToTokenList(num, s->tokenList);
                        
                    }
                    
                }
                else{printf("error %c",c);}
            
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