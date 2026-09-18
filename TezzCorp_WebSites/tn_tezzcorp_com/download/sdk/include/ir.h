// include/ir.h  (IR v4 public)
#pragma once
#include "parser.h"
#include "types.h"

typedef enum {
  I_ICONST,
  I_FCONST,
  I_ADDRSYM, // dst = &symbol
  I_SCONST,   // dst = address of string constant S#
  I_MOV,
  I_BIN,      // dst = b (op) c
  I_CMP,      // dst = b (cmp) c  (i64 0/1)
  I_FBIN,     // dst = b (fop) c
  I_FCMP,     // dst = b (fcmp) c (i64 0/1)
  I_I2F,      // dst = (double) b
  I_F2I,      // dst = (i64) b
  I_CALL,     // dst = call name(args...)
  I_CALLPTR,  // dst = call *b(args...)
  I_ALLOCA,   // dst = stack addr
  I_LOAD,     // dst = [b]
  I_STORE,    // [a] = b
  I_GEP,      // dst = b + imm_off
  I_RET,
  I_LABEL,
  I_JMP,
  I_JZ,       // if a == 0 jump b
} IROp;

typedef enum {
  B_ADD, B_SUB, B_MUL, B_DIV, B_MOD,
  B_AND, B_OR,  B_XOR,
  B_SHL, B_SHR,
} IRBin;

typedef enum {
  F_ADD, F_SUB, F_MUL, F_DIV
} IRFlt;

typedef enum {
  C_EQ, C_NEQ, C_LT, C_LTE, C_GT, C_GTE
} IRCmp;

typedef struct IRIns {
  IROp op;
  int a, b, c;       // regs/labels depending on op
  IRBin binop;
  IRCmp cmpop;

  long long imm;     // ICONST or GEP immediate
  long long immf;    // FCONST bit pattern
  int sid;           // SCONST string id

  const char* name;  // CALL target
  int nlen;
  int name_owned;
  int* args;
  int argc;

  int size;          // LOAD/STORE/ALLOCA size
  int is_unsigned;   // LOAD integer extension mode
  int align;         // ALLOCA alignment
  IRFlt fop;         // FBIN
} IRIns;

typedef struct IRFunc {
  const char* name;
  int len;
  int name_owned;

  IRIns* ins;
  int n, cap;

  int next_reg;
  int max_reg;
  int next_label;
  int param_count;
  int* param_regs;
  int param_regs_n;
  int is_extern;
  int is_kernel;
  int force_inline;

  // local env: name -> addr reg + type
  const char** vname;
  int* vlen;
  int* vaddr;
  Type** vty;
  int vcount, vcap;

  // loop stack for break/continue
  int* brk;
  int* cont;
  int lcount, lcap;

  int scope_depth;
} IRFunc;

typedef struct {
  char* bytes; // includes trailing 0
  int len;     // strlen excluding trailing 0
} IRString;

typedef struct IRGlobal {
  const char* name;
  int len;
  int name_owned;
  Type* ty;
  int size;
  int align;
  int is_extern;
  int is_static;
  int has_init;
  long long init_int;
  long long init_fbits;
} IRGlobal;

typedef struct IRModule {
  IRFunc* fns;
  int fn_n, fn_cap;

  IRString* strs;
  int str_n, str_cap;

  IRGlobal* globals;
  int global_n;
} IRModule;

IRModule* ir_build_from_module(Module* root, TypeEnv* tenv);
void      ir_dump(IRModule* m);
void      ir_set_repro(int on);
void      ir_set_async_zero(int on);
void      ir_set_vectorize(int on);
void      ir_optimize(IRModule* m);
void      ir_free(IRModule* m);
int       ir_main_const(IRModule* m, long long* out_ret);

struct ModuleGraph; // forward declare tag (defined in vm.h)

IRModule* ir_build_from_graph(struct ModuleGraph* G, TypeEnv* tenv, Module* root);
