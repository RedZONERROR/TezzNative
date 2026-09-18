// include/ir_codegen_macho.h
#pragma once
#include "ir.h"

// Minimal Mach-O emitter (macOS). Returns 0 on success.
int ir_compile_to_macho_exe(IRModule* m, const char* out_exe);
int ir_verify_macho_exe(const char* path);
