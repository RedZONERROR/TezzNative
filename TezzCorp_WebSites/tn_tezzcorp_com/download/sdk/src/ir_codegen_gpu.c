// src/ir_codegen_gpu.c
#include "ir.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

void ir_compile_to_gpu_stub(IRModule* m, const char* out_path, const char* api){
  (void)m;
  (void)out_path;
  dief("ir_codegen_gpu: backend '%s' is not implemented; use hlsl/dxil or cuda", api ? api : "gpu");
}
