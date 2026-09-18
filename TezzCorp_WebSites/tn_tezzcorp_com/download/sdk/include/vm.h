#pragma once
#include "parser.h"

typedef struct VM VM;

typedef enum { V_I64, V_F64, V_PTR } ValKind;

typedef struct {
  ValKind k;
  long long i;
  double f;
  void* p;
  Type* ty;
} Value;

typedef struct {
  const char* name;
  int len;
  Type* ty;
  void* storage; // owned by scope
} Var;

typedef struct {
  Var* vars;
  int n, cap;
} Scope;

typedef struct ModuleGraph {
  Module** mods;
  int mod_n, mod_cap;
  char* root_dir;
  char* module_root;
  int root_inited;
  int has_manifest;
  int has_lock;
  const char** dep_names;
  const char** dep_versions;
  int dep_n, dep_cap;
  const char** lock_names;
  const char** lock_versions;
  const char** lock_urls;
  int lock_n, lock_cap;
} ModuleGraph;

void mg_init(ModuleGraph* G);
void mg_free(ModuleGraph* G);

Module* mg_load(ModuleGraph* G, TypeEnv* tenv, const char* base_dir, const char* import_path);

void vm_set_async_zero(int on);

// builtins (used by IR interpreter)
int builtin_call(const char* name, int nlen, Value* args, int argc, Value* out);
