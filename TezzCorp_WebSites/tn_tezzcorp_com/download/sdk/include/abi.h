#pragma once

#include "types.h"

struct ModuleGraph;
struct Module;

void abi_emit_header(struct ModuleGraph* G, TypeEnv* tenv, struct Module* root, const char* out_path);
void abi_emit_tnx(struct ModuleGraph* G, TypeEnv* tenv, struct Module* root, const char* out_path);
void abi_emit_pyext(struct ModuleGraph* G, TypeEnv* tenv, struct Module* root, const char* out_dir, const char* module_name);
