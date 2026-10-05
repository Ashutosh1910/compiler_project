//Group 51
//Ashutosh Desai - 2023A7PS0675P
//Anushka Doshi - 2023A7PS0597P
//Aarya Jain - 2023A7PS0618P
//Devansh Agarwal - 2023A7PS0570P
#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Use grammar.txt from the current directory, or else the copy next to the
// executable, so the compiler also works when run from another directory.
static char grammarPath[4096];
static void locateGrammar(const char *argv0) {
  FILE *f = fopen("grammar.txt", "r");
  if (f) {
    fclose(f);
    return;
  }
  const char *slash = strrchr(argv0, '/');
  if (!slash)
    return;
  snprintf(grammarPath, sizeof(grammarPath), "%.*s/grammar.txt",
           (int)(slash - argv0), argv0);
  setGrammarFile(grammarPath);
}

static int printAst(const char *file) {
  Compilation c;
  int ok = runFrontEnd(file, &c);
  if (ok) {
    c.ast = buildAST(c.grammar, c.tree);
    printAST(c.ast, stdout);
  }
  freeCompilation(&c);
  return ok;
}

static int printSymbols(const char *file) {
  Compilation c;
  int ok = runAnalysis(file, &c);
  if (c.symbols)
    printSymbolTable(c.symbols, stdout);
  freeCompilation(&c);
  return ok;
}

static int checkSemantics(const char *file) {
  Compilation c;
  int ok = runAnalysis(file, &c);
  if (ok)
    printf("Code compiles successfully: no lexical, syntax or semantic "
           "errors\n");
  freeCompilation(&c);
  return ok;
}

static int generate(const char *file, const char *asmFile) {
  int ok = compileToAssembly(file, asmFile);
  if (ok)
    printf("Assembly written to %s\n", asmFile);
  return ok;
}

static void usage(void) {
  printf("usage: ./compiler <source> <output>            (interactive menu)\n"
         "       ./compiler --tokens  <source>\n"
         "       ./compiler --parse   <source> <parse-tree-file>\n"
         "       ./compiler --ast     <source>\n"
         "       ./compiler --symbols <source>\n"
         "       ./compiler --check   <source>\n"
         "       ./compiler --asm     <source> <asm-file>\n"
         "       ./compiler --build   <source> <executable>\n");
}

// Non-interactive mode, used by scripts and the test suite.
// Exit status: 0 success, 1 errors in the source, 2 bad usage.
static int runFlag(int argc, const char **args) {
  const char *flag = args[1];
  int needsOut = !strcmp(flag, "--parse") || !strcmp(flag, "--asm") ||
                 !strcmp(flag, "--build");
  if (argc != (needsOut ? 4 : 3)) {
    usage();
    return 2;
  }
  const char *src = args[2], *dst = needsOut ? args[3] : NULL;
  int ok;
  if (!strcmp(flag, "--tokens")) {
    State s = initializeState(src, 1);
    TokenList tl = scan(&s);
    free(tl.buf);
    ok = s.errorCount == 0;
  } else if (!strcmp(flag, "--parse")) {
    Compilation c;
    ok = runFrontEnd(src, &c);
    if (c.tree && !printParseTreeFull(c.grammar, c.tree, dst))
      ok = 0;
    freeCompilation(&c);
  } else if (!strcmp(flag, "--ast")) {
    ok = printAst(src);
  } else if (!strcmp(flag, "--symbols")) {
    ok = printSymbols(src);
  } else if (!strcmp(flag, "--check")) {
    ok = checkSemantics(src);
  } else if (!strcmp(flag, "--asm")) {
    ok = generate(src, dst);
  } else if (!strcmp(flag, "--build")) {
    ok = buildExecutable(src, dst);
  } else {
    usage();
    return 2;
  }
  return ok ? 0 : 1;
}

int main(int argc, const char **args) {
  int n = 1;
  locateGrammar(args[0]);
  if (argc >= 2 && strncmp(args[1], "--", 2) == 0)
    return runFlag(argc, args);
  if (argc < 3) {
    printf("Enter file name to analyse and output file name\n");
    usage();
    exit(1);
  }
  while (n != 0) {
    printf("Enter your choice:\n");
    printf("  0 - Exit\n");
    printf("  1 - Remove Comments\n");
    printf("  2 - Print Tokens\n");
    printf("  3 - Parse & Print Parse Tree\n");
    printf("  4 - Print time taken for lexical analysis and syntax analysis\n");
    printf("  5 - Print Abstract Syntax Tree\n");
    printf("  6 - Print Symbol Table\n");
    printf("  7 - Semantic Analysis (type checking)\n");
    printf("  8 - Generate Assembly Code (written to the output file)\n");

    // read a whole line so that "10" is one (invalid) choice, not 1 then 0
    char line[64], *end;
    do {
      if (!fgets(line, sizeof(line), stdin))
        return 0; // end of input
    } while (line[strspn(line, " \t\r\n")] == '\0'); // skip blank lines
    n = (int)strtol(line, &end, 10);
    if (end == line || end[strspn(end, " \t\r\n")] != '\0')
      n = -1;
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
      parseWithPrinting(args[1],args[2]);
      break;
    }
    case 4: {
      clock_t start_time, end_time;
      double total_CPU_time, total_CPU_time_in_seconds;
      start_time = clock();
      parseWithoutPrinting(args[1]);
      end_time = clock();
      total_CPU_time = (double)(end_time - start_time);
      total_CPU_time_in_seconds = total_CPU_time / CLOCKS_PER_SEC;
      printf("\nTotal CPU time taken: %f clock ticks (%f milliseconds)\n",
             total_CPU_time, total_CPU_time_in_seconds * 1000.0);
      printf("\nTotal CPU time taken: %f seconds\n", total_CPU_time_in_seconds);
      break;
    }
    case 5:
      printAst(args[1]);
      break;
    case 6:
      printSymbols(args[1]);
      break;
    case 7:
      checkSemantics(args[1]);
      break;
    case 8:
      generate(args[1], args[2]);
      break;
    default: {
      printf("wrong choice\n");
    }
    }
  }
  return 0;
}
