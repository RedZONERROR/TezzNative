// include/sema.h
#pragma once
#include "vm.h"     // ModuleGraph, Module
#include "types.h"  // TypeEnv

// Semantic analysis (v2):
// - name resolution for functions/modules (same as before)
// - builds a typed local env for each fn (params + lets + block scopes)
// - fills Expr->ty everywhere (required by IR lowering)
// - validates:
//    * struct field access: a.b
//    * indexing: a[i] for arrays/pointers
//    * & and * typing
//    * assignment compatibility (basic)
// - still NOT a full optimizer/advanced type system; it’s “enough to make IR correct”.

void sema_check_graph(ModuleGraph* G, TypeEnv* tenv, Module* entry_mod, int require_main);
void sema_check_module(ModuleGraph* G, TypeEnv* tenv, Module* M);

// Configuration
void sema_set_freestanding(int on);
void sema_set_async_zero(int on);
void sema_set_target(const char* target);
