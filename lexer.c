//Group 51
//Ashutosh Desai - 2023A7PS0675P
//Anushka Doshi - 2023A7PS0597P
//Aarya Jain - 2023A7PS0618P
//Devansh Agarwal - 2023A7PS0570P
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_VARIABLE_LEN 20
#define MAX_FUNCTION_LEN 30
#define HASH_SIZE 53

Token newToken(TokenType type, State *s) {
  Token t = {.type = type, .lexeme = {0}, .lexemeSize = 0, .lineNo = s->line};
  return t;
}

TokenList newTokenList(int initialCapacity) {
  Token *buf = (Token *)malloc(sizeof(Token) * initialCapacity);
  TokenList tl = {.buf = buf, .capacity = initialCapacity};
  return tl;
}

void printError(const char *msg) { perror(msg); }

void printLexerError(const char *msg, State *s) {
  printf("[LEXER-ERROR] at line %d: %s\n", s->line, msg);
}

int match(char a, char b, const char *msg, State *s) {
  if (a == b)
    return 1;
  else
    printLexerError(msg, s);
  s->scanNext = 0;
  return 0;
}
int isSmallAlpha(char c) { return c >= 'a' && c <= 'z'; }
int isAlpha(char c) { return isSmallAlpha(c) || (c >= 'A' && c <= 'Z'); }
int isNum(char c) { return c >= '0' && c <= '9'; }

int min(int a, int b) {
  if (a < b)
    return a;
  return b;
}

unsigned int hash(const char *s) {
  unsigned int h = 0;
  unsigned int i = 0;

  while (s[i]) {
    h = h * 31 + (unsigned int)s[i];
    i++;
  }
  return h % HASH_SIZE;
}

void insertInHashmap(Hashmap *h, const char *key, TokenType token) {
  unsigned int idx = hash(key);

  while (h->table[idx].occupied) {
    idx = (idx + 1) % HASH_SIZE;
  }

  h->table[idx].key = key;
  h->table[idx].token = token;
  h->table[idx].occupied = 1;
}

Hashmap initializeKeywordMap() {
  Hashmap keywordMap;
  memset(&keywordMap, 0, sizeof(Hashmap));

  insertInHashmap(&keywordMap, "with", TK_WITH);
  insertInHashmap(&keywordMap, "parameters", TK_PARAMETERS);
  insertInHashmap(&keywordMap, "end", TK_END);
  insertInHashmap(&keywordMap, "while", TK_WHILE);
  insertInHashmap(&keywordMap, "union", TK_UNION);
  insertInHashmap(&keywordMap, "endunion", TK_ENDUNION);
  insertInHashmap(&keywordMap, "definetype", TK_DEFINETYPE);
  insertInHashmap(&keywordMap, "as", TK_AS);
  insertInHashmap(&keywordMap, "type", TK_TYPE);
  insertInHashmap(&keywordMap, "global", TK_GLOBAL);
  insertInHashmap(&keywordMap, "parameter", TK_PARAMETER);
  insertInHashmap(&keywordMap, "list", TK_LIST);
  insertInHashmap(&keywordMap, "input", TK_INPUT);
  insertInHashmap(&keywordMap, "output", TK_OUTPUT);
  insertInHashmap(&keywordMap, "int", TK_INT);
  insertInHashmap(&keywordMap, "real", TK_REAL);
  insertInHashmap(&keywordMap, "endwhile", TK_ENDWHILE);
  insertInHashmap(&keywordMap, "if", TK_IF);
  insertInHashmap(&keywordMap, "then", TK_THEN);
  insertInHashmap(&keywordMap, "endif", TK_ENDIF);
  insertInHashmap(&keywordMap, "read", TK_READ);
  insertInHashmap(&keywordMap, "write", TK_WRITE);
  insertInHashmap(&keywordMap, "return", TK_RETURN);
  insertInHashmap(&keywordMap, "call", TK_CALL);
  insertInHashmap(&keywordMap, "record", TK_RECORD);
  insertInHashmap(&keywordMap, "endrecord", TK_ENDRECORD);
  insertInHashmap(&keywordMap, "else", TK_ELSE);
  return keywordMap;
}

TokenType lookupKeyword(Hashmap *h, const char *key) {
  unsigned int idx = hash(key);

  while (h->table[idx].occupied) {
    if (strcmp(h->table[idx].key, key) == 0) {
      return h->table[idx].token;
    }
    idx = (idx + 1) % HASH_SIZE;
  }

  return TK_ERROR;
}

State initializeState(const char *fileName,int logging) {

  FILE *file = fopen(fileName, "r");
  if (!file)
    printError("File not found");
  State s = {.file = file,
             .isAtEnd = 0,
             .line = 1,
             .scanNext = 1,
             .tokenList = newTokenList(10),
             .keywordMap = initializeKeywordMap(),
            .logging = logging};
  return s;
}

void appendToTokenList(Token c, State* s) {

  // printf("starting to add\n");
  if (s->tokenList.size < s->tokenList.capacity) {
    s->tokenList.buf[s->tokenList.size] = c;
    s->tokenList.size++;
  } else {
    s->tokenList.buf = (Token *)realloc(s->tokenList.buf, sizeof(Token) * s->tokenList.capacity * 2);
    s->tokenList.capacity = s->tokenList.capacity * 2;
    s->tokenList.buf[s->tokenList.size] = c;
    s->tokenList.size++;
  }
  if (s->logging) printToken(c);
}

TokenList scan(State *s) {

  char c;

  while (!s->isAtEnd) {
    if (s->scanNext)
      c = fgetc(s->file);
    else
      s->scanNext = 1;

    // printf("scanning %c\n",c);
    switch (c) {
    case '\n': {
      s->line++;
      break;
    }
    case ' ':
    case '\t':
      break;
    case '+': {
      appendToTokenList(newToken(TK_PLUS, s),s);
      break;
    }
    case ',': {
      appendToTokenList(newToken(TK_COMMA, s),s);
      break;
    }
    case ';': {
      appendToTokenList(newToken(TK_SEM, s),s);
      break;
    }
    case ':': {
      appendToTokenList(newToken(TK_COLON, s),s);
      break;
    }
    case '.': {
      appendToTokenList(newToken(TK_DOT, s),s);
      break;
    }
    case '-': {
      appendToTokenList(newToken(TK_MINUS, s),s);
      break;
    }
    case '*': {
      appendToTokenList(newToken(TK_MUL, s),s);
      break;
    }
    case '/': {
      appendToTokenList(newToken(TK_DIV, s),s);
      break;
    }
    case '(': {
      appendToTokenList(newToken(TK_OP, s),s);
      break;
    }
    case ')': {
      appendToTokenList(newToken(TK_CL, s),s);
      break;
    }
    case '[': {
      appendToTokenList(newToken(TK_SQL, s),s);
      break;
    }
    case ']': {
      appendToTokenList(newToken(TK_SQR, s),s);
      break;
    }
    case '~': {
      appendToTokenList(newToken(TK_NOT, s),s);
      break;
    }

    case '!':
      c = fgetc(s->file);
      if (match('=', c, "expected !=", s))
        appendToTokenList(newToken(TK_NE, s),s);
      break;
    case '=':
      c = fgetc(s->file);
      if (match('=', c, "expected ==", s))
        appendToTokenList(newToken(TK_EQ, s),s);
      break;
    case '@':
      c = fgetc(s->file);

      if (match('@', c, "expected @@@", s)) {
        c = fgetc(s->file);
        if (match('@', c, "expected @@@", s))
          appendToTokenList(newToken(TK_OR, s),s);
      }
      break;
    case '&':
      c = fgetc(s->file);

      if (match('&', c, "expected &&&", s)) {
        c = fgetc(s->file);
        if (match('&', c, "expected &&&", s))
          appendToTokenList(newToken(TK_AND, s),s);
      }
      break;
    case '<':
      c = fgetc(s->file);
      if (c != '=' && c != '-') {
        appendToTokenList(newToken(TK_LT, s),s);
        s->scanNext = 0;
        break;
      }

      if (c == '=') {
        appendToTokenList(newToken(TK_LE, s),s);
        break;
      }

      if (c == '-') {
        c = fgetc(s->file);
        if (match('-', c, "expected <--", s)) {
          c = fgetc(s->file);
          if (match('-', c, "expected <--", s))
            appendToTokenList(newToken(TK_ASSIGNOP, s),s);
        }
      }
      break;
    case '>':
      c = fgetc(s->file);
      if (c != '=') {
        appendToTokenList(newToken(TK_GT, s),s);
        s->scanNext = 0;
      } else {
        appendToTokenList(newToken(TK_GE, s),s);
      }
      break;

    case '#':
      printf("");
      Token rtoken = newToken(TK_RUID, s);
      do {
        rtoken.lexeme[rtoken.lexemeSize++] = c;
        c = fgetc(s->file);
      } while (isSmallAlpha(c) && rtoken.lexemeSize < MAX_VARIABLE_LEN);

      if (rtoken.lexemeSize < 2) {
        printLexerError("expected record identifier of atleast len 1", s);
        s->scanNext = 0;
        break;
      }

      if (isSmallAlpha(c) && rtoken.lexemeSize == MAX_VARIABLE_LEN) {
        printLexerError("exceeded max size of identifier (20)", s);
        while (isSmallAlpha(c)) {
          c = fgetc(s->file);
        }
        s->scanNext = 0;
        break;
      }
      rtoken.lexeme[rtoken.lexemeSize] = '\0';
      appendToTokenList(rtoken,s);
      s->scanNext = 0;
      break;

    case '_':
      printf("");
      Token fun = newToken(TK_FUNID, s);
      do {
        fun.lexeme[fun.lexemeSize++] = c;
        c = fgetc(s->file);
      } while (isAlpha(c) && fun.lexemeSize < MAX_FUNCTION_LEN);

      if (fun.lexemeSize < 2) {
        printLexerError("expected function name of atleast len 1", s);
        s->scanNext = 0;
        break;
      }

      if ((isAlpha(c) || isNum(c)) && fun.lexemeSize == MAX_FUNCTION_LEN) {
        printLexerError("exceeded max size of function name (30)", s);
        if (isAlpha(c)) {
          while (isAlpha(c)) {
            c = fgetc(s->file);
          }
          while (isNum(c)) {
            c = fgetc(s->file);
          }
        } else {
          while (isNum(c)) {
            c = fgetc(s->file);
          }
        }
        s->scanNext = 0;
        break;
      }

      if (isNum(c)) {
        do {
          fun.lexeme[fun.lexemeSize++] = c;
          c = fgetc(s->file);
        } while (isNum(c) && fun.lexemeSize < MAX_FUNCTION_LEN);
      }

      if (isNum(c) && fun.lexemeSize == MAX_FUNCTION_LEN) {
        printLexerError("exceeded max size of function name (30)", s);
        while (isNum(c)) {
          c = fgetc(s->file);
        }
        s->scanNext = 0;
        break;
      }

      char *_main = "_main";
      int match = 0;
      for (int i = 0; i < min(5, fun.lexemeSize); i++) {
        if (_main[i] == fun.lexeme[i])
          match++;
      }
      if (match == 5 && fun.lexemeSize == 5) {
        fun.type = TK_MAIN;
        // printf("adding main\n");
      }

      s->scanNext = 0;
      fun.lexeme[fun.lexemeSize] = '\0';
      appendToTokenList(fun,s);
      break;
    case '%':
      appendToTokenList(newToken(TK_COMMENT, s),s);
      while (c != '\n') {
        c = fgetc(s->file);
      }
      s->line++;
      break;

    case EOF: {
      s->isAtEnd = 1;
      break;
    }

    default:
      if (isNum(c)) {
        Token num = newToken(TK_NUM, s);
        int num_digits = 0;
        do {
          num_digits++;
          num.lexeme[num.lexemeSize++] = c;
          c = fgetc(s->file);
        } while (isNum(c) && num_digits < MAX_VARIABLE_LEN);

        if (c != '.') {
          appendToTokenList(num,s);
          s->scanNext = 0;
        } else {
          num.lexeme[num.lexemeSize++] = c;
          num.type = TK_RNUM;
          c = fgetc(s->file);
          if (isNum(c)) {
            num.lexeme[num.lexemeSize++] = c;
            c = fgetc(s->file);
            if (isNum(c)) {
              num.lexeme[num.lexemeSize++] = c;
              c = fgetc(s->file);
              if (c == 'E') {
                num.lexeme[num.lexemeSize++] = c;
                c = fgetc(s->file);
                if (c == '-' || c == '+' || isNum(c)) {
                  num.lexeme[num.lexemeSize++] = c;
                  if (isNum(c)) {

                    c = fgetc(s->file);
                    if (isNum(c)) {
                      num.lexeme[num.lexemeSize++] = c;
                    } else {
                      printLexerError("expected number after E", s);
                      s->scanNext = 0;
                      break;
                    }
                  } else {
                    c = fgetc(s->file);
                    if (isNum(c)) {
                      num.lexeme[num.lexemeSize++] = c;
                      c = fgetc(s->file);
                      if (isNum(c)) {
                        num.lexeme[num.lexemeSize++] = c;
                      }
                    } else {
                      printLexerError("expected number after E", s);
                      s->scanNext = 0;
                      break;
                    }
                  }
                } else {
                  printLexerError("expected number after E", s);
                  s->scanNext = 0;
                  break;
                }
              } else {
                s->scanNext = 0;
              }
            } else {
              printLexerError("expected number after decimal", s);
              s->scanNext = 0;
              break;
            }
          } else {
            printLexerError("expected number after decimal", s);
            s->scanNext = 0;
            break;
          }
          num.lexeme[num.lexemeSize] = '\0';
          appendToTokenList(num,s);
        }
      } else if (isSmallAlpha(c)) {
        Token var = newToken(TK_FIELDID, s);
        var.lexeme[var.lexemeSize++] = c;
        if (c >= 'b' && c <= 'd') {

          var.type = TK_ID;
          c = fgetc(s->file);
          if (isSmallAlpha(c)) {
            var.type = TK_FIELDID;
            var.lexeme[var.lexemeSize++] = c;
          } else if (c >= '2' && c <= '7') {
            var.lexeme[var.lexemeSize++] = c;
            c = fgetc(s->file);
            while (c <= 'd' && c >= 'b' && var.lexemeSize < MAX_VARIABLE_LEN) {
              var.lexeme[var.lexemeSize++] = c;
              c = fgetc(s->file);
            }
            if (((c >= '2' && c <= '7') || (c <= 'd' && c >= 'b')) &&
                var.lexemeSize == MAX_VARIABLE_LEN) {
              printLexerError("exceeded max length of identifier(20)", s);
              while (c <= 'd' && c >= 'b') {
                c = fgetc(s->file);
              }
              while (c >= '2' && c <= '7') {
                c = fgetc(s->file);
              }
              s->scanNext = 0;
              break;
            }
            while (c <= '7' && c >= '2' && var.lexemeSize < MAX_VARIABLE_LEN) {
              var.lexeme[var.lexemeSize++] = c;
              c = fgetc(s->file);
            }
            if (c >= '2' && c <= '7' && var.lexemeSize == MAX_VARIABLE_LEN) {
              printLexerError("exceeded max length of identifier(20)", s);
              while (c >= '2' && c <= '7') {
                c = fgetc(s->file);
              }
              s->scanNext = 0;
              break;
            }
          } else {
            s->scanNext = 0;
            printLexerError("expected [a-z]|[2-7]", s);
            break;
          }
        }

        if (var.type == TK_FIELDID) {

          c = fgetc(s->file);
          while (isSmallAlpha(c) && var.lexemeSize < MAX_VARIABLE_LEN) {
            var.lexeme[var.lexemeSize++] = c;
            c = fgetc(s->file);
          }
          if (isSmallAlpha(c) && var.lexemeSize == MAX_VARIABLE_LEN) {
            printLexerError("exceeded max length of identifier(20)", s);
            while (isSmallAlpha(c)) {
              c = fgetc(s->file);
            }
            s->scanNext = 0;
            break;
          }
          var.lexeme[var.lexemeSize] = '\0';
          TokenType found = lookupKeyword(&s->keywordMap, var.lexeme);
          if (found != TK_ERROR) {
            var.type = found;
          }
        }
        s->scanNext = 0;
        var.lexeme[var.lexemeSize] = '\0';
        appendToTokenList(var,s);
      } else {
        printf("[LEXER-ERROR] at line %d: %c not recognized\n", s->line, c);
      }
    }
  }
  appendToTokenList(newToken(TK_DOLLAR,s),s);
  fclose(s->file);
  return s->tokenList;
}

void removeComments(const char *filename) {
  State state = initializeState(filename,0);
  char c;
  while (c != EOF) {
    c = fgetc(state.file);
    if (c == '%') {
      while (c != '\n') {
        c = fgetc(state.file);
      }
    }
    if (c != EOF)
      printf("%c", c);
  }

  fclose(state.file);
  printf("Successfully removed comments from %s\n", filename);
}

void printTokens(const char *filename) {
  // TODO :MATCH ERROR PRINTING FORMAT,REPLACE STRCMP WITH OWN VERSION

  State state = initializeState(filename,1);

  TokenList tl = scan(&state);

  printf("done\n");
}