#include <stdio.h>
typedef enum TokenType{
    TK_ASSIGNOP,
    TK_COMMENT,
    TK_FIELDID,
    TK_ID ,
    TK_NUM,
    TK_RNUM,
    TK_FUNID ,
    TK_RUID,
    TK_WITH, 
    TK_PARAMETERS,
    TK_END,
    TK_WHILE,
    TK_UNION,
    TK_ENDUNION,
    TK_DEFINETYPE,
    TK_AS,
    TK_MAIN,
    TK_GLOBAL,
    TK_PARAMETER,
    TK_LIST,
    TK_SQL,
    TK_SQR,
    TK_INPUT,
    TK_OUTPUT,
    TK_INT,
    TK_REAL,
    TK_COMMA,
    TK_SEM,
    TK_COLON,
    TK_DOT,
    TK_ENDWHILE,    
    TK_CL,
    TK_IF,
    TK_THEN,
    TK_ENDIF,
    TK_READ,
    TK_WRITE,
    TK_RETURN,
    TK_PLUS,
    TK_MINUS,
    TK_MUL,
    TK_DIV,
    TK_CALL,
    TK_RECORD,
    TK_ENDRECORD,
    TK_ELSE,
    TK_AND,
    TK_OR,
    TK_NOT,
    TK_LT,
    TK_LE,
    TK_EQ,
    TK_GT,
    TK_GE,
    TK_NE,
    TK_ERROR
}TokenType;

typedef struct Token{
    TokenType type;
    char lexeme[30];
    unsigned int lexemeSize;
    unsigned int lineNo;
    int is_error;
    
}Token;

typedef struct State{
    FILE* file;
    int line;
    Token* tokenList;
}State;

typedef struct String{
    char* buffer;
    int len;
}String;

typedef struct Hashmap{
    TokenType* keywords;
}Hashmap;