// Group 51
// Ashutosh Desai - 2023A7PS0675P
// Anushka Doshi - 2023A7PS0597P
// Aarya Jain - 2023A7PS0618P
// Devansh Agarwal - 2023A7PS0570P
#include "compiler.h"
#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int runFrontEnd(const char *file, Compilation *c) {
  memset(c, 0, sizeof(*c));
  State state = initializeState(file, 0);
  c->tokens = scan(&state);
  c->lexErrors = state.errorCount;

  c->grammar = loadGrammar(getGrammarFile());
  if (!c->grammar) {
    printf("Failed to load grammar from %s\n", getGrammarFile());
    return 0;
  }
  computeFirstAndFollow(c->grammar, &c->ff);
  createParseTable(c->grammar, &c->ff, &c->pt);

  SyntaxError *errors = NULL;
  c->tree = parseTokens(&c->tokens, c->grammar, &c->pt, &c->ff, &errors, 1);
  for (SyntaxError *e = errors; e; e = e->next)
    c->syntaxErrors++;
  printSyntaxErrors(errors);
  freeSyntaxErrors(errors);
  return c->lexErrors == 0 && c->syntaxErrors == 0;
}

int runAnalysis(const char *file, Compilation *c) {
  if (!runFrontEnd(file, c))
    return 0;
  c->ast = buildAST(c->grammar, c->tree);
  c->symbols = semanticAnalysis(c->ast, &c->semanticErrors, stdout);
  return c->semanticErrors == 0;
}

int compileToAssembly(const char *file, const char *asmFile) {
  Compilation c;
  int ok = runAnalysis(file, &c);
  if (ok) {
    FILE *out = fopen(asmFile, "w");
    if (!out) {
      perror(asmFile);
      ok = 0;
    } else {
      generateCode(c.ast, c.symbols, out);
      fclose(out);
    }
  }
  freeCompilation(&c);
  return ok;
}

// runs argv[0] with the given arguments and waits for it
static int runTool(char *const argv[]) {
  pid_t pid = fork();
  if (pid < 0)
    return 0;
  if (pid == 0) {
    execvp(argv[0], argv);
    perror(argv[0]);
    _exit(127);
  }
  int status;
  if (waitpid(pid, &status, 0) < 0)
    return 0;
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int buildExecutable(const char *file, const char *exeFile) {
  size_t n = strlen(exeFile) + 8;
  char *asmFile = malloc(n), *objFile = malloc(n);
  snprintf(asmFile, n, "%s.asm", exeFile);
  snprintf(objFile, n, "%s.o", exeFile);
  int ok = compileToAssembly(file, asmFile);
  if (ok) {
    char *nasm[] = {"nasm", "-f", "elf64", asmFile, "-o", objFile, NULL};
    char *gcc[] = {"gcc", "-no-pie", objFile, "-o", (char *)exeFile, NULL};
    ok = runTool(nasm) && runTool(gcc);
    if (!ok)
      printf("Assembling or linking %s failed\n", asmFile);
    remove(objFile);
  }
  free(asmFile);
  free(objFile);
  return ok;
}

void freeCompilation(Compilation *c) {
  freeSymbolTable(c->symbols);
  freeAST(c->ast);
  freeParseTree(c->tree);
  freeGrammar(c->grammar);
  free(c->tokens.buf);
  memset(c, 0, sizeof(*c));
}
