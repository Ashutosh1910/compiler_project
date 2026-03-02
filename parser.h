//Group 51
// Ashutosh Desai - 2023A7PS0675P
// Anushka Doshi - 2023A7PS0597P
// Aarya Jain - 2023A7PS0618P
// Devansh Agarwal - 2023A7PS0570P
#ifndef PARSER_H
#define PARSER_H

#include "parserDef.h"
#include <stdio.h>

// terminal name -> TokenType hashmap
unsigned int termHash(const char *s);
void termMapInit(void);

// token access helpers
TokenType curToken(TokenList *tokens, int tokenIdx);
int curLine(TokenList *tokens, int tokenIdx);
const char *curLexeme(TokenList *tokens, int tokenIdx);

// bitset helpers
void bs_clear(BitSet *s);
void bs_add(BitSet *s, int bit);
int bs_contains(const BitSet *s, int bit);
int bs_union(BitSet *dst, const BitSet *src);
int bs_union_no_eps(BitSet *dst, const BitSet *src);
int bs_is_empty(const BitSet *s);

// terminal/symbol helpers
TokenType terminalFromString(const char *name);
int findOrAddNT(Grammar *g, const char *name);
Symbol makeSymbol(Grammar *g, const char *tok);
void addRule(Grammar *g, int lhsNT, Symbol *rhs, int rhsLen);

// grammar loading
Grammar *loadGrammar(const char *filename);
void freeGrammar(Grammar *g);
const char *getNTName(Grammar *g, int ntIndex);

// FIRST and FOLLOW computation
int firstOfString(Grammar *g, FirstFollowSets *ff, Symbol *syms, int count,
                  BitSet *dst);
//We use dfs to resolve dependencies in first and follow when first of A depends on first of B, so we can compute first of B first and then first of A, thus skipping multiple passes
void computeFirstDFS(Grammar *g, FirstFollowSets *ff, int nt, int *visited);
void computeFollowDFS(Grammar *g, FirstFollowSets *ff, int nt, int *visited);
void computeFirstAndFollow(Grammar *g, FirstFollowSets *ff);

// parse table
void createParseTable(Grammar *g, FirstFollowSets *ff, ParseTable *pt);

// parsetree
TreeNode *newTreeNode(Symbol sym);
void addChild(TreeNode *parent, TreeNode *child);

// error reporting
void addSyntaxError(SyntaxError **head, int lineNo, const char *message);
void printSyntaxErrors(SyntaxError *errors);
void freeSyntaxErrors(SyntaxError *errors);

// stack
void stackInit(Stack *s);
int stackEmpty(Stack *s);
void stackPush(Stack *s, Symbol sym, TreeNode *node);
StackEntry stackPop(Stack *s);
StackEntry stackPeek(Stack *s);

// error recovery
int isSyncToken(int tok);
int isInSyncOrFollow(FirstFollowSets *ff, int nt, int tok);

void skipComments(TokenList *tokens, int *tokenIdx);

// parsing
TreeNode *parseTokens(TokenList *tokens, Grammar *g, ParseTable *pt,
                      FirstFollowSets *ff, SyntaxError **errors, int buildTree);

// printing
void printNodeRow(Grammar *g, TreeNode *node, const char *parentSymbol,
                  FILE *out);
void printTreeInorder(Grammar *g, TreeNode *node, const char *parentSymbol,
                      FILE *out);
void printParseTree(TreeNode *root, int depth, FILE *out);
void printParseTreeFull(Grammar *g, TreeNode *root, const char *outfile);
void freeParseTree(TreeNode *node);
// for driver
void parseWithoutPrinting(const char *filename);
void parseWithPrinting(const char *filename, const char *outputfile);

#endif
