// include/ir_codegen_gas.h
#pragma once
#include "ir.h"

void ir_compile_to_gas(IRModule* m, const char* out_s);
void ir_compile_to_gas_target(IRModule* m, const char* out_s, const char* target);
void ir_compile_to_c(IRModule* m, const char* out_c);
void ir_compile_to_cuda(IRModule* m, const char* out_cu);
void ir_compile_to_gpu_stub(IRModule* m, const char* out_path, const char* api);
