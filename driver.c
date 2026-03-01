#include "lexer.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, const char **args) {
  int n = 1;
  if (argc < 2) {
    printf("Enter file name to parse/lex\n");
    exit(1);
  }
  while (n != 0) {
    printf("Enter your choice:\n");
    printf("  0 - Exit\n");
    printf("  1 - Remove Comments\n");
    printf("  2 - Print Tokens\n");
    printf("  3 - Parse & Print Parse Tree\n");
    scanf("%d", &n);
    switch (n) {
    case 0:
      continue;
    case 1: {
      removeComments(args[1]);
      break;
    }
    case 2: {
      printTokens(args[1]);
      break;
    }
    case 3: {
      /* Lex the source file */
      clock_t start_time, end_time;
     double total_CPU_time, total_CPU_time_in_seconds;
      start_time = clock();
      State state = initializeState(args[1]);
      TokenList tl = scan(&state);
      printf("Lexing complete: %d tokens\n", tl.size);

      /* Load grammar */
      Grammar *grammar = loadGrammar("grammar.txt");
      if (!grammar) {
        printf("Failed to load grammar\n");
        break;
      }
      printf("Grammar loaded: %d rules, %d non-terminals\n", grammar->numRules,
             grammar->numNT);

      /* Compute FIRST and FOLLOW sets */
      FirstFollowSets ff;
      computeFirstAndFollow(grammar, &ff);
      printf("FIRST and FOLLOW sets computed\n");

      /* Build parse table */
      ParseTable pt;
      createParseTable(grammar, &ff, &pt);
      printf("Parse table constructed\n");

      /* Parse */
      SyntaxError *errors = NULL;
      TreeNode *tree = parseTokens(&tl, grammar, &pt, &ff, &errors);

      /* Output */
      if (errors) {
        printSyntaxErrors(errors);
        freeSyntaxErrors(errors);
      }

      if (tree) {
        printParseTreeFull(grammar, tree, "parseTreeOutput.txt");
        printf("Parse tree written to parseTreeOutput.txt\n");
        freeParseTree(tree);
      }

      freeGrammar(grammar);
      free(tl.buf);
      end_time = clock();

      total_CPU_time = (double)(end_time - start_time);
      total_CPU_time_in_seconds = total_CPU_time / CLOCKS_PER_SEC;
      printf("\nTotal CPU time taken: %f milliseconds\n", total_CPU_time);
      printf("\nTotal CPU time taken: %f seconds\n", total_CPU_time_in_seconds);
      break;
    }
    default: {
      printf("wrong choice\n");
    }
    }
  }
  return 0;
}