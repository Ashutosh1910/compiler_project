//Group 51
//Ashutosh Desai - 2023A7PS0675P
//Anushka Doshi - 2023A7PS0597P
//Aarya Jain - 2023A7PS0618P
//Devansh Agarwal - 2023A7PS0570P
#include "lexer.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>


int main(int argc, const char **args) {
  char n = '1';
  if (argc < 3) {
    printf("Enter file name to analyse and output file name\n");
    exit(1);
  }
  while (n != '0') {
    printf("Enter your choice:\n");
    printf("  0 - Exit\n");
    printf("  1 - Remove Comments\n");
    printf("  2 - Print Tokens\n");
    printf("  3 - Parse & Print Parse Tree\n");
    printf("  4 - Print time taken for lexical analysis and syntax analysis\n");

    scanf("\n%c", &n);
    switch (n) {
    case '0':
      continue;
    case '1': {
      removeComments(args[1]);
      break;
    }
    case '2': {
      printTokens(args[1]);
      break;
    }
    case '3': {
      parseWithPrinting(args[1],args[2]);
      break;
    }
    case '4': {
      clock_t start_time, end_time;
      double total_CPU_time, total_CPU_time_in_seconds;
      start_time = clock();
      parseWithoutPrinting(args[1]);
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
