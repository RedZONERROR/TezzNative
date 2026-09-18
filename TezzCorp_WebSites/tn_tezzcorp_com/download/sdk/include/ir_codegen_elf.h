// include/ir_codegen_elf.h
#pragma once
#include "ir.h"

// Minimal ELF emitter (Linux). Returns 0 on success.
int ir_compile_to_elf_exe(IRModule* m, const char* out_exe);
int ir_verify_elf_exe(const char* path);
