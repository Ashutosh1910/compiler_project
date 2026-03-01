#ifndef PARSER_DEF_H
#define PARSER_DEF_H

#include "lexerDef.h"

#define MAX_RULES 200
#define MAX_RHS 20
#define MAX_NON_TERMINALS 60
#define MAX_SYMBOL_NAME 64
#define MAX_LINE_LEN 1024

/* ─── Symbol: terminal or non-terminal ─── */
typedef enum { SYM_TERMINAL, SYM_NON_TERMINAL } SymbolKind;

typedef struct {
  SymbolKind kind;
  int id; /* TokenType value if terminal, NT index if non-terminal */
} Symbol;

/* ─── Grammar rule: A → X1 X2 ... Xn ─── */
typedef struct {
  int lhs; /* non-terminal index */
  Symbol rhs[MAX_RHS];
  int rhsLen;
} GrammarRule;

/* ─── Grammar ─── */
typedef struct {
  GrammarRule rules[MAX_RULES];
  int numRules;
  char ntNames[MAX_NON_TERMINALS][MAX_SYMBOL_NAME]; /* index → name */
  int numNT;
} Grammar;

/* ─── Bitset for FIRST / FOLLOW (one uint64_t per word, enough for NUM_TOKENS)
 * ─── */
#define BITSET_WORDS 2 /* 128 bits, more than enough for ~65 tokens */
typedef struct {
  unsigned long long bits[BITSET_WORDS];
} BitSet;

/* ─── FIRST and FOLLOW arrays, one BitSet per non-terminal ─── */
typedef struct {
  BitSet first[MAX_NON_TERMINALS];
  BitSet follow[MAX_NON_TERMINALS];
} FirstFollowSets;

/* ─── Parse table: table[nt][terminal] = rule index (-1 = error) ─── */
typedef struct {
  int table[MAX_NON_TERMINALS][NUM_TOKENS];
} ParseTable;

/* ─── Parse tree node ─── */
typedef struct TreeNode {
  Symbol sym;
  char lexeme[31];
  int lineNo;
  int ruleIndex; /* which grammar rule expanded this NT (-1 for leaf) */
  struct TreeNode *firstChild;
  struct TreeNode *nextSibling;
} TreeNode;

/* ─── Error list ─── */
typedef struct SyntaxError {
  int lineNo;
  char message[256];
  struct SyntaxError *next;
} SyntaxError;

#endif
