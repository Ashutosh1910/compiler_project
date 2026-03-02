// Ashutosh Desai - 2023A7PS0675P
// Anushka Doshi - 2023A7PS0597P
// Aarya Jain - 2023A7PS0618P
// Devansh Agarwal - 2023A7PS0570P
#ifndef PARSER_DEF_H
#define PARSER_DEF_H

#include "lexerDef.h"
#define TERM_MAP_CAP 128
#define MAX_RULES 100
#define MAX_RHS 20
#define MAX_NON_TERMINALS 60
#define MAX_SYMBOL_NAME                                                        \
  64 // preferring to store known sized(or know max size) strings in stack for
     // faster memory access
#define MAX_LINE_LEN 1024
#define STACK_CAP 4096
typedef enum { SYM_TERMINAL, SYM_NON_TERMINAL } SymbolKind;

typedef struct {
  SymbolKind kind;
  int id;
} Symbol;

typedef struct {
  int lhs;
  Symbol rhs[MAX_RHS];
  int rhsLen;
} GrammarRule;

typedef struct {
  GrammarRule rules[MAX_RULES];
  int numRules;
  char ntNames[MAX_NON_TERMINALS][MAX_SYMBOL_NAME];
  int numNT;
} Grammar;

#define BITSET_WORDS 2
typedef struct {
  unsigned long long bits[BITSET_WORDS];
} BitSet;

typedef struct {
  BitSet first[MAX_NON_TERMINALS]; // used bloom filter for first and follow
                                   // beacuse we only need to check existence
                                   // mostly
  BitSet follow[MAX_NON_TERMINALS];
} FirstFollowSets;

typedef struct {
  int table[MAX_NON_TERMINALS][NUM_TOKENS];
} ParseTable;

typedef struct TreeNode {
  Symbol sym;
  char lexeme[31];
  int lineNo;
  int ruleIndex;
  struct TreeNode *firstChild;
  struct TreeNode
      *nextSibling; // we used this struct for syntax as each node can have
                    // multiple children we need to traverse them inorder
} TreeNode;

typedef struct SyntaxError {
  int lineNo;
  char message[256];
  struct SyntaxError *next;
} SyntaxError;

typedef struct {
  Symbol sym;
  TreeNode *node;
} StackEntry;

typedef struct {
  StackEntry data[STACK_CAP];
  int top;
} Stack;

typedef struct {
  const char *key;
  TokenType val;
} TermMapEntry;

#endif
