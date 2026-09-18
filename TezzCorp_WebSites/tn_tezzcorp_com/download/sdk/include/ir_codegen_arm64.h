// include/ir_codegen_arm64.h
#pragma once
#include "ir.h"
#include "ir_codegen_pe.h"
#include <stdint.h>

// Minimal ARM64 native emitters (no external compiler).
// Returns 0 on success.
int ir_compile_to_elf_arm64_exe(IRModule* m, const char* out_exe);
int ir_compile_to_macho_arm64_exe(IRModule* m, const char* out_exe);
int ir_arm64_codegen_text(IRModule* m, unsigned char** out_text, uint32_t* out_size, size_t* out_main_off);
int ir_arm64_codegen_text_win(IRModule* m, uint64_t image_base, const PeImportInfo* imp,
                              const uint32_t* str_rva, int str_n,
                              GlobalInfo* ginfo, int gcount,
                              unsigned char** out_text, uint32_t* out_size, size_t* out_main_off);
