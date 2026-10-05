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
// digits before the decimal point; with ".dd" and "E+dd" a number still
// fits the 30 characters of Token.lexeme
#define MAX_NUMBER_DIGITS 23
#define HASH_SIZE 53
// decoded bytes of one string literal
#define MAX_STRING_LEN 255

Token newToken(TokenType type, State *s) {
  Token t = {.type = type,
             .lexeme = {0},
             .lexemeSize = 0,
             .lineNo = s->line,
             .literal = -1};
  return t;
}

/* --------------------------------------------------------- string table */

// Decoded string literals. A token's lexeme holds only 30 bytes, so a
// TK_STR token carries an index into this table instead; the AST copies the
// bytes it needs, so the table only has to live until the next
// initializeState().
typedef struct {
  char *bytes; // NUL-terminated; a literal never contains NUL
  int len;
} StringLiteral;

static StringLiteral *strings;
static int numStrings, capStrings;

static void clearStringLiterals(void) {
  for (int i = 0; i < numStrings; i++)
    free(strings[i].bytes);
  free(strings);
  strings = NULL;
  numStrings = capStrings = 0;
}

int addStringLiteral(const char *bytes, int len) {
  if (numStrings == capStrings) {
    capStrings = capStrings ? capStrings * 2 : 16;
    strings = realloc(strings, sizeof(StringLiteral) * capStrings);
  }
  char *copy = malloc(len + 1);
  memcpy(copy, bytes, len);
  copy[len] = '\0';
  strings[numStrings] = (StringLiteral){copy, len};
  return numStrings++;
}

const char *stringLiteralText(int index, int *len) {
  if (index < 0 || index >= numStrings) {
    if (len)
      *len = 0;
    return "";
  }
  if (len)
    *len = strings[index].len;
  return strings[index].bytes;
}

TokenList newTokenList(int initialCapacity) {
  Token *buf = (Token *)malloc(sizeof(Token) * initialCapacity);
  TokenList tl = {.buf = buf, .capacity = initialCapacity};
  return tl;
}

void printError(const char *msg) { perror(msg); }

void printLexerError(const char *msg, State *s) {
  s->errorCount++;
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
  insertInHashmap(&keywordMap, "readchar", TK_READCHAR);
  insertInHashmap(&keywordMap, "writechar", TK_WRITECHAR);
  insertInHashmap(&keywordMap, "print", TK_PRINT);
  insertInHashmap(&keywordMap, "exit", TK_EXIT);
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

  clearStringLiterals();
  FILE *file = fopen(fileName, "r");
  if (!file)
    printError("File not found");
  State s = {.file = file,
             .isAtEnd = (file == NULL),
             .errorCount = (file == NULL),
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

/* -------------------------------------------------------------- literals */

static int isPrintable(int c) { return c >= 0x20 && c <= 0x7E; }

// A literal never spans lines; CR counts as a line end too, so that an
// unterminated literal on a CRLF line gives one error.
static int isLineEnd(int c) { return c == '\n' || c == '\r' || c == EOF; }

// value of the byte after a backslash, or -1 if it is no valid escape
static int escapeValue(int c, int inString) {
  switch (c) {
  case 'n': return '\n';
  case 't': return '\t';
  case 'r': return '\r';
  case '\\': return '\\';
  case '\'': return '\'';
  case '"': return '"';
  case '0': return inString ? -1 : 0; // strings are printed with %s
  default: return -1;
  }
}

// "\<X>" of an escape message: the byte if printable, else x and two hex digits
static void escapeName(int c, char *buf, size_t n) {
  if (isPrintable(c))
    snprintf(buf, n, "%c", c);
  else
    snprintf(buf, n, "x%02X", (unsigned char)c);
}

static void addSpelling(Token *t, int c) {
  if (t->lexemeSize < sizeof(t->lexeme) - 1)
    t->lexeme[t->lexemeSize++] = (char)c;
}

// After the opening ' : one character or escape, then the closing '. A byte
// that ends the line (or EOF) is left in *cp for the main loop, so line
// counting stays right; after any other error the rest of the literal is
// skipped up to its closing quote.
static void scanCharLiteral(State *s, int *cp) {
  Token t = newToken(TK_CHARLIT, s);
  char msg[80], name[8];
  addSpelling(&t, '\'');
  int c = fgetc(s->file);
  if (isLineEnd(c))
    goto unterminated;
  if (c == '\'') {
    printLexerError("empty character literal", s);
    return;
  }
  if (c == '\\') {
    addSpelling(&t, c);
    c = fgetc(s->file);
    if (isLineEnd(c))
      goto unterminated;
    if (escapeValue(c, 0) < 0) {
      escapeName(c, name, sizeof(name));
      snprintf(msg, sizeof(msg),
               "unknown escape sequence \\%s in character literal", name);
      printLexerError(msg, s);
      goto skip;
    }
  } else if (!isPrintable(c)) {
    snprintf(msg, sizeof(msg), "byte \\x%02X not allowed in character literal",
             (unsigned char)c);
    printLexerError(msg, s);
    goto skip;
  }
  addSpelling(&t, c);
  c = fgetc(s->file);
  if (c == '\'') {
    addSpelling(&t, c);
    appendToTokenList(t, s);
    return;
  }
  if (isLineEnd(c))
    goto unterminated;
  printLexerError("character literal must contain exactly one character", s);
skip:
  do
    c = fgetc(s->file);
  while (c != '\'' && !isLineEnd(c));
  if (c == '\'')
    return;
  *cp = c;
  s->scanNext = 0;
  return;
unterminated:
  printLexerError("unterminated character literal", s);
  *cp = c;
  s->scanNext = 0;
}

// After the opening " : bytes and escapes up to the closing ". Every error is
// reported (several per literal are possible) and suppresses the token.
static void scanStringLiteral(State *s, int *cp) {
  Token t = newToken(TK_STR, s);
  char text[MAX_STRING_LEN];
  char msg[80], name[8];
  int len = 0, errors = 0, tooLong = 0;
  addSpelling(&t, '"');
  for (;;) {
    int c = fgetc(s->file);
    if (isLineEnd(c)) {
      printLexerError("unterminated string literal", s);
      *cp = c;
      s->scanNext = 0;
      return;
    }
    addSpelling(&t, c);
    if (c == '"')
      break;
    int b = c;
    if (c == '\\') {
      c = fgetc(s->file);
      if (isLineEnd(c)) {
        printLexerError("unterminated string literal", s);
        *cp = c;
        s->scanNext = 0;
        return;
      }
      addSpelling(&t, c);
      b = escapeValue(c, 1);
      if (b < 0) {
        escapeName(c, name, sizeof(name));
        snprintf(msg, sizeof(msg),
                 "unknown escape sequence \\%s in string literal", name);
        printLexerError(msg, s);
        errors++;
        continue;
      }
    } else if (!isPrintable(c)) {
      snprintf(msg, sizeof(msg), "byte \\x%02X not allowed in string literal",
               (unsigned char)c);
      printLexerError(msg, s);
      errors++;
      continue;
    }
    if (len == MAX_STRING_LEN) {
      if (!tooLong)
        printLexerError("string literal longer than 255 characters", s);
      tooLong = 1;
      errors++;
      continue;
    }
    text[len++] = (char)b;
  }
  if (errors)
    return;
  t.literal = addStringLiteral(text, len);
  appendToTokenList(t, s);
}

int charLiteralValue(const char *lexeme) {
  if (lexeme[1] == '\\')
    return escapeValue((unsigned char)lexeme[2], 0);
  return (unsigned char)lexeme[1];
}

TokenList scan(State *s) {

  // int (not char) so that EOF is distinguishable on every platform
  int c = 0;

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
    case '\r': // tolerate Windows line endings
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

    case '#': {
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
    }

    case '_': {
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
    }
    case '%':
      appendToTokenList(newToken(TK_COMMENT, s),s);
      while (c != '\n' && c != EOF) {
        c = fgetc(s->file);
      }
      if (c == '\n')
        s->line++;
      else
        s->isAtEnd = 1;
      break;

    case EOF: {
      s->isAtEnd = 1;
      break;
    }

    case '\'':
      scanCharLiteral(s, &c);
      break;
    case '"':
      scanStringLiteral(s, &c);
      break;

    default:
      if (isNum(c)) {
        Token num = newToken(TK_NUM, s);
        int num_digits = 0;
        do {
          num_digits++;
          num.lexeme[num.lexemeSize++] = c;
          c = fgetc(s->file);
        } while (isNum(c) && num_digits < MAX_NUMBER_DIGITS);

        if (isNum(c)) {
          // too long for the lexeme buffer: report it and skip the whole
          // number (digits, fraction and exponent) instead of splitting it
          printLexerError("number has more than 23 digits", s);
          while (isNum(c))
            c = fgetc(s->file);
          if (c == '.')
            do
              c = fgetc(s->file);
            while (isNum(c));
          if (c == 'E') {
            c = fgetc(s->file);
            if (c == '+' || c == '-')
              c = fgetc(s->file);
            while (isNum(c))
              c = fgetc(s->file);
          }
          s->scanNext = 0;
          break;
        }

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
                      } else {
                        // the exponent needs exactly two digits
                        printLexerError("expected number after E", s);
                        s->scanNext = 0;
                        break;
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
            // a lone b, c or d is a one-letter field name
            s->scanNext = 0;
            var.type = TK_FIELDID;
            var.lexeme[var.lexemeSize] = '\0';
            appendToTokenList(var, s);
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
        char msg[80];
        if (c > 32 && c < 127)
          snprintf(msg, sizeof(msg), "%c not recognized", c);
        else // NUL, control characters and non-ASCII bytes
          snprintf(msg, sizeof(msg), "byte \\x%02X not recognized",
                   (unsigned char)c);
        printLexerError(msg, s);
      }
    }
  }
  appendToTokenList(newToken(TK_DOLLAR,s),s);
  if (s->file)
    fclose(s->file);
  s->file = NULL;
  return s->tokenList;
}

void removeComments(const char *filename) {
  State state = initializeState(filename,0);
  free(state.tokenList.buf);
  if (!state.file)
    return;
  int c = 0;
  int pending = 0; // a byte read ahead that ended a literal
  while (c != EOF) {
    if (pending) {
      pending = 0;
    } else {
      c = fgetc(state.file);
    }
    if (c == '%') {
      while (c != '\n' && c != EOF) {
        c = fgetc(state.file);
      }
    } else if (c == '"' || c == '\'') {
      // a % inside a string or character literal is not a comment: copy the
      // literal verbatim up to its closing quote, or up to the line end
      int quote = c;
      putchar(c);
      for (;;) {
        c = fgetc(state.file);
        if (isLineEnd(c)) {
          pending = 1; // the line end is copied by the main loop
          break;
        }
        putchar(c);
        if (c == quote)
          break;
        if (c == '\\') {
          c = fgetc(state.file);
          if (isLineEnd(c)) {
            pending = 1;
            break;
          }
          putchar(c);
        }
      }
      continue;
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
  free(tl.buf);

  printf("done\n");
}