//Group 51
//Ashutosh Desai - 2023A7PS0675P
//Anushka Doshi - 2023A7PS0597P
//Aarya Jain - 2023A7PS0618P
//Devansh Agarwal - 2023A7PS0570P
#include "lexerDef.h"
const char *tokenTypeToString(TokenType type) {
  switch (type) {
  case TK_ASSIGNOP:
    return "TK_ASSIGNOP";
  case TK_COMMENT:
    return "TK_COMMENT";
  case TK_FIELDID:
    return "TK_FIELDID";
  case TK_ID:
    return "TK_ID";
  case TK_NUM:
    return "TK_NUM";
  case TK_RNUM:
    return "TK_RNUM";
  case TK_FUNID:
    return "TK_FUNID";
  case TK_RUID:
    return "TK_RUID";
  case TK_WITH:
    return "TK_WITH";
  case TK_PARAMETERS:
    return "TK_PARAMETERS";
  case TK_END:
    return "TK_END";
  case TK_WHILE:
    return "TK_WHILE";
  case TK_UNION:
    return "TK_UNION";
  case TK_ENDUNION:
    return "TK_ENDUNION";
  case TK_DEFINETYPE:
    return "TK_DEFINETYPE";
  case TK_AS:
    return "TK_AS";
  case TK_TYPE:
    return "TK_TYPE";
  case TK_MAIN:
    return "TK_MAIN";
  case TK_GLOBAL:
    return "TK_GLOBAL";
  case TK_PARAMETER:
    return "TK_PARAMETER";
  case TK_LIST:
    return "TK_LIST";
  case TK_SQL:
    return "TK_SQL";
  case TK_SQR:
    return "TK_SQR";
  case TK_INPUT:
    return "TK_INPUT";
  case TK_OUTPUT:
    return "TK_OUTPUT";
  case TK_INT:
    return "TK_INT";
  case TK_REAL:
    return "TK_REAL";
  case TK_COMMA:
    return "TK_COMMA";
  case TK_SEM:
    return "TK_SEM";
  case TK_COLON:
    return "TK_COLON";
  case TK_DOT:
    return "TK_DOT";
  case TK_ENDWHILE:
    return "TK_ENDWHILE";
  case TK_OP:
    return "TK_OP";
  case TK_CL:
    return "TK_CL";
  case TK_IF:
    return "TK_IF";
  case TK_THEN:
    return "TK_THEN";
  case TK_ENDIF:
    return "TK_ENDIF";
  case TK_READ:
    return "TK_READ";
  case TK_WRITE:
    return "TK_WRITE";
  case TK_RETURN:
    return "TK_RETURN";
  case TK_PLUS:
    return "TK_PLUS";
  case TK_MINUS:
    return "TK_MINUS";
  case TK_MUL:
    return "TK_MUL";
  case TK_DIV:
    return "TK_DIV";
  case TK_CALL:
    return "TK_CALL";
  case TK_RECORD:
    return "TK_RECORD";
  case TK_ENDRECORD:
    return "TK_ENDRECORD";
  case TK_ELSE:
    return "TK_ELSE";
  case TK_AND:
    return "TK_AND";
  case TK_OR:
    return "TK_OR";
  case TK_NOT:
    return "TK_NOT";
  case TK_LT:
    return "TK_LT";
  case TK_LE:
    return "TK_LE";
  case TK_EQ:
    return "TK_EQ";
  case TK_GT:
    return "TK_GT";
  case TK_GE:
    return "TK_GE";
  case TK_NE:
    return "TK_NE";
  case TK_EPS:
    return "TK_EPS";
  case TK_DOLLAR:
    return "TK_DOLLAR";
  case TK_ERROR:
    return "TK_ERROR";
  case NUM_TOKENS:
    return "NUM_TOKENS";
  default:
    return "UNKNOWN_TOKEN";
  }
}

const char *tokenTypeToLexeme(Token* t) {
  switch (t->type) {
  case TK_ASSIGNOP:
    return "<---";
  case TK_WITH:
    return "with";
  case TK_PARAMETERS:
    return "parameters";
  case TK_END:
    return "end";
  case TK_WHILE:
    return "while";
  case TK_UNION:
    return "union";
  case TK_ENDUNION:
    return "endunion";
  case TK_DEFINETYPE:
    return "definetype";
  case TK_AS:
    return "as";
  case TK_TYPE:
    return "type";
  case TK_MAIN:
    return "_main";
  case TK_GLOBAL:
    return "global";
  case TK_PARAMETER:
    return "parameter";
  case TK_LIST:
    return "list";
  case TK_SQL:
    return "[";
  case TK_SQR:
    return "]";
  case TK_INPUT:
    return "input";
  case TK_OUTPUT:
    return "output";
  case TK_INT:
    return "int";
  case TK_REAL:
    return "real";
  case TK_COMMA:
    return ",";
  case TK_SEM:
    return ";";
  case TK_COLON:
    return ":";
  case TK_DOT:
    return ".";
  case TK_ENDWHILE:
    return "endwhile";
  case TK_OP:
    return "(";
  case TK_CL:
    return ")";
  case TK_IF:
    return "if";
  case TK_THEN:
    return "then";
  case TK_ENDIF:
    return "endif";
  case TK_READ:
    return "read";
  case TK_WRITE:
    return "write";
  case TK_RETURN:
    return "return";
  case TK_PLUS:
    return "+";
  case TK_MINUS:
    return "-";
  case TK_MUL:
    return "*";
  case TK_DIV:
    return "/";
  case TK_CALL:
    return "call";
  case TK_RECORD:
    return "record";
  case TK_ENDRECORD:
    return "endrecord";
  case TK_ELSE:
    return "else";
  case TK_AND:
    return "&&&";
  case TK_OR:
    return "@@@";
  case TK_NOT:
    return "~";
  case TK_LT:
    return "<";
  case TK_LE:
    return "<=";
  case TK_EQ:
    return "==";
  case TK_GT:
    return ">";
  case TK_GE:
    return ">=";
  case TK_NE:
    return "!=";
  case TK_DOLLAR:
    return "$";
  default:
    return t->lexeme;
  }
}

void printTokenHeader() {
  printf("---------------------------------------------------------------------"
         "----------\n");
  printf("| %-6s | %-18s | %-25s |\n", "Line", "Token Type", "Lexeme");
  printf("---------------------------------------------------------------------"
         "----------\n");
}

void printToken(Token t) {
  printf("| %-6u | %-18s | %-25s |\n", t.lineNo, tokenTypeToString(t.type),
         t.lexemeSize > 0 ? t.lexeme : "-");
}