#ifndef PARSER_H
#define PARSER_H

#include "parserDef.h"

/* Grammar loading */
Grammar *loadGrammar(const char *filename);
void freeGrammar(Grammar *g);

/* FIRST / FOLLOW */
void computeFirstAndFollow(Grammar *g, FirstFollowSets *ff);

/* Parse table */
void createParseTable(Grammar *g, FirstFollowSets *ff, ParseTable *pt);

/* Parsing */
TreeNode *parseTokens(TokenList *tokens, Grammar *g, ParseTable *pt,
                      FirstFollowSets *ff, SyntaxError **errors);

/* Tree output */
void printParseTree(TreeNode *root, int depth, FILE *out);
void printParseTreeFull(Grammar *g, TreeNode *root, const char *outfile);
void freeParseTree(TreeNode *root);

/* Error list */
void printSyntaxErrors(SyntaxError *errors);
void freeSyntaxErrors(SyntaxError *errors);

/* Utility */
const char *getNTName(Grammar *g, int ntIndex);

#endif
