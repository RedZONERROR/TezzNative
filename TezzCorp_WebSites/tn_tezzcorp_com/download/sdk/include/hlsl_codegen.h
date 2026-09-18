#pragma once
#include "vm.h"
#include "types.h"

// Emit HLSL compute shaders for kernel functions in the module graph.
// The HLSL backend targets a restricted kernel subset (no calls other than gpu_tid).
void hlsl_emit_kernels(ModuleGraph* G, TypeEnv* tenv, Module* entry, const char* out_path);
