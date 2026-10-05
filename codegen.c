// Group 51
// Ashutosh Desai - 2023A7PS0675P
// Anushka Doshi - 2023A7PS0597P
// Aarya Jain - 2023A7PS0618P
// Devansh Agarwal - 2023A7PS0570P
//
// Code generation: a single walk over the annotated AST that prints NASM.
//
// Registers
//   rax   int results           xmm0  real results
//   rcx   right int operand     xmm1  right real operand
//   Intermediate values of an expression are kept on the machine stack.
//
// Stack frame of a function (grows down):
//
//   [rbp + 16 + inSize + k]  output parameters  \  block reserved by the
//   [rbp + 16 + k]           input parameters    /  caller (paramSize bytes)
//   [rbp + 8]                return address
//   [rbp]                    saved rbp
//   [rbp - offset]           locals (localSize bytes, zeroed on entry)
//
// A call copies the actual inputs into a fresh block below the caller's rsp,
// zeroes the outputs, calls, then copies the outputs back (copy-in /
// copy-out). Because frame and block sizes are multiples of 16, rsp is
// 16-byte aligned at every call, as the System V ABI requires for printf.
// Each activation has its own frame and block, so recursion needs nothing
// more.
//
// Arrays are laid out like records: a local array occupies
// [rbp - offset, rbp - offset + 8 * length), a global one is G_<name> in
// .bss, and element i is at base + 8 * i. Every element access computes its
// index into rax, checks 0 <= index < length (one unsigned compare) and
// jumps to rt_index_error when it fails; then rcx holds the base.
//
// Stack guard: main stores the top of the stack in rt_stack_top, and every
// other function checks on entry that the stack used so far plus its own
// activation (FuncEntry.stackNeed, see semantic.c) stays within
// MAX_STACK_USE; a deeper recursion stops at rt_stack_overflow instead of
// crashing. The static check bounds every non-recursive chain by the same
// amount, so the guard never fires for those.
//
// Runtime errors (division by zero, index out of bounds, stack overflow) and
// all output go to stdout through C stdio, so they appear in program order.
#include "codegen.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static FILE *out;
static FuncEntry *cur;
static int labelCount;
static int stringCount; // S<k> labels of print's string literals

static int newLabel(void) { return labelCount++; }

static void emit(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fputs("    ", out);
  vfprintf(out, fmt, ap);
  fputc('\n', out);
  va_end(ap);
}

static void emitLabel(int l) { fprintf(out, "L%d:\n", l); }

// memory operand for byte `off` inside variable v
static const char *addr(VarEntry *v, int off) {
  static char buf[4][96]; // a few results may be alive in one emit call
  static int next;
  char *b = buf[next];
  next = (next + 1) % 4;
  switch (v->kind) {
  case VAR_GLOBAL:
    snprintf(b, 96, "[rel G_%s + %d]", v->name, off);
    break;
  case VAR_LOCAL:
    snprintf(b, 96, "[rbp - %d]", v->offset - off);
    break;
  default: // parameters live in the caller's block above the return address
    snprintf(b, 96, "[rbp + %d]", 16 + v->offset + off);
  }
  return b;
}

static const char *varAddr(AstVarRef *v, int extra) {
  return addr(v->entry, v->offset + extra);
}

static void genInt(Expr *e);

// Element access: index -> rax, bounds check, base of the array -> rcx, so
// the element is [rcx + rax*8]. Clobbers rax and rcx only (and whatever the
// index expression uses).
static void genElementAddress(AstVarRef *v) {
  Type *array = v->entry->type;
  int ok = newLabel();
  genInt(v->index);
  emit("cmp rax, %d", array->length);
  emit("jb L%d", ok); // unsigned: a negative index looks huge and fails too
  emit("mov rsi, rax");
  emit("lea rdx, [rel AN_%s]", v->name);
  emit("mov rcx, %d", array->length);
  emit("mov r8, %d", v->line);
  emit("jmp rt_index_error");
  emitLabel(ok);
  emit("lea rcx, %s", addr(v->entry, 0));
}

/* ---------------------------------------------------------------- records */

// scalar leaves of a record in memory order, e.g. #line -> 4 reals
typedef struct {
  int offset;
  TypeKind kind;
} Leaf;

static int collectLeaves(Type *t, int base, Leaf *leaves, int n) {
  if (isScalar(t)) {
    leaves[n].offset = base;
    leaves[n].kind = t->kind;
    return n + 1;
  }
  for (int i = 0; i < t->numFields; i++)
    n = collectLeaves(t->fields[i].type, base + t->fields[i].offset, leaves,
                      n);
  return n;
}

static int countLeaves(Type *t) {
  if (isScalar(t))
    return 1;
  int n = 0;
  for (int i = 0; i < t->numFields; i++)
    n += countLeaves(t->fields[i].type);
  return n;
}

// Copies size bytes between two memory operands that do not overlap: small
// blocks qword by qword, large ones with rep movsq. Clobbers rax, rcx, rsi
// and rdi.
static void blockMove(const char *dst, const char *src, int size) {
  emit("lea rdi, %s", dst);
  emit("lea rsi, %s", src);
  if (size <= 8 * SCALAR_SIZE) {
    for (int k = 0; k < size; k += SCALAR_SIZE) {
      emit("mov rax, qword [rsi + %d]", k);
      emit("mov qword [rdi + %d], rax", k);
    }
  } else {
    emit("mov rcx, %d", size / SCALAR_SIZE);
    emit("rep movsq");
  }
}

// The value is staged on the stack before any of it is stored, so the copy
// is correct even when source and destination overlap (two members of one
// union).
static void copyBytes(const char *what, int size, VarEntry *src, int srcOff,
                      VarEntry *dst, int dstOff) {
  if (src == dst && srcOff == dstOff)
    return;
  emit("; copy %s (%d bytes)", what, size);
  emit("sub rsp, %d", size);
  blockMove("[rsp]", addr(src, srcOff), size);
  blockMove(addr(dst, dstOff), "[rsp]", size);
  emit("add rsp, %d", size);
}

static Leaf *leavesOf(Type *t, int *n) {
  *n = countLeaves(t);
  Leaf *leaves = malloc(sizeof(Leaf) * (*n > 0 ? *n : 1));
  collectLeaves(t, 0, leaves, 0);
  return leaves;
}

/* ------------------------------------------------------------ expressions */

static void genReal(Expr *e);

static void loadRealConst(double d, const char *text) {
  unsigned long long bits;
  memcpy(&bits, &d, sizeof(bits));
  emit("mov rax, 0x%016llx ; %s", bits, text);
  emit("movq xmm0, rax");
}

static void intOp(TokenType op) {
  switch (op) {
  case TK_PLUS:
    emit("add rax, rcx");
    break;
  case TK_MINUS:
    emit("sub rax, rcx");
    break;
  case TK_MUL:
    emit("imul rax, rcx");
    break;
  default: {
    // idiv traps on x / 0 and on INT64_MIN / -1; handle both explicitly
    int div = newLabel(), done = newLabel();
    emit("test rcx, rcx");
    emit("jz rt_div_by_zero");
    emit("cmp rcx, -1");
    emit("jne L%d", div);
    emit("neg rax");
    emit("jmp L%d", done);
    emitLabel(div);
    emit("cqo");
    emit("idiv rcx");
    emitLabel(done);
  }
  }
}

static void realOp(TokenType op) {
  static const char *names[] = {"addsd", "subsd", "mulsd", "divsd"};
  int i = op == TK_PLUS ? 0 : op == TK_MINUS ? 1 : op == TK_MUL ? 2 : 3;
  emit("%s xmm0, xmm1", names[i]);
}

// int-valued expression -> rax
static void genInt(Expr *e) {
  switch (e->kind) {
  case EXPR_NUM:
    emit("mov rax, %lld", e->ival);
    break;
  case EXPR_VAR:
    if (e->var.index) {
      genElementAddress(&e->var);
      emit("mov rax, qword [rcx + rax*8]");
    } else {
      emit("mov rax, qword %s", varAddr(&e->var, 0));
    }
    break;
  case EXPR_BINOP:
    // the right operand may clobber rcx (an element access does), but
    // leaves the stack as it found it
    genInt(e->left);
    emit("push rax");
    genInt(e->right);
    emit("mov rcx, rax");
    emit("pop rax");
    intOp(e->op);
    break;
  case EXPR_RNUM: // never int-typed
    break;
  }
}

static void pushXmm0(void) {
  emit("sub rsp, 8");
  emit("movsd qword [rsp], xmm0");
}

static void popXmm0ToXmm1AndRestore(void) {
  emit("movsd xmm1, xmm0");
  emit("movsd xmm0, qword [rsp]");
  emit("add rsp, 8");
}

// any scalar expression -> xmm0 as a double
static void genReal(Expr *e) {
  if (e->type->kind == TY_INT) {
    genInt(e);
    emit("cvtsi2sd xmm0, rax");
    return;
  }
  switch (e->kind) {
  case EXPR_RNUM:
    loadRealConst(e->rval, e->text);
    break;
  case EXPR_VAR:
    if (e->var.index) {
      genElementAddress(&e->var);
      emit("movsd xmm0, qword [rcx + rax*8]");
    } else {
      emit("movsd xmm0, qword %s", varAddr(&e->var, 0));
    }
    break;
  case EXPR_BINOP:
    genReal(e->left);
    pushXmm0();
    genReal(e->right);
    popXmm0ToXmm1AndRestore();
    realOp(e->op);
    break;
  case EXPR_NUM: // int-typed, handled above
    break;
  }
}

static void genScalar(Expr *e, TypeKind want) {
  if (want == TY_INT)
    genInt(e);
  else
    genReal(e);
}

// Value of one scalar leaf (at `off` inside the record type of e) of a
// record-valued expression, computed field by field:
//   (a + b).f = a.f + b.f        (a * s).f = a.f * s        (a / s).f = a.f / s
static void genLeaf(Expr *e, int off, TypeKind kind) {
  if (e->kind == EXPR_VAR) {
    if (kind == TY_INT)
      emit("mov rax, qword %s", varAddr(&e->var, off));
    else
      emit("movsd xmm0, qword %s", varAddr(&e->var, off));
    return;
  }
  Expr *rec = e->left, *other = e->right;
  int scalarRight = 1;
  if (isScalar(e->left->type)) { // s * rec
    rec = e->right;
    other = e->left;
    scalarRight = 0;
  }
  // left operand first, so subtraction and division keep their order
  if (scalarRight)
    genLeaf(rec, off, kind);
  else
    genScalar(other, kind);
  if (kind == TY_INT)
    emit("push rax");
  else
    pushXmm0();
  if (!scalarRight)
    genLeaf(rec, off, kind);
  else if (isAggregate(other->type))
    genLeaf(other, off, kind);
  else
    genScalar(other, kind);
  if (kind == TY_INT) {
    emit("mov rcx, rax");
    emit("pop rax");
    intOp(e->op);
  } else {
    popXmm0ToXmm1AndRestore();
    realOp(e->op);
  }
}

/* -------------------------------------------------------------- booleans */

static void genCond(BoolExpr *b, int lTrue, int lFalse) {
  switch (b->kind) {
  case BOOL_AND: {
    int mid = newLabel();
    genCond(b->left, mid, lFalse);
    emitLabel(mid);
    genCond(b->right, lTrue, lFalse);
    return;
  }
  case BOOL_OR: {
    int mid = newLabel();
    genCond(b->left, lTrue, mid);
    emitLabel(mid);
    genCond(b->right, lTrue, lFalse);
    return;
  }
  case BOOL_NOT:
    genCond(b->left, lFalse, lTrue);
    return;
  case BOOL_REL:
    break;
  }

  // An operand may be an element access, whose index is any expression, so
  // the right operand waits on the stack while the left one is evaluated.
  if (b->lhs->type->kind == TY_INT && b->rhs->type->kind == TY_INT) {
    static const char *jumps[] = {"jl", "jle", "je", "jg", "jge", "jne"};
    genInt(b->rhs);
    emit("push rax");
    genInt(b->lhs);
    emit("pop rcx");
    emit("cmp rax, rcx");
    emit("%s L%d", jumps[b->relop - TK_LT], lTrue);
    emit("jmp L%d", lFalse);
    return;
  }
  genReal(b->rhs);
  pushXmm0();
  genReal(b->lhs);
  emit("movsd xmm1, qword [rsp]");
  emit("add rsp, 8");
  // ucomisd sets CF/ZF/PF; unordered (NaN) compares are false except for !=
  switch (b->relop) {
  case TK_LT:
    emit("ucomisd xmm1, xmm0");
    emit("ja L%d", lTrue);
    break;
  case TK_LE:
    emit("ucomisd xmm1, xmm0");
    emit("jae L%d", lTrue);
    break;
  case TK_GT:
    emit("ucomisd xmm0, xmm1");
    emit("ja L%d", lTrue);
    break;
  case TK_GE:
    emit("ucomisd xmm0, xmm1");
    emit("jae L%d", lTrue);
    break;
  case TK_EQ:
    emit("ucomisd xmm0, xmm1");
    emit("jp L%d", lFalse);
    emit("je L%d", lTrue);
    break;
  default: // TK_NE
    emit("ucomisd xmm0, xmm1");
    emit("jp L%d", lTrue);
    emit("jne L%d", lTrue);
  }
  emit("jmp L%d", lFalse);
}

/* ------------------------------------------------------------- statements */

static void genStmts(Stmt *s);

static void genAssign(Stmt *s) {
  Type *t = s->lhs.type;
  if (s->lhs.index) {
    // the value first, then the index (A.7.3), so the value waits on the
    // stack while the index is computed and checked
    if (t->kind == TY_INT) {
      genInt(s->rhs);
      emit("push rax");
      genElementAddress(&s->lhs);
      emit("pop rdx");
      emit("mov qword [rcx + rax*8], rdx");
    } else {
      genReal(s->rhs);
      pushXmm0();
      genElementAddress(&s->lhs);
      emit("movsd xmm0, qword [rsp]");
      emit("add rsp, 8");
      emit("movsd qword [rcx + rax*8], xmm0");
    }
  } else if (t->kind == TY_INT) {
    genInt(s->rhs);
    emit("mov qword %s, rax", varAddr(&s->lhs, 0));
  } else if (t->kind == TY_REAL) {
    genReal(s->rhs);
    emit("movsd qword %s, xmm0", varAddr(&s->lhs, 0));
  } else if (s->rhs->kind == EXPR_VAR) {
    copyBytes("record", t->size, s->rhs->var.entry, s->rhs->var.offset,
              s->lhs.entry, s->lhs.offset);
  } else {
    // Compute every field before storing any of them, so that
    // r <--- r / r.n still divides every field by the old r.n.
    int n;
    Leaf *leaves = leavesOf(t, &n);
    for (int i = 0; i < n; i++) {
      genLeaf(s->rhs, leaves[i].offset, leaves[i].kind);
      if (leaves[i].kind == TY_REAL)
        emit("movq rax, xmm0");
      emit("push rax");
    }
    for (int i = n - 1; i >= 0; i--) {
      emit("pop rax");
      emit("mov qword %s, rax", varAddr(&s->lhs, leaves[i].offset));
    }
    free(leaves);
  }
}

static void callPrintf(const char *fmt, int isReal) {
  emit("lea rdi, [rel %s]", fmt);
  emit("mov eax, %d", isReal ? 1 : 0); // number of vector registers used
  emit("call printf wrt ..plt");
}

static void genRead(Stmt *s) {
  AstVarRef *v = &s->ioArg->var;
  if (v->index) { // checked before any input is consumed
    genElementAddress(v);
    emit("lea rsi, [rcx + rax*8]");
    emit("lea rdi, [rel %s]",
         v->type->kind == TY_INT ? "fmt_read_int" : "fmt_read_real");
    emit("xor eax, eax");
    emit("call scanf wrt ..plt");
    return;
  }
  int n;
  Leaf *leaves = leavesOf(v->type, &n);
  for (int i = 0; i < n; i++) {
    emit("lea rsi, %s", varAddr(v, leaves[i].offset));
    emit("lea rdi, [rel %s]",
         leaves[i].kind == TY_INT ? "fmt_read_int" : "fmt_read_real");
    emit("xor eax, eax");
    emit("call scanf wrt ..plt");
  }
  free(leaves);
}

static void genWrite(Stmt *s) {
  Expr *e = s->ioArg;
  if (e->kind != EXPR_VAR) {
    if (e->kind == EXPR_NUM) {
      emit("mov rsi, %lld", e->ival);
      callPrintf("fmt_int_nl", 0);
    } else {
      loadRealConst(e->rval, e->text);
      callPrintf("fmt_real_nl", 1);
    }
    return;
  }
  if (e->var.index) { // one element: printed like a scalar variable
    if (e->type->kind == TY_INT) {
      genInt(e);
      emit("mov rsi, rax");
      callPrintf("fmt_int_nl", 0);
    } else {
      genReal(e);
      callPrintf("fmt_real_nl", 1);
    }
    return;
  }
  // a record prints its scalar leaves on one line, separated by spaces
  int n;
  Leaf *leaves = leavesOf(e->var.type, &n);
  for (int i = 0; i < n; i++) {
    int last = i == n - 1;
    if (leaves[i].kind == TY_INT) {
      emit("mov rsi, qword %s", varAddr(&e->var, leaves[i].offset));
      callPrintf(last ? "fmt_int_nl" : "fmt_int_sp", 0);
    } else {
      emit("movsd xmm0, qword %s", varAddr(&e->var, leaves[i].offset));
      callPrintf(last ? "fmt_real_nl" : "fmt_real_sp", 1);
    }
  }
  free(leaves);
}

// readchar: getchar() gives a byte 0..255, or -1 (EOF) at the end of input
static void genReadChar(Stmt *s) {
  AstVarRef *v = &s->ioArg->var;
  if (!v->index) {
    emit("call getchar wrt ..plt");
    emit("movsxd rax, eax");
    emit("mov qword %s, rax", varAddr(v, 0));
    return;
  }
  // the element is checked before any input is consumed; its address waits
  // on the stack (with padding that keeps rsp aligned for the call)
  genElementAddress(v);
  emit("lea rax, [rcx + rax*8]");
  emit("push rax");
  emit("sub rsp, 8");
  emit("call getchar wrt ..plt");
  emit("add rsp, 8");
  emit("pop rcx");
  emit("movsxd rax, eax");
  emit("mov qword [rcx], rax");
}

// print: every item is written before the next one is evaluated; strings
// go through %s, so a % in them is printed as it is
static void genPrint(Stmt *s) {
  for (PrintItem *it = s->items; it; it = it->next) {
    if (it->isString) {
      // the bytes as numbers, so no character needs NASM quoting
      int k = stringCount++;
      fprintf(out, "section .data\nS%d: db ", k);
      for (int i = 0; i < it->len; i++)
        fprintf(out, "%d, ", (unsigned char)it->text[i]);
      fprintf(out, "0\nsection .text\n");
      emit("lea rsi, [rel S%d]", k);
      callPrintf("fmt_str", 0);
    } else if (it->expr->type->kind == TY_INT) {
      genInt(it->expr);
      emit("mov rsi, rax");
      callPrintf("fmt_int", 0);
    } else {
      genReal(it->expr);
      callPrintf("fmt_real", 1);
    }
  }
}

static void genCall(Stmt *s) {
  FuncEntry *f = s->callee;
  if (f->paramSize > 0)
    emit("sub rsp, %d", f->paramSize);
  char slot[48];
  for (int i = 0; i < f->numInputs; i++) {
    VarEntry *formal = f->inputs[i], *actual = s->ins.ids[i].entry;
    snprintf(slot, sizeof(slot), "[rsp + %d]", formal->offset);
    blockMove(slot, addr(actual, 0), formal->type->size);
  }
  if (f->paramSize > f->inSize) { // zero the outputs (and the padding)
    emit("lea rdi, [rsp + %d]", f->inSize);
    emit("mov rcx, %d", (f->paramSize - f->inSize) / SCALAR_SIZE);
    emit("xor eax, eax");
    emit("rep stosq");
  }
  emit("call F%s", f->name);
  for (int i = 0; i < f->numOutputs; i++) {
    VarEntry *formal = f->outputs[i], *actual = s->outs.ids[i].entry;
    snprintf(slot, sizeof(slot), "[rsp + %d]", formal->offset);
    blockMove(addr(actual, 0), slot, formal->type->size);
  }
  if (f->paramSize > 0)
    emit("add rsp, %d", f->paramSize);
}

static void genStmt(Stmt *s) {
  static const char *names[] = {"assignment", "while",     "if",
                                "read",       "write",     "call",
                                "readchar",   "writechar", "print",
                                "exit"};
  fprintf(out, "    ; line %d: %s\n", s->line, names[s->kind]);
  switch (s->kind) {
  case STMT_ASSIGN:
    genAssign(s);
    break;
  case STMT_READ:
    genRead(s);
    break;
  case STMT_WRITE:
    genWrite(s);
    break;
  case STMT_CALL:
    genCall(s);
    break;
  case STMT_READCHAR:
    genReadChar(s);
    break;
  case STMT_WRITECHAR: // the low 8 bits, as putchar takes them
    genInt(s->ioArg);
    emit("mov edi, eax");
    emit("call putchar wrt ..plt");
    break;
  case STMT_PRINT:
    genPrint(s);
    break;
  case STMT_EXIT: // exit() flushes stdout; the status is the low 8 bits
    genInt(s->ioArg);
    emit("mov edi, eax");
    emit("call exit wrt ..plt");
    break;
  case STMT_WHILE: {
    int top = newLabel(), body = newLabel(), end = newLabel();
    emitLabel(top);
    genCond(s->cond, body, end);
    emitLabel(body);
    genStmts(s->body);
    emit("jmp L%d", top);
    emitLabel(end);
    break;
  }
  case STMT_IF: {
    int then = newLabel(), other = newLabel(), end = newLabel();
    genCond(s->cond, then, other);
    emitLabel(then);
    genStmts(s->body);
    emit("jmp L%d", end);
    emitLabel(other);
    genStmts(s->elseBody);
    emitLabel(end);
    break;
  }
  }
}

static void genStmts(Stmt *s) {
  for (; s; s = s->next)
    genStmt(s);
}

/* -------------------------------------------------------------- functions */

static void genFunction(FuncEntry *f) {
  cur = f;
  fputc('\n', out);
  if (f->isMain) {
    fprintf(out, "main:\n");
    emit("lea rax, [rsp + 8] ; the stack above main's return address");
    emit("mov [rel rt_stack_top], rax");
  } else {
    // stack used by the callers down to this return address, plus this
    // activation, must stay within MAX_STACK_USE
    fprintf(out, "F%s:\n", f->name);
    emit("mov rax, [rel rt_stack_top]");
    emit("sub rax, rsp");
    emit("cmp rax, %d", MAX_STACK_USE + 8 - f->stackNeed);
    emit("jg rt_stack_overflow");
  }
  emit("push rbp");
  emit("mov rbp, rsp");
  if (f->localSize > 0) {
    emit("sub rsp, %d", f->localSize);
    emit("; zero the locals");
    emit("lea rdi, [rbp - %d]", f->localSize);
    emit("mov rcx, %d", f->localSize / SCALAR_SIZE);
    emit("xor eax, eax");
    emit("rep stosq");
  }

  genStmts(f->ast->stmts);

  // The return list is a parallel assignment to the output slots:
  // return [b2, b3] with b3 itself an output must copy b3's old value, so
  // every value is pushed before any slot is written.
  fprintf(out, "    ; line %d: return\n", f->ast->returnLine);
  IdList *ret = &f->ast->returns;
  int total = 0;
  for (int i = 0; i < ret->count; i++)
    total += f->outputs[i]->type->size;
  if (total > 0) {
    char slot[48];
    emit("sub rsp, %d", total);
    for (int i = 0, off = 0; i < ret->count; i++) {
      snprintf(slot, sizeof(slot), "[rsp + %d]", off);
      blockMove(slot, addr(ret->ids[i].entry, 0), f->outputs[i]->type->size);
      off += f->outputs[i]->type->size;
    }
    for (int i = 0, off = 0; i < ret->count; i++) {
      snprintf(slot, sizeof(slot), "[rsp + %d]", off);
      blockMove(addr(f->outputs[i], 0), slot, f->outputs[i]->type->size);
      off += f->outputs[i]->type->size;
    }
    emit("add rsp, %d", total);
  }
  if (f->isMain)
    emit("xor eax, eax");
  emit("leave");
  emit("ret");
}

// AN_<name>: the name of every array variable, for index error messages;
// one label per distinct name (a local array name may occur in several
// functions)
static void emitArrayName(VarEntry *v, const char ***done, int *n) {
  if (v->type->kind != TY_ARRAY)
    return;
  for (int i = 0; i < *n; i++)
    if (strcmp((*done)[i], v->name) == 0)
      return;
  *done = realloc(*done, sizeof(const char *) * (*n + 1));
  (*done)[(*n)++] = v->name;
  fprintf(out, "AN_%s: db \"%s\", 0\n", v->name, v->name);
}

static void emitArrayNames(SymbolTable *st) {
  const char **done = NULL;
  int n = 0;
  for (VarEntry *g = st->globals; g; g = g->next)
    emitArrayName(g, &done, &n);
  for (int i = 0; i < st->numFuncs; i++)
    for (VarEntry *v = st->funcs[i]->vars; v; v = v->next)
      emitArrayName(v, &done, &n);
  free(done);
}

void generateCode(Program *p, SymbolTable *st, FILE *o) {
  (void)p;
  out = o;
  labelCount = 0;
  stringCount = 0;

  fprintf(out, "; generated by the Group 51 compiler\n");
  fprintf(out, "; build: nasm -f elf64 file.asm -o file.o && "
               "gcc -no-pie file.o -o program\n");
  fprintf(out, "default rel\n");
  fprintf(out, "global main\n");
  fprintf(out, "extern printf, scanf, exit, getchar, putchar\n\n");

  fprintf(out, "section .data\n");
  fprintf(out, "fmt_int_nl:    db \"%%lld\", 10, 0\n");
  fprintf(out, "fmt_real_nl:   db \"%%.2f\", 10, 0\n");
  fprintf(out, "fmt_int_sp:    db \"%%lld \", 0\n");
  fprintf(out, "fmt_real_sp:   db \"%%.2f \", 0\n");
  fprintf(out, "fmt_read_int:  db \" %%lld\", 0\n");
  fprintf(out, "fmt_read_real: db \" %%lf\", 0\n");
  fprintf(out, "msg_div_zero:  db \"Runtime error: division by zero\", 10, "
               "0\n");
  fprintf(out, "fmt_int:       db \"%%lld\", 0\n");
  fprintf(out, "fmt_real:      db \"%%.2f\", 0\n");
  fprintf(out, "fmt_str:       db \"%%s\", 0\n");
  fprintf(out, "msg_index:     db \"Runtime error: index %%lld out of bounds "
               "for array %%s of length %%lld at line %%lld\", 10, 0\n");
  fprintf(out, "msg_stack:     db \"Runtime error: stack overflow (recursion "
               "too deep)\", 10, 0\n");
  emitArrayNames(st);
  fputc('\n', out);

  fprintf(out, "section .bss\n");
  fprintf(out, "    alignb 8\n");
  fprintf(out, "rt_stack_top: resq 1\n");
  for (VarEntry *v = st->globals; v; v = v->next)
    fprintf(out, "G_%s: resb %d ; global %s\n", v->name, v->type->size,
            typeName(v->type));

  fprintf(out, "\nsection .text\n");
  for (int i = 0; i < st->numFuncs; i++)
    genFunction(st->funcs[i]);

  fprintf(out, "\nrt_div_by_zero:\n");
  emit("and rsp, -16 ; realign: we may be in the middle of an expression");
  emit("lea rdi, [rel msg_div_zero]");
  emit("xor eax, eax");
  emit("call printf wrt ..plt");
  emit("mov edi, 1");
  emit("call exit wrt ..plt");

  // rsi = index, rdx = array name, rcx = length, r8 = line (printf's order)
  fprintf(out, "\nrt_index_error:\n");
  emit("and rsp, -16 ; realign: we may be in the middle of an expression");
  emit("lea rdi, [rel msg_index]");
  emit("xor eax, eax");
  emit("call printf wrt ..plt");
  emit("mov edi, 1");
  emit("call exit wrt ..plt");

  fprintf(out, "\nrt_stack_overflow:\n");
  emit("and rsp, -16");
  emit("lea rdi, [rel msg_stack]");
  emit("xor eax, eax");
  emit("call printf wrt ..plt");
  emit("mov edi, 1");
  emit("call exit wrt ..plt");

  fprintf(out, "\nsection .note.GNU-stack noalloc noexec nowrite progbits\n");
  cur = NULL;
}
