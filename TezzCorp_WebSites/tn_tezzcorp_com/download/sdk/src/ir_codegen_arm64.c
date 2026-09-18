// src/ir_codegen_arm64.c
#include "ir_codegen_arm64.h"
#include "ir_codegen_pe.h"
#include "util.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static void write_u16(FILE* f, uint16_t v){ fwrite(&v, 1, 2, f); }
static void write_u32(FILE* f, uint32_t v){ fwrite(&v, 1, 4, f); }
static void write_u64(FILE* f, uint64_t v){ fwrite(&v, 1, 8, f); }

static void put_u32_le(unsigned char* p, uint32_t v){
  p[0] = (unsigned char)(v & 0xFF);
  p[1] = (unsigned char)((v >> 8) & 0xFF);
  p[2] = (unsigned char)((v >> 16) & 0xFF);
  p[3] = (unsigned char)((v >> 24) & 0xFF);
}
static void put_u64_le(unsigned char* p, uint64_t v){
  p[0] = (unsigned char)(v & 0xFF);
  p[1] = (unsigned char)((v >> 8) & 0xFF);
  p[2] = (unsigned char)((v >> 16) & 0xFF);
  p[3] = (unsigned char)((v >> 24) & 0xFF);
  p[4] = (unsigned char)((v >> 32) & 0xFF);
  p[5] = (unsigned char)((v >> 40) & 0xFF);
  p[6] = (unsigned char)((v >> 48) & 0xFF);
  p[7] = (unsigned char)((v >> 56) & 0xFF);
}

static uint32_t align_up_u32(uint32_t v, uint32_t a){
  if(a == 0) return v;
  uint32_t m = v % a;
  if(m == 0) return v;
  return v + (a - m);
}

typedef struct {
  unsigned char* data;
  size_t len;
  size_t cap;
} CodeBuf;

typedef struct {
  size_t pos;
  int func_idx;
} CallPatch;

typedef struct {
  const char* name;
  int len;
  int is_extern;
  size_t off;
  int has_off;
} FuncInfo;

static void cb_init(CodeBuf* cb, size_t cap){
  cb->data = (unsigned char*)malloc(cap);
  cb->len = 0;
  cb->cap = cap;
}
static void cb_free(CodeBuf* cb){
  free(cb->data);
  cb->data = NULL;
  cb->len = cb->cap = 0;
}
static void cb_emit4(CodeBuf* cb, uint32_t v){
  if(cb->len + 4 > cb->cap){
    cb->cap *= 2;
    cb->data = (unsigned char*)realloc(cb->data, cb->cap);
  }
  put_u32_le(cb->data + cb->len, v);
  cb->len += 4;
}

static uint32_t reg_slot_off(int reg);
static int lower_func_arm64(IRFunc* f, CodeBuf* cb, FuncInfo* finfo, int fcount,
                            size_t* out_off, size_t* out_entry,
                            CallPatch* cpatch, int* cpatch_n, int cpatch_cap,
                            const uint32_t* str_rva, int str_n,
                            GlobalInfo* ginfo, int gcount,
                            uint64_t image_base, uint32_t text_rva);

static uint32_t arm64_add_imm(int rd, int rn, uint32_t imm12){
  return 0x91000000u | ((imm12 & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_sub_imm(int rd, int rn, uint32_t imm12){
  return 0xD1000000u | ((imm12 & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_movz_x(int rd, uint16_t imm, int shift){
  uint32_t hw = ((uint32_t)shift / 16u) << 21;
  return 0xD2800000u | hw | ((uint32_t)imm << 5) | (uint32_t)rd;
}
static uint32_t arm64_movk_x(int rd, uint16_t imm, int shift){
  uint32_t hw = ((uint32_t)shift / 16u) << 21;
  return 0xF2800000u | hw | ((uint32_t)imm << 5) | (uint32_t)rd;
}
static uint32_t arm64_add_reg(int rd, int rn, int rm){
  return 0x8B000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_sub_reg(int rd, int rn, int rm){
  return 0xCB000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_mul_reg(int rd, int rn, int rm){
  return 0x9B007C00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_udiv_reg(int rd, int rn, int rm){
  return 0x9AC00800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_and_reg(int rd, int rn, int rm){
  return 0x8A000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_orr_reg(int rd, int rn, int rm){
  return 0xAA000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_eor_reg(int rd, int rn, int rm){
  return 0xCA000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_sdiv_reg(int rd, int rn, int rm){
  return 0x9AC00C00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_lslv_reg(int rd, int rn, int rm){
  return 0x9AC02000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_asrv_reg(int rd, int rn, int rm){
  return 0x9AC02800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_str_imm(int rt, int rn, uint32_t off){
  return 0xF9000000u | ((off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_ldr_imm(int rt, int rn, uint32_t off){
  return 0xF9400000u | ((off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_strw_imm(int rt, int rn, uint32_t off){
  return 0xB9000000u | ((off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_ldrw_imm(int rt, int rn, uint32_t off){
  return 0xB9400000u | ((off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_strh_imm(int rt, int rn, uint32_t off){
  return 0x79000000u | ((off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_ldrh_imm(int rt, int rn, uint32_t off){
  return 0x79400000u | ((off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_strb_imm(int rt, int rn, uint32_t off){
  return 0x39000000u | ((off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_ldrb_imm(int rt, int rn, uint32_t off){
  return 0x39400000u | ((off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_ldrsb_x_imm(int rt, int rn, uint32_t off){
  return 0x39C00000u | ((off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_ldrsh_x_imm(int rt, int rn, uint32_t off){
  return 0x79C00000u | ((off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_ldrsw_imm(int rt, int rn, uint32_t off){
  return 0xB9800000u | ((off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_strf_imm(int rt, int rn, uint32_t off){
  return 0xFD000000u | ((off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_ldrf_imm(int rt, int rn, uint32_t off){
  return 0xFD400000u | ((off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_str_reg(int rt, int rn){
  return 0xF9000000u | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_ldr_reg(int rt, int rn){
  return 0xF9400000u | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t arm64_cmp_reg(int rn, int rm){
  return 0xEB00001Fu | ((uint32_t)rm << 16) | ((uint32_t)rn << 5);
}
static uint32_t arm64_cmp_imm0(int rn){
  return 0xF100001Fu | ((uint32_t)rn << 5);
}
static uint32_t arm64_cset(int rd, int cond){
  // cset is alias of csinc with inverted condition
  uint32_t inv = ((uint32_t)cond ^ 1u) & 0xFu;
  return 0x9A9F07E0u | (inv << 12) | (uint32_t)rd;
}
static uint32_t arm64_b_imm(int imm26){
  return 0x14000000u | ((uint32_t)imm26 & 0x03FFFFFFu);
}
static uint32_t arm64_bl_imm(int imm26){
  return 0x94000000u | ((uint32_t)imm26 & 0x03FFFFFFu);
}
static uint32_t arm64_bcond(int imm19, int cond){
  return 0x54000000u | ((uint32_t)(imm19 & 0x7FFFF) << 5) | (uint32_t)(cond & 0xF);
}
static uint32_t arm64_ret(void){ return 0xD65F03C0u; }
static uint32_t arm64_svc_0(void){ return 0xD4000001u; }
static uint32_t arm64_svc_imm(uint16_t imm){
  return 0xD4000001u | ((uint32_t)imm << 5);
}
static uint32_t arm64_ldr_lit(int rt, int imm19){
  return 0x58000000u | ((uint32_t)(imm19 & 0x7FFFF) << 5) | (uint32_t)rt;
}
static uint32_t arm64_blr(int rn){
  return 0xD63F0000u | ((uint32_t)rn << 5);
}
static uint32_t arm64_fmov_dx(int rd, int rn){
  return 0x9E670000u | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_fcvtzs_xd(int rd, int rn){
  return 0x9E780000u | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_scvtf_dx(int rd, int rn){
  return 0x9E620000u | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_fsub_dd(int rd, int rn, int rm){
  return 0x1E603800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_fmul_dd(int rd, int rn, int rm){
  return 0x1E600800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_fadd_dd(int rd, int rn, int rm){
  return 0x1E602800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_fdiv_dd(int rd, int rn, int rm){
  return 0x1E601800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t arm64_fcmp_dd(int rn, int rm){
  return 0x1E602000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5);
}

static void emit_call_iat(CodeBuf* cb, uint64_t iat_addr){
  // ldr x16, [pc, #imm19]; ldr x16, [x16]; blr x16; .quad iat_addr
  cb_emit4(cb, arm64_ldr_lit(16, 3));
  cb_emit4(cb, arm64_ldr_reg(16, 16));
  cb_emit4(cb, arm64_blr(16));
  if(cb->len + 8 > cb->cap){
    cb->cap *= 2;
    cb->data = (unsigned char*)realloc(cb->data, cb->cap);
  }
  put_u64_le(cb->data + cb->len, iat_addr);
  cb->len += 8;
}

static void emit_mov_imm64(CodeBuf* cb, int rd, uint64_t imm){
  uint16_t p0 = (uint16_t)(imm & 0xFFFFu);
  uint16_t p1 = (uint16_t)((imm >> 16) & 0xFFFFu);
  uint16_t p2 = (uint16_t)((imm >> 32) & 0xFFFFu);
  uint16_t p3 = (uint16_t)((imm >> 48) & 0xFFFFu);
  cb_emit4(cb, arm64_movz_x(rd, p0, 0));
  if(p1) cb_emit4(cb, arm64_movk_x(rd, p1, 16));
  if(p2) cb_emit4(cb, arm64_movk_x(rd, p2, 32));
  if(p3) cb_emit4(cb, arm64_movk_x(rd, p3, 48));
}

static void emit_ld_slot(CodeBuf* cb, int rt, int reg){
  uint32_t off = reg_slot_off(reg);
  if((off / 8u) <= 0xFFFu){
    cb_emit4(cb, arm64_ldr_imm(rt, 31, off/8u));
    return;
  }
  emit_mov_imm64(cb, 9, off);
  cb_emit4(cb, arm64_add_reg(9, 31, 9));
  cb_emit4(cb, arm64_ldr_reg(rt, 9));
}

static void emit_st_slot(CodeBuf* cb, int rt, int reg){
  uint32_t off = reg_slot_off(reg);
  if((off / 8u) <= 0xFFFu){
    cb_emit4(cb, arm64_str_imm(rt, 31, off/8u));
    return;
  }
  emit_mov_imm64(cb, 9, off);
  cb_emit4(cb, arm64_add_reg(9, 31, 9));
  cb_emit4(cb, arm64_str_reg(rt, 9));
}

static void emit_ld_slot_fp(CodeBuf* cb, int ft, int reg){
  uint32_t off = reg_slot_off(reg);
  if((off / 8u) <= 0xFFFu){
    cb_emit4(cb, arm64_ldrf_imm(ft, 31, off/8u));
    return;
  }
  emit_mov_imm64(cb, 9, off);
  cb_emit4(cb, arm64_add_reg(9, 31, 9));
  cb_emit4(cb, arm64_ldrf_imm(ft, 9, 0));
}

static void emit_st_slot_fp(CodeBuf* cb, int ft, int reg){
  uint32_t off = reg_slot_off(reg);
  if((off / 8u) <= 0xFFFu){
    cb_emit4(cb, arm64_strf_imm(ft, 31, off/8u));
    return;
  }
  emit_mov_imm64(cb, 9, off);
  cb_emit4(cb, arm64_add_reg(9, 31, 9));
  cb_emit4(cb, arm64_strf_imm(ft, 9, 0));
}

static void arm64_push_lr(CodeBuf* cb){
  cb_emit4(cb, arm64_sub_imm(31, 31, 16));
  cb_emit4(cb, arm64_str_reg(30, 31));
}

static void arm64_pop_lr(CodeBuf* cb){
  cb_emit4(cb, arm64_ldr_reg(30, 31));
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
}

typedef struct {
  size_t off_exit;
  size_t off_getstd;
  size_t off_write;
  size_t off_gettime;
  size_t off_valloc;
  size_t off_vfree;
  size_t off_say_inline;
  size_t off_say_str_inline;
  size_t off_say_f_inline;
  size_t off_say;
  size_t off_say_str;
  size_t off_say_f;
  size_t off_say_multi;
  size_t off_len;
  size_t off_strcmp;
  size_t off_tnstrcmp;
  size_t off_memcpy;
  size_t off_memmove;
  size_t off_memcmp;
  size_t off_time_ns;
  size_t off_time_ms;
  size_t off_time;
  size_t off_malloc;
  size_t off_free;
  size_t off_read_line;
  size_t off_write_line;
  size_t off_get_cwd;
  size_t off_fopen;
  size_t off_fclose;
  size_t off_fread;
  size_t off_fwrite;
  size_t off_fseek;
  size_t off_ftell;
  size_t off_fflush;
  size_t off_io_err;
  size_t off_io_eof;
  size_t off_read_bytes;
  size_t off_stdin;
  size_t off_stdout;
  size_t off_stderr;
} Arm64StubInfo;

static void build_linux_arm64_stubs(CodeBuf* cb, Arm64StubInfo* out,
                                    uint64_t heap_cur_addr, uint64_t heap_end_addr,
                                    uint64_t err_addr, uint64_t stdin_addr,
                                    uint64_t stdout_addr, uint64_t stderr_addr){
  if(!cb || !out) return;
  // tn_exit(code)
  out->off_exit = cb->len;
  emit_mov_imm64(cb, 8, 93); // sys_exit
  cb_emit4(cb, arm64_svc_0());
  cb_emit4(cb, arm64_ret());

  // tn_getstd(handle) -> fd (stdout/stderr)
  out->off_getstd = cb->len;
  // use x9 as temp to avoid clobbering x1 (callers pass buffers in x1)
  emit_mov_imm64(cb, 9, (uint64_t)(int64_t)-12); // STDERR
  cb_emit4(cb, arm64_cmp_reg(0, 9));
  cb_emit4(cb, arm64_cset(9, 0)); // eq
  cb_emit4(cb, arm64_add_imm(0, 9, 1)); // 1 or 2
  cb_emit4(cb, arm64_ret());

  // tn_write(handle, buf, len, written*, overlapped) -> 1
  out->off_write = cb->len;
  cb_emit4(cb, arm64_add_imm(9, 3, 0)); // x9 = written*
  emit_mov_imm64(cb, 8, 64); // sys_write
  emit_mov_imm64(cb, 0, 1);  // fd = 1 (stdout)
  cb_emit4(cb, arm64_svc_0());
  cb_emit4(cb, arm64_cmp_imm0(9));
  // if x9 == 0, skip store
  size_t jpos = cb->len;
  cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_str_reg(0, 9)); // *written = rax
  // patch branch
  {
    size_t target = cb->len;
    int64_t disp = ((int64_t)target - (int64_t)jpos) / 4;
    uint32_t ins = arm64_bcond((int)disp, 0);
    put_u32_le(cb->data + jpos, ins);
  }
  emit_mov_imm64(cb, 0, 1);
  cb_emit4(cb, arm64_ret());

  // tn_gettime(&out) -> clock_gettime(CLOCK_MONOTONIC)
  out->off_gettime = cb->len;
  cb_emit4(cb, arm64_add_imm(9, 0, 0)); // x9 = out*
  cb_emit4(cb, arm64_sub_imm(31, 31, 32));
  emit_mov_imm64(cb, 0, 1);   // CLOCK_MONOTONIC
  cb_emit4(cb, arm64_add_imm(1, 31, 0)); // &timespec
  emit_mov_imm64(cb, 8, 113); // sys_clock_gettime
  cb_emit4(cb, arm64_svc_0());
  cb_emit4(cb, arm64_ldr_imm(2, 31, 0)); // sec
  cb_emit4(cb, arm64_ldr_imm(3, 31, 1)); // nsec
  emit_mov_imm64(cb, 4, 1000000000ULL);
  cb_emit4(cb, arm64_mul_reg(2, 2, 4));
  cb_emit4(cb, arm64_add_reg(2, 2, 3));
  cb_emit4(cb, arm64_str_reg(2, 9));
  cb_emit4(cb, arm64_add_imm(31, 31, 32));
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // say_str_inline(str) -> i64 (no newline)
  out->off_say_str_inline = cb->len;
  arm64_push_lr(cb);
  cb_emit4(cb, arm64_add_imm(10, 0, 0)); // x10 = s
  // compute len in x2
  cb_emit4(cb, arm64_add_imm(1, 0, 0)); // x1 = s
  emit_mov_imm64(cb, 2, 0); // len = 0
  size_t len2_loop = cb->len;
  cb_emit4(cb, arm64_ldrb_imm(3, 1, 0)); // w3 = *p
  cb_emit4(cb, arm64_cmp_imm0(3));
  size_t j_len_done = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // eq
  cb_emit4(cb, arm64_add_imm(1, 1, 1));
  cb_emit4(cb, arm64_add_imm(2, 2, 1));
  int64_t disp = ((int64_t)len2_loop - (int64_t)cb->len) / 4;
  cb_emit4(cb, arm64_b_imm((int)disp));
  // patch eq
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)j_len_done) / 4;
    put_u32_le(cb->data + j_len_done, arm64_bcond((int)d, 0));
  }
  // get stdout handle
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd = cb->len; cb_emit4(cb, arm64_bl_imm(0)); // patched to tn_getstd
  // call write(fd, buf, len)
  cb_emit4(cb, arm64_add_imm(1, 10, 0)); // x1 = s
  cb_emit4(cb, arm64_add_imm(2, 2, 0)); // x2 = len
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write = cb->len; cb_emit4(cb, arm64_bl_imm(0)); // patched to tn_write
  arm64_pop_lr(cb);
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // say_inline(i64) -> i64 (no newline)
  out->off_say_inline = cb->len;
  arm64_push_lr(cb);
  // stack buffer 64 bytes
  cb_emit4(cb, arm64_sub_imm(31, 31, 64));
  emit_mov_imm64(cb, 5, 0); // sign flag
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_nonneg = cb->len; cb_emit4(cb, arm64_bcond(10, 0)); // GE
  // negate
  cb_emit4(cb, arm64_sub_reg(0, 31, 0)); // x0 = -x0
  emit_mov_imm64(cb, 5, 1);
  // nonneg label
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)j_nonneg) / 4;
    put_u32_le(cb->data + j_nonneg, arm64_bcond((int)d, 10));
  }
  // x1 = sp + 63 (end)
  cb_emit4(cb, arm64_add_imm(1, 31, 63));
  emit_mov_imm64(cb, 2, 0); // len
  // if x0 == 0, store '0'
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_nonzero = cb->len; cb_emit4(cb, arm64_bcond(1, 0)); // NE
  emit_mov_imm64(cb, 3, '0');
  cb_emit4(cb, arm64_strb_imm(3, 1, 0));
  cb_emit4(cb, arm64_sub_imm(1, 1, 1));
  cb_emit4(cb, arm64_add_imm(2, 2, 1));
  size_t j_after_digits = cb->len; cb_emit4(cb, arm64_b_imm(0));
  // nonzero loop
  size_t loop_digits = cb->len;
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)j_nonzero) / 4;
    put_u32_le(cb->data + j_nonzero, arm64_bcond((int)d, 1));
  }
  emit_mov_imm64(cb, 4, 10);
  cb_emit4(cb, arm64_udiv_reg(3, 0, 4)); // q
  cb_emit4(cb, arm64_mul_reg(6, 3, 4)); // q*10
  cb_emit4(cb, arm64_sub_reg(6, 0, 6)); // rem
  emit_mov_imm64(cb, 7, '0');
  cb_emit4(cb, arm64_add_reg(6, 6, 7));
  cb_emit4(cb, arm64_strb_imm(6, 1, 0));
  cb_emit4(cb, arm64_sub_imm(1, 1, 1));
  cb_emit4(cb, arm64_add_imm(2, 2, 1));
  cb_emit4(cb, arm64_add_imm(0, 3, 0)); // x0 = q
  cb_emit4(cb, arm64_cmp_imm0(0));
  int64_t dloop1 = ((int64_t)loop_digits - (int64_t)cb->len) / 4;
  cb_emit4(cb, arm64_bcond((int)dloop1, 1)); // NE
  // after digits
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)j_after_digits) / 4;
    put_u32_le(cb->data + j_after_digits, arm64_b_imm((int)d));
  }
  // sign?
  cb_emit4(cb, arm64_cmp_imm0(5));
  size_t j_nosign = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  emit_mov_imm64(cb, 3, '-');
  cb_emit4(cb, arm64_strb_imm(3, 1, 0));
  cb_emit4(cb, arm64_sub_imm(1, 1, 1));
  cb_emit4(cb, arm64_add_imm(2, 2, 1));
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)j_nosign) / 4;
    put_u32_le(cb->data + j_nosign, arm64_bcond((int)d, 0));
  }
  // x1 = start ptr (x1 + 1)
  cb_emit4(cb, arm64_add_imm(1, 1, 1));
  // get stdout
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd2 = cb->len; cb_emit4(cb, arm64_bl_imm(0)); // tn_getstd
  // write
  cb_emit4(cb, arm64_add_imm(1, 1, 0));
  cb_emit4(cb, arm64_add_imm(2, 2, 0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write2 = cb->len; cb_emit4(cb, arm64_bl_imm(0)); // tn_write
  cb_emit4(cb, arm64_add_imm(31, 31, 64));
  arm64_pop_lr(cb);
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // say_f_inline: fixed 6-digit formatter (no libc)
  out->off_say_f_inline = cb->len;
  arm64_push_lr(cb);
  cb_emit4(cb, arm64_sub_imm(31, 31, 96)); // stack temp
  // if sign, write '-' and clear sign bit
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t jf_nonneg = cb->len; cb_emit4(cb, arm64_bcond(10, 0)); // GE
  emit_mov_imm64(cb, 1, '-');
  cb_emit4(cb, arm64_strb_imm(1, 31, 80));
  cb_emit4(cb, arm64_add_imm(1, 31, 80));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd3 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write3 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 9, 0x7FFFFFFFFFFFFFFFULL);
  cb_emit4(cb, arm64_and_reg(0, 0, 9));
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)jf_nonneg) / 4;
    put_u32_le(cb->data + jf_nonneg, arm64_bcond((int)d, 10));
  }
  // d0 = bits(x0)
  cb_emit4(cb, arm64_fmov_dx(0, 0));
  // x1 = int(d0)
  cb_emit4(cb, arm64_fcvtzs_xd(1, 0));
  // d1 = float(x1)
  cb_emit4(cb, arm64_scvtf_dx(1, 1));
  // d0 = d0 - d1
  cb_emit4(cb, arm64_fsub_dd(0, 0, 1));
  // d1 = 1e6
  emit_mov_imm64(cb, 2, 0x412E848000000000ULL);
  cb_emit4(cb, arm64_fmov_dx(1, 2));
  // d0 *= 1e6
  cb_emit4(cb, arm64_fmul_dd(0, 0, 1));
  // x2 = frac
  cb_emit4(cb, arm64_fcvtzs_xd(2, 0));
  // preserve frac across say_inline and write('.') calls
  cb_emit4(cb, arm64_str_imm(2, 31, 8)); // [sp+64]
  // print int part
  cb_emit4(cb, arm64_add_imm(0, 1, 0));
  size_t call_sayf_int = cb->len; cb_emit4(cb, arm64_bl_imm(0)); // say_inline
  // write '.'
  emit_mov_imm64(cb, 1, '.');
  cb_emit4(cb, arm64_strb_imm(1, 31, 80));
  cb_emit4(cb, arm64_add_imm(1, 31, 80));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd_fdot = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write_fdot = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  // format fraction (6 digits) into [sp..sp+5]
  cb_emit4(cb, arm64_ldr_imm(2, 31, 8)); // reload frac
  cb_emit4(cb, arm64_add_imm(1, 31, 5)); // ptr = sp+5
  emit_mov_imm64(cb, 4, 6); // count
  size_t frac_loop = cb->len;
  emit_mov_imm64(cb, 5, 10);
  cb_emit4(cb, arm64_udiv_reg(6, 2, 5)); // q
  cb_emit4(cb, arm64_mul_reg(7, 6, 5));
  cb_emit4(cb, arm64_sub_reg(7, 2, 7)); // rem
  emit_mov_imm64(cb, 8, '0');
  cb_emit4(cb, arm64_add_reg(7, 7, 8));
  cb_emit4(cb, arm64_strb_imm(7, 1, 0));
  cb_emit4(cb, arm64_sub_imm(1, 1, 1));
  cb_emit4(cb, arm64_add_imm(2, 6, 0));
  cb_emit4(cb, arm64_sub_imm(4, 4, 1));
  cb_emit4(cb, arm64_cmp_imm0(4));
  int64_t dloop2 = ((int64_t)frac_loop - (int64_t)cb->len) / 4;
  cb_emit4(cb, arm64_bcond((int)dloop2, 1)); // NE
  // write fraction
  cb_emit4(cb, arm64_add_imm(1, 31, 0));
  emit_mov_imm64(cb, 2, 6);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd_ffrac = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write_ffrac = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(31, 31, 96));
  arm64_pop_lr(cb);
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // say_str (newline)
  size_t say_str = cb->len;
  out->off_say_str = say_str;
  arm64_push_lr(cb);
  size_t call_saystr_inline = cb->len; cb_emit4(cb, arm64_bl_imm(0)); // patched to say_str_inline
  // write newline
  cb_emit4(cb, arm64_sub_imm(31, 31, 16));
  emit_mov_imm64(cb, 1, 10);
  cb_emit4(cb, arm64_strb_imm(1, 31, 0));
  cb_emit4(cb, arm64_add_imm(1, 31, 0));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd4 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write4 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
  arm64_pop_lr(cb);
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // say (newline)
  size_t say = cb->len;
  out->off_say = say;
  arm64_push_lr(cb);
  size_t call_say_inline = cb->len; cb_emit4(cb, arm64_bl_imm(0)); // patched to say_inline
  // newline
  cb_emit4(cb, arm64_sub_imm(31, 31, 16));
  emit_mov_imm64(cb, 1, 10);
  cb_emit4(cb, arm64_strb_imm(1, 31, 0));
  cb_emit4(cb, arm64_add_imm(1, 31, 0));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd5 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write5 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
  arm64_pop_lr(cb);
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // say_f (newline)
  size_t say_f = cb->len;
  out->off_say_f = say_f;
  arm64_push_lr(cb);
  size_t call_sayf_inline = cb->len; cb_emit4(cb, arm64_bl_imm(0)); // patched
  cb_emit4(cb, arm64_sub_imm(31, 31, 16));
  emit_mov_imm64(cb, 1, 10);
  cb_emit4(cb, arm64_strb_imm(1, 31, 0));
  cb_emit4(cb, arm64_add_imm(1, 31, 0));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd6 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write6 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
  arm64_pop_lr(cb);
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // say_multi(vals:*i64, tags:*i64, argc:i64)
  out->off_say_multi = cb->len;
  arm64_push_lr(cb);
  // save callee-saved regs we use (x19-x22)
  cb_emit4(cb, arm64_sub_imm(31, 31, 32));
  cb_emit4(cb, arm64_str_imm(19, 31, 0));
  cb_emit4(cb, arm64_str_imm(20, 31, 1));
  cb_emit4(cb, arm64_str_imm(21, 31, 2));
  cb_emit4(cb, arm64_str_imm(22, 31, 3));
  // x0=vals, x1=tags, x2=argc
  cb_emit4(cb, arm64_add_imm(19, 0, 0)); // vals*
  cb_emit4(cb, arm64_add_imm(20, 1, 0)); // tags*
  cb_emit4(cb, arm64_add_imm(21, 2, 0)); // argc
  emit_mov_imm64(cb, 22, 0); // i
  size_t sm_loop = cb->len;
  cb_emit4(cb, arm64_cmp_reg(22, 21));
  size_t sm_jge = cb->len; cb_emit4(cb, arm64_bcond(10, 0)); // GE -> done
  // if i>0 write space (inline)
  cb_emit4(cb, arm64_cmp_imm0(22));
  size_t sm_je_nospace = cb->len; cb_emit4(cb, arm64_bcond(0, 0));
  // write space
  cb_emit4(cb, arm64_sub_imm(31, 31, 16));
  emit_mov_imm64(cb, 4, ' ');
  cb_emit4(cb, arm64_strb_imm(4, 31, 0));
  cb_emit4(cb, arm64_add_imm(1, 31, 0));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd7 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write7 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
  size_t sm_nospace = cb->len;
  {
    int64_t d = ((int64_t)sm_nospace - (int64_t)sm_je_nospace) / 4;
    put_u32_le(cb->data + sm_je_nospace, arm64_bcond((int)d, 0));
  }
  // tags/vals are i64 arrays; offset = i*8
  emit_mov_imm64(cb, 6, 8);
  cb_emit4(cb, arm64_mul_reg(6, 22, 6)); // idx8
  cb_emit4(cb, arm64_add_reg(4, 20, 6)); // tag ptr
  cb_emit4(cb, arm64_add_reg(5, 19, 6)); // val ptr
  cb_emit4(cb, arm64_ldr_reg(7, 4)); // tag
  cb_emit4(cb, arm64_ldr_reg(8, 5)); // val
  // if tag == 2 -> say_str_inline
  emit_mov_imm64(cb, 9, 2);
  cb_emit4(cb, arm64_cmp_reg(7, 9));
  size_t sm_je_str = cb->len; cb_emit4(cb, arm64_bcond(0, 0));
  // if tag == 1 -> say_f_inline
  emit_mov_imm64(cb, 9, 1);
  cb_emit4(cb, arm64_cmp_reg(7, 9));
  size_t sm_je_flt = cb->len; cb_emit4(cb, arm64_bcond(0, 0));
  // else say_inline (int)
  cb_emit4(cb, arm64_add_imm(0, 8, 0));
  size_t call_say_inline2 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  size_t sm_jmp_next = cb->len; cb_emit4(cb, arm64_b_imm(0));
  size_t sm_str = cb->len;
  cb_emit4(cb, arm64_add_imm(0, 8, 0));
  size_t call_saystr_inline2 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  size_t sm_jmp_next2 = cb->len; cb_emit4(cb, arm64_b_imm(0));
  size_t sm_flt = cb->len;
  cb_emit4(cb, arm64_add_imm(0, 8, 0));
  size_t call_sayf_inline2 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  size_t sm_next = cb->len;
  // i++
  cb_emit4(cb, arm64_add_imm(22, 22, 1));
  int64_t dloop3 = ((int64_t)sm_loop - (int64_t)cb->len) / 4;
  cb_emit4(cb, arm64_b_imm((int)dloop3));
  size_t sm_done = cb->len;
  // newline at end
  cb_emit4(cb, arm64_sub_imm(31, 31, 16));
  emit_mov_imm64(cb, 4, 10);
  cb_emit4(cb, arm64_strb_imm(4, 31, 0));
  cb_emit4(cb, arm64_add_imm(1, 31, 0));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd8 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write8 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
  // restore callee-saved regs
  cb_emit4(cb, arm64_ldr_imm(19, 31, 0));
  cb_emit4(cb, arm64_ldr_imm(20, 31, 1));
  cb_emit4(cb, arm64_ldr_imm(21, 31, 2));
  cb_emit4(cb, arm64_ldr_imm(22, 31, 3));
  cb_emit4(cb, arm64_add_imm(31, 31, 32));
  arm64_pop_lr(cb);
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // patch branches/calls
  {
    // say_str_inline calls
    int64_t d1 = ((int64_t)out->off_getstd - (int64_t)call_getstd) / 4;
    put_u32_le(cb->data + call_getstd, arm64_bl_imm((int)d1));
    int64_t d2 = ((int64_t)out->off_write - (int64_t)call_write) / 4;
    put_u32_le(cb->data + call_write, arm64_bl_imm((int)d2));
    // say_inline calls
    int64_t d3 = ((int64_t)out->off_getstd - (int64_t)call_getstd2) / 4;
    put_u32_le(cb->data + call_getstd2, arm64_bl_imm((int)d3));
    int64_t d4 = ((int64_t)out->off_write - (int64_t)call_write2) / 4;
    put_u32_le(cb->data + call_write2, arm64_bl_imm((int)d4));
    // say_f_inline calls
    int64_t d5 = ((int64_t)out->off_getstd - (int64_t)call_getstd3) / 4;
    put_u32_le(cb->data + call_getstd3, arm64_bl_imm((int)d5));
    int64_t d6 = ((int64_t)out->off_write - (int64_t)call_write3) / 4;
    put_u32_le(cb->data + call_write3, arm64_bl_imm((int)d6));
    int64_t d6b = ((int64_t)out->off_say_inline - (int64_t)call_sayf_int) / 4;
    put_u32_le(cb->data + call_sayf_int, arm64_bl_imm((int)d6b));
    int64_t d6c = ((int64_t)out->off_getstd - (int64_t)call_getstd_fdot) / 4;
    put_u32_le(cb->data + call_getstd_fdot, arm64_bl_imm((int)d6c));
    int64_t d6d = ((int64_t)out->off_write - (int64_t)call_write_fdot) / 4;
    put_u32_le(cb->data + call_write_fdot, arm64_bl_imm((int)d6d));
    int64_t d6e = ((int64_t)out->off_getstd - (int64_t)call_getstd_ffrac) / 4;
    put_u32_le(cb->data + call_getstd_ffrac, arm64_bl_imm((int)d6e));
    int64_t d6f = ((int64_t)out->off_write - (int64_t)call_write_ffrac) / 4;
    put_u32_le(cb->data + call_write_ffrac, arm64_bl_imm((int)d6f));
    // say_str
    int64_t d7 = ((int64_t)out->off_say_str_inline - (int64_t)call_saystr_inline) / 4;
    put_u32_le(cb->data + call_saystr_inline, arm64_bl_imm((int)d7));
    int64_t d8 = ((int64_t)out->off_getstd - (int64_t)call_getstd4) / 4;
    put_u32_le(cb->data + call_getstd4, arm64_bl_imm((int)d8));
    int64_t d9 = ((int64_t)out->off_write - (int64_t)call_write4) / 4;
    put_u32_le(cb->data + call_write4, arm64_bl_imm((int)d9));
    // say
    int64_t d10 = ((int64_t)out->off_say_inline - (int64_t)call_say_inline) / 4;
    put_u32_le(cb->data + call_say_inline, arm64_bl_imm((int)d10));
    int64_t d11 = ((int64_t)out->off_getstd - (int64_t)call_getstd5) / 4;
    put_u32_le(cb->data + call_getstd5, arm64_bl_imm((int)d11));
    int64_t d12 = ((int64_t)out->off_write - (int64_t)call_write5) / 4;
    put_u32_le(cb->data + call_write5, arm64_bl_imm((int)d12));
    // say_f
    int64_t d13 = ((int64_t)out->off_say_f_inline - (int64_t)call_sayf_inline) / 4;
    put_u32_le(cb->data + call_sayf_inline, arm64_bl_imm((int)d13));
    int64_t d14 = ((int64_t)out->off_getstd - (int64_t)call_getstd6) / 4;
    put_u32_le(cb->data + call_getstd6, arm64_bl_imm((int)d14));
    int64_t d15 = ((int64_t)out->off_write - (int64_t)call_write6) / 4;
    put_u32_le(cb->data + call_write6, arm64_bl_imm((int)d15));
    // say_multi spacers
    int64_t d16 = ((int64_t)sm_done - (int64_t)sm_jge) / 4;
    put_u32_le(cb->data + sm_jge, arm64_bcond((int)d16, 10));
    int64_t d17 = ((int64_t)out->off_getstd - (int64_t)call_getstd7) / 4;
    put_u32_le(cb->data + call_getstd7, arm64_bl_imm((int)d17));
    int64_t d18 = ((int64_t)out->off_write - (int64_t)call_write7) / 4;
    put_u32_le(cb->data + call_write7, arm64_bl_imm((int)d18));
    int64_t d19 = ((int64_t)sm_str - (int64_t)sm_je_str) / 4;
    put_u32_le(cb->data + sm_je_str, arm64_bcond((int)d19, 0));
    int64_t d20 = ((int64_t)sm_flt - (int64_t)sm_je_flt) / 4;
    put_u32_le(cb->data + sm_je_flt, arm64_bcond((int)d20, 0));
    int64_t d21 = ((int64_t)sm_next - (int64_t)sm_jmp_next) / 4;
    put_u32_le(cb->data + sm_jmp_next, arm64_b_imm((int)d21));
    int64_t d22 = ((int64_t)sm_next - (int64_t)sm_jmp_next2) / 4;
    put_u32_le(cb->data + sm_jmp_next2, arm64_b_imm((int)d22));
    int64_t d23 = ((int64_t)out->off_say_inline - (int64_t)call_say_inline2) / 4;
    put_u32_le(cb->data + call_say_inline2, arm64_bl_imm((int)d23));
    int64_t d24 = ((int64_t)out->off_say_str_inline - (int64_t)call_saystr_inline2) / 4;
    put_u32_le(cb->data + call_saystr_inline2, arm64_bl_imm((int)d24));
    int64_t d25 = ((int64_t)out->off_say_f_inline - (int64_t)call_sayf_inline2) / 4;
    put_u32_le(cb->data + call_sayf_inline2, arm64_bl_imm((int)d25));
    int64_t d26 = ((int64_t)out->off_getstd - (int64_t)call_getstd8) / 4;
    put_u32_le(cb->data + call_getstd8, arm64_bl_imm((int)d26));
    int64_t d27 = ((int64_t)out->off_write - (int64_t)call_write8) / 4;
    put_u32_le(cb->data + call_write8, arm64_bl_imm((int)d27));
  }

  // len(str) -> i64
  out->off_len = cb->len;
  cb_emit4(cb, arm64_add_imm(1, 0, 0)); // x1 = s
  emit_mov_imm64(cb, 0, 0); // len = 0
  size_t len_loop = cb->len;
  cb_emit4(cb, arm64_ldrb_imm(2, 1, 0));
  cb_emit4(cb, arm64_cmp_imm0(2));
  size_t len2_done = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_add_imm(1, 1, 1));
  cb_emit4(cb, arm64_add_imm(0, 0, 1));
  {
    int64_t d = ((int64_t)len_loop - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_b_imm((int)d));
  }
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)len2_done) / 4;
    put_u32_le(cb->data + len2_done, arm64_bcond((int)d, 0));
  }
  cb_emit4(cb, arm64_ret());

  // strcmp(a, b) -> i64
  out->off_strcmp = cb->len;
  out->off_tnstrcmp = out->off_strcmp;
  cb_emit4(cb, arm64_add_imm(2, 0, 0)); // x2 = a
  cb_emit4(cb, arm64_add_imm(3, 1, 0)); // x3 = b
  size_t sc_loop = cb->len;
  cb_emit4(cb, arm64_ldrb_imm(4, 2, 0));
  cb_emit4(cb, arm64_ldrb_imm(5, 3, 0));
  cb_emit4(cb, arm64_cmp_reg(4, 5));
  size_t sc_diff = cb->len; cb_emit4(cb, arm64_bcond(0, 1)); // NE
  cb_emit4(cb, arm64_cmp_imm0(4));
  size_t sc_eq = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_add_imm(2, 2, 1));
  cb_emit4(cb, arm64_add_imm(3, 3, 1));
  {
    int64_t d = ((int64_t)sc_loop - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_b_imm((int)d));
  }
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)sc_diff) / 4;
    put_u32_le(cb->data + sc_diff, arm64_bcond((int)d, 1));
  }
  cb_emit4(cb, arm64_sub_reg(0, 4, 5));
  cb_emit4(cb, arm64_ret());
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)sc_eq) / 4;
    put_u32_le(cb->data + sc_eq, arm64_bcond((int)d, 0));
  }
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // memcpy(dst, src, n) -> dst
  out->off_memcpy = cb->len;
  cb_emit4(cb, arm64_add_imm(3, 0, 0)); // save dst
  size_t mc_loop = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(2));
  size_t mc_done = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_ldrb_imm(4, 1, 0));
  cb_emit4(cb, arm64_strb_imm(4, 0, 0));
  cb_emit4(cb, arm64_add_imm(0, 0, 1));
  cb_emit4(cb, arm64_add_imm(1, 1, 1));
  cb_emit4(cb, arm64_sub_imm(2, 2, 1));
  {
    int64_t d = ((int64_t)mc_loop - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_b_imm((int)d));
  }
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)mc_done) / 4;
    put_u32_le(cb->data + mc_done, arm64_bcond((int)d, 0));
  }
  cb_emit4(cb, arm64_add_imm(0, 3, 0));
  cb_emit4(cb, arm64_ret());

  // memmove(dst, src, n) -> dst
  out->off_memmove = cb->len;
  cb_emit4(cb, arm64_add_imm(3, 0, 0)); // save dst
  cb_emit4(cb, arm64_cmp_reg(0, 1));
  size_t mm_fwd = cb->len; cb_emit4(cb, arm64_bcond(0, 11)); // LT -> forward
  // backward copy
  cb_emit4(cb, arm64_add_reg(4, 0, 2)); // dst + n
  cb_emit4(cb, arm64_add_reg(5, 1, 2)); // src + n
  cb_emit4(cb, arm64_sub_imm(4, 4, 1));
  cb_emit4(cb, arm64_sub_imm(5, 5, 1));
  size_t mm_loopb = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(2));
  size_t mm_doneb = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_ldrb_imm(6, 5, 0));
  cb_emit4(cb, arm64_strb_imm(6, 4, 0));
  cb_emit4(cb, arm64_sub_imm(4, 4, 1));
  cb_emit4(cb, arm64_sub_imm(5, 5, 1));
  cb_emit4(cb, arm64_sub_imm(2, 2, 1));
  {
    int64_t d = ((int64_t)mm_loopb - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_b_imm((int)d));
  }
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)mm_doneb) / 4;
    put_u32_le(cb->data + mm_doneb, arm64_bcond((int)d, 0));
  }
  cb_emit4(cb, arm64_add_imm(0, 3, 0));
  cb_emit4(cb, arm64_ret());
  // forward copy
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)mm_fwd) / 4;
    put_u32_le(cb->data + mm_fwd, arm64_bcond((int)d, 11));
  }
  size_t mm_loopf = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(2));
  size_t mm_donef = cb->len; cb_emit4(cb, arm64_bcond(0, 0));
  cb_emit4(cb, arm64_ldrb_imm(4, 1, 0));
  cb_emit4(cb, arm64_strb_imm(4, 0, 0));
  cb_emit4(cb, arm64_add_imm(0, 0, 1));
  cb_emit4(cb, arm64_add_imm(1, 1, 1));
  cb_emit4(cb, arm64_sub_imm(2, 2, 1));
  {
    int64_t d = ((int64_t)mm_loopf - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_b_imm((int)d));
  }
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)mm_donef) / 4;
    put_u32_le(cb->data + mm_donef, arm64_bcond((int)d, 0));
  }
  cb_emit4(cb, arm64_add_imm(0, 3, 0));
  cb_emit4(cb, arm64_ret());

  // memcmp(a, b, n) -> i64
  out->off_memcmp = cb->len;
  size_t mcmp_loop = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(2));
  size_t mcmp_eq = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_ldrb_imm(3, 0, 0));
  cb_emit4(cb, arm64_ldrb_imm(4, 1, 0));
  cb_emit4(cb, arm64_cmp_reg(3, 4));
  size_t mcmp_diff = cb->len; cb_emit4(cb, arm64_bcond(0, 1)); // NE
  cb_emit4(cb, arm64_add_imm(0, 0, 1));
  cb_emit4(cb, arm64_add_imm(1, 1, 1));
  cb_emit4(cb, arm64_sub_imm(2, 2, 1));
  {
    int64_t d = ((int64_t)mcmp_loop - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_b_imm((int)d));
  }
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)mcmp_diff) / 4;
    put_u32_le(cb->data + mcmp_diff, arm64_bcond((int)d, 1));
  }
  cb_emit4(cb, arm64_sub_reg(0, 3, 4));
  cb_emit4(cb, arm64_ret());
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)mcmp_eq) / 4;
    put_u32_le(cb->data + mcmp_eq, arm64_bcond((int)d, 0));
  }
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // time_now_ns() -> i64
  out->off_time_ns = cb->len;
  cb_emit4(cb, arm64_sub_imm(31, 31, 32));
  emit_mov_imm64(cb, 0, 1); // CLOCK_MONOTONIC
  cb_emit4(cb, arm64_add_imm(1, 31, 0));
  emit_mov_imm64(cb, 8, 113); // sys_clock_gettime
  cb_emit4(cb, arm64_svc_0());
  cb_emit4(cb, arm64_ldr_imm(2, 31, 0));
  cb_emit4(cb, arm64_ldr_imm(3, 31, 1));
  emit_mov_imm64(cb, 4, 1000000000ULL);
  cb_emit4(cb, arm64_mul_reg(2, 2, 4));
  cb_emit4(cb, arm64_add_reg(0, 2, 3));
  cb_emit4(cb, arm64_add_imm(31, 31, 32));
  cb_emit4(cb, arm64_ret());

  // time_now_ms() -> i64
  out->off_time_ms = cb->len;
  arm64_push_lr(cb);
  {
    int64_t d = ((int64_t)out->off_time_ns - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_bl_imm((int)d));
  }
  emit_mov_imm64(cb, 1, 1000000ULL);
  cb_emit4(cb, arm64_udiv_reg(0, 0, 1));
  arm64_pop_lr(cb);
  cb_emit4(cb, arm64_ret());

  // time_now() -> i64 (seconds)
  out->off_time = cb->len;
  arm64_push_lr(cb);
  {
    int64_t d = ((int64_t)out->off_time_ns - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_bl_imm((int)d));
  }
  emit_mov_imm64(cb, 1, 1000000000ULL);
  cb_emit4(cb, arm64_udiv_reg(0, 0, 1));
  arm64_pop_lr(cb);
  cb_emit4(cb, arm64_ret());

  // malloc(size) -> ptr (bump allocator)
  out->off_malloc = cb->len;
  emit_mov_imm64(cb, 9, heap_cur_addr);
  emit_mov_imm64(cb, 10, heap_end_addr);
  cb_emit4(cb, arm64_ldr_reg(1, 9));  // cur
  cb_emit4(cb, arm64_ldr_reg(2, 10)); // end
  cb_emit4(cb, arm64_add_reg(3, 1, 0)); // cur + size
  cb_emit4(cb, arm64_cmp_reg(3, 2));
  size_t mfail = cb->len; cb_emit4(cb, arm64_bcond(0, 12)); // GT
  cb_emit4(cb, arm64_str_reg(3, 9)); // store new cur
  cb_emit4(cb, arm64_add_imm(0, 1, 0)); // return old cur
  cb_emit4(cb, arm64_ret());
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)mfail) / 4;
    put_u32_le(cb->data + mfail, arm64_bcond((int)d, 12));
  }
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // free(ptr) -> 0 (no-op)
  out->off_free = cb->len;
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // fopen(path, mode) -> FILE*
  out->off_fopen = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(0)); // path null?
  size_t j_fopen_ret0 = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_cmp_imm0(1)); // mode null?
  size_t j_fopen_ret0b = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_ldrb_imm(2, 1, 0)); // w2 = mode[0]
  emit_mov_imm64(cb, 3, (uint64_t)'r');
  cb_emit4(cb, arm64_cmp_reg(2, 3));
  size_t j_mode_r = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  emit_mov_imm64(cb, 3, (uint64_t)'w');
  cb_emit4(cb, arm64_cmp_reg(2, 3));
  size_t j_mode_w = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  emit_mov_imm64(cb, 3, (uint64_t)'a');
  cb_emit4(cb, arm64_cmp_reg(2, 3));
  size_t j_mode_a = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  size_t fopen_ret0 = cb->len;
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());
  size_t mode_r = cb->len;
  emit_mov_imm64(cb, 2, 0); // O_RDONLY
  size_t j_mode_open = cb->len; cb_emit4(cb, arm64_b_imm(0));
  size_t mode_w = cb->len;
  emit_mov_imm64(cb, 2, 0x241); // O_WRONLY|O_CREAT|O_TRUNC
  size_t j_mode_open2 = cb->len; cb_emit4(cb, arm64_b_imm(0));
  size_t mode_a = cb->len;
  emit_mov_imm64(cb, 2, 0x441); // O_WRONLY|O_CREAT|O_APPEND
  size_t mode_open = cb->len;
  // x1 = path, x0 = AT_FDCWD
  cb_emit4(cb, arm64_add_imm(1, 0, 0));
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-100);
  emit_mov_imm64(cb, 3, 0x1A4); // 0644
  emit_mov_imm64(cb, 8, 56); // sys_openat
  cb_emit4(cb, arm64_svc_0());
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_fopen_ok = cb->len; cb_emit4(cb, arm64_bcond(10, 0)); // GE
  // error: store errno, return 0
  cb_emit4(cb, arm64_sub_reg(0, 31, 0));
  emit_mov_imm64(cb, 10, err_addr);
  cb_emit4(cb, arm64_str_reg(0, 10));
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());
  size_t fopen_ok = cb->len;
  cb_emit4(cb, arm64_add_imm(9, 0, 0)); // save fd
  emit_mov_imm64(cb, 0, 32);
  {
    int64_t d = ((int64_t)out->off_malloc - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_bl_imm((int)d));
  }
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_fopen_ret0c = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_str_reg(9, 0)); // fd
  emit_mov_imm64(cb, 1, 0);
  cb_emit4(cb, arm64_str_imm(1, 0, 1)); // eof=0
  cb_emit4(cb, arm64_str_imm(1, 0, 2)); // err=0
  emit_mov_imm64(cb, 1, 0xFFFFFFFFFFFFFFFFULL);
  cb_emit4(cb, arm64_str_imm(1, 0, 3)); // pushback=-1
  cb_emit4(cb, arm64_ret());
  {
    int64_t d = ((int64_t)fopen_ret0 - (int64_t)j_fopen_ret0) / 4;
    put_u32_le(cb->data + j_fopen_ret0, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)fopen_ret0 - (int64_t)j_fopen_ret0b) / 4;
    put_u32_le(cb->data + j_fopen_ret0b, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)mode_r - (int64_t)j_mode_r) / 4;
    put_u32_le(cb->data + j_mode_r, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)mode_w - (int64_t)j_mode_w) / 4;
    put_u32_le(cb->data + j_mode_w, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)mode_a - (int64_t)j_mode_a) / 4;
    put_u32_le(cb->data + j_mode_a, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)mode_open - (int64_t)j_mode_open) / 4;
    put_u32_le(cb->data + j_mode_open, arm64_b_imm((int)d));
  }
  {
    int64_t d = ((int64_t)mode_open - (int64_t)j_mode_open2) / 4;
    put_u32_le(cb->data + j_mode_open2, arm64_b_imm((int)d));
  }
  {
    int64_t d = ((int64_t)fopen_ok - (int64_t)j_fopen_ok) / 4;
    put_u32_le(cb->data + j_fopen_ok, arm64_bcond((int)d, 10));
  }
  {
    int64_t d = ((int64_t)fopen_ret0 - (int64_t)j_fopen_ret0c) / 4;
    put_u32_le(cb->data + j_fopen_ret0c, arm64_bcond((int)d, 0));
  }

  // fclose(FILE*) -> int
  out->off_fclose = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_fclose_retm1 = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_add_imm(9, 0, 0)); // save FILE*
  emit_mov_imm64(cb, 1, stdin_addr);
  cb_emit4(cb, arm64_cmp_reg(0, 1));
  size_t j_fclose_ret0_1 = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  emit_mov_imm64(cb, 1, stdout_addr);
  cb_emit4(cb, arm64_cmp_reg(0, 1));
  size_t j_fclose_ret0_2 = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  emit_mov_imm64(cb, 1, stderr_addr);
  cb_emit4(cb, arm64_cmp_reg(0, 1));
  size_t j_fclose_ret0_3 = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_ldr_reg(0, 9)); // fd
  emit_mov_imm64(cb, 8, 57); // sys_close
  cb_emit4(cb, arm64_svc_0());
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_fclose_ok = cb->len; cb_emit4(cb, arm64_bcond(10, 0)); // GE
  cb_emit4(cb, arm64_sub_reg(0, 31, 0));
  emit_mov_imm64(cb, 1, err_addr);
  cb_emit4(cb, arm64_str_reg(0, 1));
  emit_mov_imm64(cb, 0, 0xFFFFFFFFFFFFFFFFULL);
  cb_emit4(cb, arm64_ret());
  size_t fclose_ok = cb->len;
  cb_emit4(cb, arm64_add_imm(0, 9, 0)); // arg for free
  {
    int64_t d = ((int64_t)out->off_free - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_bl_imm((int)d));
  }
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());
  size_t fclose_ret0 = cb->len;
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());
  size_t fclose_retm1 = cb->len;
  emit_mov_imm64(cb, 0, 0xFFFFFFFFFFFFFFFFULL);
  cb_emit4(cb, arm64_ret());
  {
    int64_t d = ((int64_t)fclose_retm1 - (int64_t)j_fclose_retm1) / 4;
    put_u32_le(cb->data + j_fclose_retm1, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)fclose_ret0 - (int64_t)j_fclose_ret0_1) / 4;
    put_u32_le(cb->data + j_fclose_ret0_1, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)fclose_ret0 - (int64_t)j_fclose_ret0_2) / 4;
    put_u32_le(cb->data + j_fclose_ret0_2, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)fclose_ret0 - (int64_t)j_fclose_ret0_3) / 4;
    put_u32_le(cb->data + j_fclose_ret0_3, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)fclose_ok - (int64_t)j_fclose_ok) / 4;
    put_u32_le(cb->data + j_fclose_ok, arm64_bcond((int)d, 10));
  }

  // fread(buf, size, count, f) -> i64
  out->off_fread = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(3));
  size_t j_fread_ret0 = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_fread_ret0b = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_cmp_imm0(1));
  size_t j_fread_ret0c = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_cmp_imm0(2));
  size_t j_fread_ret0d = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_add_imm(9, 3, 0)); // f
  cb_emit4(cb, arm64_add_imm(10, 0, 0)); // buf
  cb_emit4(cb, arm64_mul_reg(11, 1, 2)); // total
  cb_emit4(cb, arm64_cmp_imm0(11));
  size_t j_fread_ret0e = cb->len; cb_emit4(cb, arm64_bcond(13, 0)); // LE
  cb_emit4(cb, arm64_ldr_reg(0, 9)); // fd
  cb_emit4(cb, arm64_add_imm(1, 10, 0)); // buf
  cb_emit4(cb, arm64_add_imm(2, 11, 0)); // n
  emit_mov_imm64(cb, 8, 63); // sys_read
  cb_emit4(cb, arm64_svc_0());
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_fread_ok = cb->len; cb_emit4(cb, arm64_bcond(10, 0)); // GE
  cb_emit4(cb, arm64_sub_reg(0, 31, 0));
  emit_mov_imm64(cb, 1, err_addr);
  cb_emit4(cb, arm64_str_reg(0, 1));
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());
  size_t fread_ok = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_fread_noeof = cb->len; cb_emit4(cb, arm64_bcond(1, 0)); // NE
  emit_mov_imm64(cb, 1, 1);
  cb_emit4(cb, arm64_str_imm(1, 9, 1)); // eof=1
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());
  size_t fread_noeof = cb->len;
  emit_mov_imm64(cb, 1, 0);
  cb_emit4(cb, arm64_str_imm(1, 9, 1)); // eof=0
  cb_emit4(cb, arm64_ret());
  size_t fread_ret0 = cb->len;
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());
  {
    int64_t d = ((int64_t)fread_ret0 - (int64_t)j_fread_ret0) / 4;
    put_u32_le(cb->data + j_fread_ret0, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)fread_ret0 - (int64_t)j_fread_ret0b) / 4;
    put_u32_le(cb->data + j_fread_ret0b, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)fread_ret0 - (int64_t)j_fread_ret0c) / 4;
    put_u32_le(cb->data + j_fread_ret0c, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)fread_ret0 - (int64_t)j_fread_ret0d) / 4;
    put_u32_le(cb->data + j_fread_ret0d, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)fread_ret0 - (int64_t)j_fread_ret0e) / 4;
    put_u32_le(cb->data + j_fread_ret0e, arm64_bcond((int)d, 13));
  }
  {
    int64_t d = ((int64_t)fread_ok - (int64_t)j_fread_ok) / 4;
    put_u32_le(cb->data + j_fread_ok, arm64_bcond((int)d, 10));
  }
  {
    int64_t d = ((int64_t)fread_noeof - (int64_t)j_fread_noeof) / 4;
    put_u32_le(cb->data + j_fread_noeof, arm64_bcond((int)d, 1));
  }

  // fwrite(buf, size, count, f) -> i64
  out->off_fwrite = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(3));
  size_t j_fwrite_ret0 = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_fwrite_ret0b = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_cmp_imm0(1));
  size_t j_fwrite_ret0c = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_cmp_imm0(2));
  size_t j_fwrite_ret0d = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_add_imm(9, 3, 0)); // f
  cb_emit4(cb, arm64_add_imm(10, 0, 0)); // buf
  cb_emit4(cb, arm64_mul_reg(11, 1, 2)); // total
  cb_emit4(cb, arm64_cmp_imm0(11));
  size_t j_fwrite_ret0e = cb->len; cb_emit4(cb, arm64_bcond(13, 0)); // LE
  cb_emit4(cb, arm64_ldr_reg(0, 9)); // fd
  cb_emit4(cb, arm64_add_imm(1, 10, 0)); // buf
  cb_emit4(cb, arm64_add_imm(2, 11, 0)); // n
  emit_mov_imm64(cb, 8, 64); // sys_write
  cb_emit4(cb, arm64_svc_0());
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_fwrite_ok = cb->len; cb_emit4(cb, arm64_bcond(10, 0)); // GE
  cb_emit4(cb, arm64_sub_reg(0, 31, 0));
  emit_mov_imm64(cb, 1, err_addr);
  cb_emit4(cb, arm64_str_reg(0, 1));
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());
  size_t fwrite_ok = cb->len;
  cb_emit4(cb, arm64_ret());
  size_t fwrite_ret0 = cb->len;
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());
  {
    int64_t d = ((int64_t)fwrite_ret0 - (int64_t)j_fwrite_ret0) / 4;
    put_u32_le(cb->data + j_fwrite_ret0, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)fwrite_ret0 - (int64_t)j_fwrite_ret0b) / 4;
    put_u32_le(cb->data + j_fwrite_ret0b, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)fwrite_ret0 - (int64_t)j_fwrite_ret0c) / 4;
    put_u32_le(cb->data + j_fwrite_ret0c, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)fwrite_ret0 - (int64_t)j_fwrite_ret0d) / 4;
    put_u32_le(cb->data + j_fwrite_ret0d, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)fwrite_ret0 - (int64_t)j_fwrite_ret0e) / 4;
    put_u32_le(cb->data + j_fwrite_ret0e, arm64_bcond((int)d, 13));
  }
  {
    int64_t d = ((int64_t)fwrite_ok - (int64_t)j_fwrite_ok) / 4;
    put_u32_le(cb->data + j_fwrite_ok, arm64_bcond((int)d, 10));
  }

  // fseek(f, off, whence) -> int
  out->off_fseek = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_fseek_retm1 = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_add_imm(9, 0, 0)); // save f
  cb_emit4(cb, arm64_ldr_reg(0, 9)); // fd
  cb_emit4(cb, arm64_add_imm(1, 1, 0)); // off
  cb_emit4(cb, arm64_add_imm(2, 2, 0)); // whence
  emit_mov_imm64(cb, 8, 62); // sys_lseek
  cb_emit4(cb, arm64_svc_0());
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_fseek_ok = cb->len; cb_emit4(cb, arm64_bcond(10, 0));
  cb_emit4(cb, arm64_sub_reg(0, 31, 0));
  emit_mov_imm64(cb, 1, err_addr);
  cb_emit4(cb, arm64_str_reg(0, 1));
  emit_mov_imm64(cb, 0, 0xFFFFFFFFFFFFFFFFULL);
  cb_emit4(cb, arm64_ret());
  size_t fseek_ok = cb->len;
  emit_mov_imm64(cb, 1, 0);
  cb_emit4(cb, arm64_str_imm(1, 9, 1)); // eof=0
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());
  size_t fseek_retm1 = cb->len;
  emit_mov_imm64(cb, 0, 0xFFFFFFFFFFFFFFFFULL);
  cb_emit4(cb, arm64_ret());
  {
    int64_t d = ((int64_t)fseek_retm1 - (int64_t)j_fseek_retm1) / 4;
    put_u32_le(cb->data + j_fseek_retm1, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)fseek_ok - (int64_t)j_fseek_ok) / 4;
    put_u32_le(cb->data + j_fseek_ok, arm64_bcond((int)d, 10));
  }

  // ftell(f) -> i64
  out->off_ftell = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_ftell_retm1 = cb->len; cb_emit4(cb, arm64_bcond(0, 0));
  cb_emit4(cb, arm64_ldr_reg(0, 0)); // fd
  emit_mov_imm64(cb, 1, 0);
  emit_mov_imm64(cb, 2, 1); // SEEK_CUR
  emit_mov_imm64(cb, 8, 62); // sys_lseek
  cb_emit4(cb, arm64_svc_0());
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_ftell_ok = cb->len; cb_emit4(cb, arm64_bcond(10, 0));
  cb_emit4(cb, arm64_sub_reg(0, 31, 0));
  emit_mov_imm64(cb, 1, err_addr);
  cb_emit4(cb, arm64_str_reg(0, 1));
  emit_mov_imm64(cb, 0, 0xFFFFFFFFFFFFFFFFULL);
  cb_emit4(cb, arm64_ret());
  size_t ftell_ok = cb->len;
  cb_emit4(cb, arm64_ret());
  size_t ftell_retm1 = cb->len;
  emit_mov_imm64(cb, 0, 0xFFFFFFFFFFFFFFFFULL);
  cb_emit4(cb, arm64_ret());
  {
    int64_t d = ((int64_t)ftell_retm1 - (int64_t)j_ftell_retm1) / 4;
    put_u32_le(cb->data + j_ftell_retm1, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)ftell_ok - (int64_t)j_ftell_ok) / 4;
    put_u32_le(cb->data + j_ftell_ok, arm64_bcond((int)d, 10));
  }

  // fflush(f) -> 0
  out->off_fflush = cb->len;
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // io_err() -> i64
  out->off_io_err = cb->len;
  emit_mov_imm64(cb, 1, err_addr);
  cb_emit4(cb, arm64_ldr_reg(0, 1));
  cb_emit4(cb, arm64_ret());

  // io_eof(f) -> i64
  out->off_io_eof = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_ioe_ret1 = cb->len; cb_emit4(cb, arm64_bcond(0, 0));
  cb_emit4(cb, arm64_ldr_imm(0, 0, 1)); // eof
  cb_emit4(cb, arm64_ret());
  size_t ioe_ret1 = cb->len;
  emit_mov_imm64(cb, 0, 1);
  cb_emit4(cb, arm64_ret());
  {
    int64_t d = ((int64_t)ioe_ret1 - (int64_t)j_ioe_ret1) / 4;
    put_u32_le(cb->data + j_ioe_ret1, arm64_bcond((int)d, 0));
  }

  // read_bytes(f, buf, n) -> i64
  out->off_read_bytes = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_rb_ret0 = cb->len; cb_emit4(cb, arm64_bcond(0, 0));
  cb_emit4(cb, arm64_cmp_imm0(1));
  size_t j_rb_ret0b = cb->len; cb_emit4(cb, arm64_bcond(0, 0));
  cb_emit4(cb, arm64_cmp_imm0(2));
  size_t j_rb_ret0c = cb->len; cb_emit4(cb, arm64_bcond(13, 0)); // LE
  cb_emit4(cb, arm64_add_imm(9, 0, 0)); // f
  cb_emit4(cb, arm64_add_imm(10, 1, 0)); // buf
  cb_emit4(cb, arm64_add_imm(11, 2, 0)); // n
  emit_mov_imm64(cb, 12, 0); // total
  cb_emit4(cb, arm64_ldr_imm(13, 9, 3)); // pushback
  emit_mov_imm64(cb, 14, 0xFFFFFFFFFFFFFFFFULL);
  cb_emit4(cb, arm64_cmp_reg(13, 14));
  size_t j_rb_no_pb = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ => no pushback
  cb_emit4(cb, arm64_strb_imm(13, 10, 0));
  cb_emit4(cb, arm64_str_imm(14, 9, 3)); // pushback=-1
  emit_mov_imm64(cb, 12, 1);
  cb_emit4(cb, arm64_add_imm(10, 10, 1));
  cb_emit4(cb, arm64_sub_imm(11, 11, 1));
  cb_emit4(cb, arm64_cmp_imm0(11));
  size_t j_rb_done_total = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  size_t rb_no_pb = cb->len;
  cb_emit4(cb, arm64_ldr_reg(0, 9)); // fd
  cb_emit4(cb, arm64_add_imm(1, 10, 0));
  cb_emit4(cb, arm64_add_imm(2, 11, 0));
  emit_mov_imm64(cb, 8, 63); // sys_read
  cb_emit4(cb, arm64_svc_0());
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_rb_ok = cb->len; cb_emit4(cb, arm64_bcond(10, 0)); // GE
  cb_emit4(cb, arm64_sub_reg(0, 31, 0));
  emit_mov_imm64(cb, 1, err_addr);
  cb_emit4(cb, arm64_str_reg(0, 1));
  cb_emit4(cb, arm64_cmp_imm0(12));
  size_t j_rb_ret_total = cb->len; cb_emit4(cb, arm64_bcond(1, 0)); // NE
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());
  size_t rb_ret_total = cb->len;
  cb_emit4(cb, arm64_add_imm(0, 12, 0));
  cb_emit4(cb, arm64_ret());
  size_t rb_ok = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_rb_not_eof = cb->len; cb_emit4(cb, arm64_bcond(1, 0)); // NE
  emit_mov_imm64(cb, 1, 1);
  cb_emit4(cb, arm64_str_imm(1, 9, 1)); // eof=1
  cb_emit4(cb, arm64_add_imm(0, 12, 0));
  cb_emit4(cb, arm64_ret());
  size_t rb_not_eof = cb->len;
  emit_mov_imm64(cb, 1, 0);
  cb_emit4(cb, arm64_str_imm(1, 9, 1)); // eof=0
  cb_emit4(cb, arm64_add_reg(0, 0, 12));
  cb_emit4(cb, arm64_ret());
  size_t rb_done_total = cb->len;
  cb_emit4(cb, arm64_add_imm(0, 12, 0));
  cb_emit4(cb, arm64_ret());
  size_t rb_ret0 = cb->len;
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());
  {
    int64_t d = ((int64_t)rb_ret0 - (int64_t)j_rb_ret0) / 4;
    put_u32_le(cb->data + j_rb_ret0, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)rb_ret0 - (int64_t)j_rb_ret0b) / 4;
    put_u32_le(cb->data + j_rb_ret0b, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)rb_ret0 - (int64_t)j_rb_ret0c) / 4;
    put_u32_le(cb->data + j_rb_ret0c, arm64_bcond((int)d, 13));
  }
  {
    int64_t d = ((int64_t)rb_no_pb - (int64_t)j_rb_no_pb) / 4;
    put_u32_le(cb->data + j_rb_no_pb, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)rb_done_total - (int64_t)j_rb_done_total) / 4;
    put_u32_le(cb->data + j_rb_done_total, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)rb_ok - (int64_t)j_rb_ok) / 4;
    put_u32_le(cb->data + j_rb_ok, arm64_bcond((int)d, 10));
  }
  {
    int64_t d = ((int64_t)rb_ret_total - (int64_t)j_rb_ret_total) / 4;
    put_u32_le(cb->data + j_rb_ret_total, arm64_bcond((int)d, 1));
  }
  {
    int64_t d = ((int64_t)rb_not_eof - (int64_t)j_rb_not_eof) / 4;
    put_u32_le(cb->data + j_rb_not_eof, arm64_bcond((int)d, 1));
  }

  // write_line(f, s) -> int
  out->off_write_line = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_wl_retm1 = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_cmp_imm0(1));
  size_t j_wl_retm1b = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_add_imm(9, 0, 0)); // f
  cb_emit4(cb, arm64_add_imm(10, 1, 0)); // s (walker)
  cb_emit4(cb, arm64_add_imm(11, 1, 0)); // s start
  emit_mov_imm64(cb, 2, 0); // len
  size_t wl_len_loop = cb->len;
  cb_emit4(cb, arm64_ldrb_imm(3, 10, 0));
  cb_emit4(cb, arm64_cmp_imm0(3));
  size_t j_wl_len_done = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_add_imm(10, 10, 1));
  cb_emit4(cb, arm64_add_imm(2, 2, 1));
  {
    int64_t d = ((int64_t)wl_len_loop - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_b_imm((int)d));
  }
  size_t wl_len_done = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(2));
  size_t j_wl_write_nl = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_ldr_reg(0, 9)); // fd
  cb_emit4(cb, arm64_add_imm(1, 11, 0)); // buf
  cb_emit4(cb, arm64_add_imm(2, 2, 0)); // len
  emit_mov_imm64(cb, 8, 64); // sys_write
  cb_emit4(cb, arm64_svc_0());
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_wl_err1 = cb->len; cb_emit4(cb, arm64_bcond(11, 0)); // LT
  size_t wl_write_nl = cb->len;
  cb_emit4(cb, arm64_sub_imm(31, 31, 16));
  emit_mov_imm64(cb, 3, 10);
  cb_emit4(cb, arm64_strb_imm(3, 31, 0));
  cb_emit4(cb, arm64_ldr_reg(0, 9));
  cb_emit4(cb, arm64_add_imm(1, 31, 0));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 8, 64);
  cb_emit4(cb, arm64_svc_0());
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_wl_err2 = cb->len; cb_emit4(cb, arm64_bcond(11, 0)); // LT
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());
  size_t wl_err = cb->len;
  cb_emit4(cb, arm64_sub_reg(0, 31, 0));
  emit_mov_imm64(cb, 1, err_addr);
  cb_emit4(cb, arm64_str_reg(0, 1));
  emit_mov_imm64(cb, 0, 0xFFFFFFFFFFFFFFFFULL);
  cb_emit4(cb, arm64_ret());
  size_t wl_retm1 = cb->len;
  emit_mov_imm64(cb, 0, 0xFFFFFFFFFFFFFFFFULL);
  cb_emit4(cb, arm64_ret());
  {
    int64_t d = ((int64_t)wl_retm1 - (int64_t)j_wl_retm1) / 4;
    put_u32_le(cb->data + j_wl_retm1, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)wl_retm1 - (int64_t)j_wl_retm1b) / 4;
    put_u32_le(cb->data + j_wl_retm1b, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)wl_len_done - (int64_t)j_wl_len_done) / 4;
    put_u32_le(cb->data + j_wl_len_done, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)wl_write_nl - (int64_t)j_wl_write_nl) / 4;
    put_u32_le(cb->data + j_wl_write_nl, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)wl_err - (int64_t)j_wl_err1) / 4;
    put_u32_le(cb->data + j_wl_err1, arm64_bcond((int)d, 11));
  }
  {
    int64_t d = ((int64_t)wl_err - (int64_t)j_wl_err2) / 4;
    put_u32_le(cb->data + j_wl_err2, arm64_bcond((int)d, 11));
  }

  // read_line(f) -> *u8 (simple, bounded)
  out->off_read_line = cb->len;
  arm64_push_lr(cb);
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_rl_ret0 = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_sub_imm(31, 31, 320));
  cb_emit4(cb, arm64_str_imm(0, 31, 33)); // f at sp+264
  emit_mov_imm64(cb, 1, 0);
  cb_emit4(cb, arm64_str_imm(1, 31, 32)); // len at sp+256
  size_t rl_loop = cb->len;
  cb_emit4(cb, arm64_ldr_imm(1, 31, 32)); // len
  emit_mov_imm64(cb, 2, 255);
  cb_emit4(cb, arm64_cmp_reg(1, 2));
  size_t j_rl_done = cb->len; cb_emit4(cb, arm64_bcond(10, 0)); // GE
  cb_emit4(cb, arm64_ldr_imm(0, 31, 33)); // f
  cb_emit4(cb, arm64_add_imm(1, 31, 272)); // &ch
  emit_mov_imm64(cb, 2, 1);
  {
    int64_t d = ((int64_t)out->off_read_bytes - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_bl_imm((int)d));
  }
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_rl_done2 = cb->len; cb_emit4(cb, arm64_bcond(13, 0)); // LE
  cb_emit4(cb, arm64_ldrb_imm(3, 31, 272));
  emit_mov_imm64(cb, 4, 10);
  cb_emit4(cb, arm64_cmp_reg(3, 4));
  size_t j_rl_done3 = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  emit_mov_imm64(cb, 4, 13);
  cb_emit4(cb, arm64_cmp_reg(3, 4));
  size_t j_rl_handle_cr = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_add_imm(5, 31, 0));
  cb_emit4(cb, arm64_add_reg(5, 5, 1));
  cb_emit4(cb, arm64_strb_imm(3, 5, 0));
  cb_emit4(cb, arm64_add_imm(1, 1, 1));
  cb_emit4(cb, arm64_str_imm(1, 31, 32));
  {
    int64_t d = ((int64_t)rl_loop - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_b_imm((int)d));
  }
  size_t rl_handle_cr = cb->len;
  cb_emit4(cb, arm64_ldr_imm(0, 31, 33));
  cb_emit4(cb, arm64_add_imm(1, 31, 273));
  emit_mov_imm64(cb, 2, 1);
  {
    int64_t d = ((int64_t)out->off_read_bytes - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_bl_imm((int)d));
  }
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_rl_done4 = cb->len; cb_emit4(cb, arm64_bcond(13, 0)); // LE
  cb_emit4(cb, arm64_ldrb_imm(3, 31, 273));
  emit_mov_imm64(cb, 4, 10);
  cb_emit4(cb, arm64_cmp_reg(3, 4));
  size_t j_rl_done5 = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_ldr_imm(0, 31, 33));
  cb_emit4(cb, arm64_strb_imm(3, 0, 24)); // pushback
  size_t rl_done = cb->len;
  cb_emit4(cb, arm64_ldr_imm(1, 31, 32)); // len
  cb_emit4(cb, arm64_add_imm(1, 1, 1)); // len+1
  cb_emit4(cb, arm64_add_imm(0, 1, 0));
  {
    int64_t d = ((int64_t)out->off_malloc - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_bl_imm((int)d));
  }
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_rl_fail = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_ldr_imm(2, 31, 32)); // len
  cb_emit4(cb, arm64_cmp_imm0(2));
  size_t j_rl_skip_copy = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_add_imm(1, 31, 0)); // src = sp
  {
    int64_t d = ((int64_t)out->off_memcpy - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_bl_imm((int)d));
  }
  size_t rl_skip_copy = cb->len;
  cb_emit4(cb, arm64_ldr_imm(2, 31, 32));
  cb_emit4(cb, arm64_add_reg(3, 0, 2));
  emit_mov_imm64(cb, 4, 0);
  cb_emit4(cb, arm64_strb_imm(4, 3, 0));
  cb_emit4(cb, arm64_add_imm(31, 31, 320));
  arm64_pop_lr(cb);
  cb_emit4(cb, arm64_ret());
  size_t rl_fail = cb->len;
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_add_imm(31, 31, 320));
  arm64_pop_lr(cb);
  cb_emit4(cb, arm64_ret());
  size_t rl_ret0 = cb->len;
  emit_mov_imm64(cb, 0, 0);
  arm64_pop_lr(cb);
  cb_emit4(cb, arm64_ret());
  {
    int64_t d = ((int64_t)rl_done - (int64_t)j_rl_done) / 4;
    put_u32_le(cb->data + j_rl_done, arm64_bcond((int)d, 10));
  }
  {
    int64_t d = ((int64_t)rl_done - (int64_t)j_rl_done2) / 4;
    put_u32_le(cb->data + j_rl_done2, arm64_bcond((int)d, 13));
  }
  {
    int64_t d = ((int64_t)rl_done - (int64_t)j_rl_done3) / 4;
    put_u32_le(cb->data + j_rl_done3, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)rl_handle_cr - (int64_t)j_rl_handle_cr) / 4;
    put_u32_le(cb->data + j_rl_handle_cr, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)rl_done - (int64_t)j_rl_done4) / 4;
    put_u32_le(cb->data + j_rl_done4, arm64_bcond((int)d, 13));
  }
  {
    int64_t d = ((int64_t)rl_done - (int64_t)j_rl_done5) / 4;
    put_u32_le(cb->data + j_rl_done5, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)rl_skip_copy - (int64_t)j_rl_skip_copy) / 4;
    put_u32_le(cb->data + j_rl_skip_copy, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)rl_fail - (int64_t)j_rl_fail) / 4;
    put_u32_le(cb->data + j_rl_fail, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)rl_ret0 - (int64_t)j_rl_ret0) / 4;
    put_u32_le(cb->data + j_rl_ret0, arm64_bcond((int)d, 0));
  }

  // get_cwd() -> *u8
  out->off_get_cwd = cb->len;
  arm64_push_lr(cb);
  emit_mov_imm64(cb, 0, 512);
  {
    int64_t d = ((int64_t)out->off_malloc - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_bl_imm((int)d));
  }
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_cwd_ret0 = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_sub_imm(31, 31, 16));
  cb_emit4(cb, arm64_str_imm(0, 31, 0)); // save buf
  emit_mov_imm64(cb, 1, 512);
  emit_mov_imm64(cb, 8, 79); // sys_getcwd
  cb_emit4(cb, arm64_svc_0());
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_cwd_ok = cb->len; cb_emit4(cb, arm64_bcond(10, 0)); // GE
  cb_emit4(cb, arm64_sub_reg(0, 31, 0));
  emit_mov_imm64(cb, 1, err_addr);
  cb_emit4(cb, arm64_str_reg(0, 1));
  cb_emit4(cb, arm64_ldr_imm(0, 31, 0)); // buf
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
  {
    int64_t d = ((int64_t)out->off_free - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_bl_imm((int)d));
  }
  emit_mov_imm64(cb, 0, 0);
  arm64_pop_lr(cb);
  cb_emit4(cb, arm64_ret());
  size_t cwd_ok = cb->len;
  cb_emit4(cb, arm64_ldr_imm(0, 31, 0)); // buf
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
  arm64_pop_lr(cb);
  cb_emit4(cb, arm64_ret());
  size_t cwd_ret0 = cb->len;
  emit_mov_imm64(cb, 0, 0);
  arm64_pop_lr(cb);
  cb_emit4(cb, arm64_ret());
  {
    int64_t d = ((int64_t)cwd_ret0 - (int64_t)j_cwd_ret0) / 4;
    put_u32_le(cb->data + j_cwd_ret0, arm64_bcond((int)d, 0));
  }
  {
    int64_t d = ((int64_t)cwd_ok - (int64_t)j_cwd_ok) / 4;
    put_u32_le(cb->data + j_cwd_ok, arm64_bcond((int)d, 10));
  }

  // stdin/stdout/stderr
  out->off_stdin = cb->len;
  emit_mov_imm64(cb, 0, stdin_addr);
  cb_emit4(cb, arm64_ret());
  out->off_stdout = cb->len;
  emit_mov_imm64(cb, 0, stdout_addr);
  cb_emit4(cb, arm64_ret());
  out->off_stderr = cb->len;
  emit_mov_imm64(cb, 0, stderr_addr);
  cb_emit4(cb, arm64_ret());
}

static void map_stub(FuncInfo* finfo, int fcount, const char* name, size_t off){
  if(!name) return;
  int nlen = (int)strlen(name);
  for(int i=0;i<fcount;i++){
    if(finfo[i].name && finfo[i].len == nlen && strncmp(finfo[i].name, name, (size_t)nlen)==0){
      finfo[i].off = off;
      finfo[i].has_off = 1;
      return;
    }
  }
}

static void build_macos_arm64_stubs(CodeBuf* cb, Arm64StubInfo* out){
  if(!cb || !out) return;
  // tn_exit(code)
  out->off_exit = cb->len;
  emit_mov_imm64(cb, 16, 0x2000001ULL); // exit
  cb_emit4(cb, arm64_svc_imm(0x80));
  cb_emit4(cb, arm64_ret());

  // tn_getstd(handle) -> fd
  out->off_getstd = cb->len;
  emit_mov_imm64(cb, 1, (uint64_t)(int64_t)-12);
  cb_emit4(cb, arm64_cmp_reg(0, 1));
  cb_emit4(cb, arm64_cset(1, 0)); // eq
  cb_emit4(cb, arm64_add_imm(0, 1, 1)); // 1 or 2
  cb_emit4(cb, arm64_ret());

  // tn_write(handle, buf, len, written*, overlapped) -> 1
  out->off_write = cb->len;
  cb_emit4(cb, arm64_add_imm(9, 3, 0)); // x9 = written*
  emit_mov_imm64(cb, 16, 0x2000004ULL); // write
  emit_mov_imm64(cb, 0, 1); // fd = stdout
  cb_emit4(cb, arm64_svc_imm(0x80));
  cb_emit4(cb, arm64_cmp_imm0(9));
  size_t jpos = cb->len;
  cb_emit4(cb, arm64_bcond(0, 0));
  cb_emit4(cb, arm64_str_reg(0, 9));
  {
    size_t target = cb->len;
    int64_t disp = ((int64_t)target - (int64_t)jpos) / 4;
    put_u32_le(cb->data + jpos, arm64_bcond((int)disp, 0));
  }
  emit_mov_imm64(cb, 0, 1);
  cb_emit4(cb, arm64_ret());

  // tn_gettime(&out) -> gettimeofday
  out->off_gettime = cb->len;
  cb_emit4(cb, arm64_add_imm(9, 0, 0)); // x9 = out*
  cb_emit4(cb, arm64_sub_imm(31, 31, 32));
  emit_mov_imm64(cb, 0, 0); // tv = rsp
  cb_emit4(cb, arm64_add_imm(0, 31, 0));
  emit_mov_imm64(cb, 1, 0); // tz = NULL
  emit_mov_imm64(cb, 16, 0x2000074ULL); // gettimeofday
  cb_emit4(cb, arm64_svc_imm(0x80));
  cb_emit4(cb, arm64_ldr_imm(2, 31, 0)); // sec
  cb_emit4(cb, arm64_ldr_imm(3, 31, 1)); // usec
  emit_mov_imm64(cb, 4, 1000000ULL);
  cb_emit4(cb, arm64_mul_reg(3, 3, 4)); // usec * 1e6 = nsec
  emit_mov_imm64(cb, 4, 1000000000ULL);
  cb_emit4(cb, arm64_mul_reg(2, 2, 4));
  cb_emit4(cb, arm64_add_reg(2, 2, 3));
  cb_emit4(cb, arm64_str_reg(2, 9));
  cb_emit4(cb, arm64_add_imm(31, 31, 32));
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // tn_valloc(size) -> mmap (minimal)
  out->off_valloc = cb->len;
  // allocate size + 16, store size, return ptr+16
  emit_mov_imm64(cb, 1, 16);
  cb_emit4(cb, arm64_add_reg(1, 0, 1)); // x1 = size + 16
  emit_mov_imm64(cb, 0, 0); // addr
  emit_mov_imm64(cb, 2, 3); // PROT_READ|WRITE
  emit_mov_imm64(cb, 3, 0x1002); // MAP_PRIVATE|ANON
  emit_mov_imm64(cb, 4, (uint64_t)(int64_t)-1);
  emit_mov_imm64(cb, 5, 0);
  emit_mov_imm64(cb, 16, 0x20000C5ULL); // mmap
  cb_emit4(cb, arm64_svc_imm(0x80));
  // store size at [rax]
  cb_emit4(cb, arm64_str_reg(0, 0));
  cb_emit4(cb, arm64_add_imm(0, 0, 16));
  cb_emit4(cb, arm64_ret());

  // tn_vfree(ptr) -> munmap (uses size header)
  out->off_vfree = cb->len;
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_vf_done = cb->len; cb_emit4(cb, arm64_bcond(0, 0)); // EQ
  cb_emit4(cb, arm64_sub_imm(0, 0, 16));
  cb_emit4(cb, arm64_ldr_reg(1, 0)); // size
  emit_mov_imm64(cb, 16, 0x2000049ULL); // munmap
  cb_emit4(cb, arm64_svc_imm(0x80));
  {
    size_t target = cb->len;
    int64_t disp = ((int64_t)target - (int64_t)j_vf_done) / 4;
    put_u32_le(cb->data + j_vf_done, arm64_bcond((int)disp, 0));
  }
  cb_emit4(cb, arm64_ret());
}

static void build_win_arm64_stubs(CodeBuf* cb, uint64_t image_base, const PeImportInfo* imp, Arm64StubInfo* out){
  if(!cb || !out || !imp) return;
  uint64_t iat_getstd = image_base + (uint64_t)imp->iat_getstd;
  uint64_t iat_write = image_base + (uint64_t)imp->iat_write;
  uint64_t iat_lstrlen = image_base + (uint64_t)imp->iat_lstrlen;
  uint64_t iat_lstrcmp = image_base + (uint64_t)imp->iat_lstrcmp;
  uint64_t iat_gettime = image_base + (uint64_t)imp->iat_gettime;
  uint64_t iat_valloc = image_base + (uint64_t)imp->iat_valloc;
  uint64_t iat_vfree = image_base + (uint64_t)imp->iat_vfree;
  uint64_t iat_memcpy = image_base + (uint64_t)imp->iat_memcpy;
  uint64_t iat_memmove = image_base + (uint64_t)imp->iat_memmove;
  uint64_t iat_memcmp = image_base + (uint64_t)imp->iat_memcmp;
  // tn_getstd(handle) -> GetStdHandle
  out->off_getstd = cb->len;
  emit_call_iat(cb, iat_getstd);
  cb_emit4(cb, arm64_ret());

  // tn_write(handle, buf, len, written*, overlapped) -> WriteFile
  out->off_write = cb->len;
  emit_call_iat(cb, iat_write);
  cb_emit4(cb, arm64_ret());

  // say_str_inline
  out->off_say_str_inline = cb->len;
  cb_emit4(cb, arm64_add_imm(10, 0, 0)); // x10 = s
  cb_emit4(cb, arm64_add_imm(1, 0, 0));
  emit_mov_imm64(cb, 2, 0);
  size_t len_loop = cb->len;
  cb_emit4(cb, arm64_ldrb_imm(3, 1, 0));
  cb_emit4(cb, arm64_cmp_imm0(3));
  size_t j_len_done = cb->len; cb_emit4(cb, arm64_bcond(0, 0));
  cb_emit4(cb, arm64_add_imm(1, 1, 1));
  cb_emit4(cb, arm64_add_imm(2, 2, 1));
  int64_t disp = ((int64_t)len_loop - (int64_t)cb->len) / 4;
  cb_emit4(cb, arm64_b_imm((int)disp));
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)j_len_done) / 4;
    put_u32_le(cb->data + j_len_done, arm64_bcond((int)d, 0));
  }
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(1, 10, 0));
  cb_emit4(cb, arm64_add_imm(2, 2, 0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // say_inline
  out->off_say_inline = cb->len;
  cb_emit4(cb, arm64_sub_imm(31, 31, 64));
  emit_mov_imm64(cb, 5, 0);
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_nonneg = cb->len; cb_emit4(cb, arm64_bcond(10, 0));
  cb_emit4(cb, arm64_sub_reg(0, 31, 0));
  emit_mov_imm64(cb, 5, 1);
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)j_nonneg) / 4;
    put_u32_le(cb->data + j_nonneg, arm64_bcond((int)d, 10));
  }
  cb_emit4(cb, arm64_add_imm(1, 31, 63));
  emit_mov_imm64(cb, 2, 0);
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t j_nonzero = cb->len; cb_emit4(cb, arm64_bcond(1, 0));
  emit_mov_imm64(cb, 3, '0');
  cb_emit4(cb, arm64_strb_imm(3, 1, 0));
  cb_emit4(cb, arm64_sub_imm(1, 1, 1));
  cb_emit4(cb, arm64_add_imm(2, 2, 1));
  size_t j_after_digits = cb->len; cb_emit4(cb, arm64_b_imm(0));
  size_t loop_digits = cb->len;
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)j_nonzero) / 4;
    put_u32_le(cb->data + j_nonzero, arm64_bcond((int)d, 1));
  }
  emit_mov_imm64(cb, 4, 10);
  cb_emit4(cb, arm64_udiv_reg(3, 0, 4));
  cb_emit4(cb, arm64_mul_reg(6, 3, 4));
  cb_emit4(cb, arm64_sub_reg(6, 0, 6));
  emit_mov_imm64(cb, 7, '0');
  cb_emit4(cb, arm64_add_reg(6, 6, 7));
  cb_emit4(cb, arm64_strb_imm(6, 1, 0));
  cb_emit4(cb, arm64_sub_imm(1, 1, 1));
  cb_emit4(cb, arm64_add_imm(2, 2, 1));
  cb_emit4(cb, arm64_add_imm(0, 3, 0));
  cb_emit4(cb, arm64_cmp_imm0(0));
  int64_t dloop3 = ((int64_t)loop_digits - (int64_t)cb->len) / 4;
  cb_emit4(cb, arm64_bcond((int)dloop3, 1));
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)j_after_digits) / 4;
    put_u32_le(cb->data + j_after_digits, arm64_b_imm((int)d));
  }
  cb_emit4(cb, arm64_cmp_imm0(5));
  size_t j_nosign = cb->len; cb_emit4(cb, arm64_bcond(0, 0));
  emit_mov_imm64(cb, 3, '-');
  cb_emit4(cb, arm64_strb_imm(3, 1, 0));
  cb_emit4(cb, arm64_sub_imm(1, 1, 1));
  cb_emit4(cb, arm64_add_imm(2, 2, 1));
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)j_nosign) / 4;
    put_u32_le(cb->data + j_nosign, arm64_bcond((int)d, 0));
  }
  cb_emit4(cb, arm64_add_imm(1, 1, 1));
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd2 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(1, 1, 0));
  cb_emit4(cb, arm64_add_imm(2, 2, 0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write2 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(31, 31, 64));
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // say_f_inline: fixed 6-digit formatter (no libc)
  out->off_say_f_inline = cb->len;
  cb_emit4(cb, arm64_sub_imm(31, 31, 96));
  cb_emit4(cb, arm64_cmp_imm0(0));
  size_t jf_nonneg = cb->len; cb_emit4(cb, arm64_bcond(10, 0));
  emit_mov_imm64(cb, 1, '-');
  cb_emit4(cb, arm64_strb_imm(1, 31, 80));
  cb_emit4(cb, arm64_add_imm(1, 31, 80));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd3 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write3 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 9, 0x7FFFFFFFFFFFFFFFULL);
  cb_emit4(cb, arm64_and_reg(0, 0, 9));
  {
    size_t target = cb->len;
    int64_t d = ((int64_t)target - (int64_t)jf_nonneg) / 4;
    put_u32_le(cb->data + jf_nonneg, arm64_bcond((int)d, 10));
  }
  cb_emit4(cb, arm64_fmov_dx(0, 0));
  cb_emit4(cb, arm64_fcvtzs_xd(1, 0));
  cb_emit4(cb, arm64_scvtf_dx(1, 1));
  cb_emit4(cb, arm64_fsub_dd(0, 0, 1));
  emit_mov_imm64(cb, 2, 0x412E848000000000ULL);
  cb_emit4(cb, arm64_fmov_dx(1, 2));
  cb_emit4(cb, arm64_fmul_dd(0, 0, 1));
  cb_emit4(cb, arm64_fcvtzs_xd(2, 0));
  cb_emit4(cb, arm64_str_imm(2, 31, 8)); // preserve frac
  cb_emit4(cb, arm64_add_imm(0, 1, 0));
  size_t call_sayf_int = cb->len; cb_emit4(cb, arm64_bl_imm(0)); // say_inline
  emit_mov_imm64(cb, 1, '.');
  cb_emit4(cb, arm64_strb_imm(1, 31, 80));
  cb_emit4(cb, arm64_add_imm(1, 31, 80));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd_fdot = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write_fdot = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_ldr_imm(2, 31, 8)); // reload frac
  cb_emit4(cb, arm64_add_imm(1, 31, 5));
  emit_mov_imm64(cb, 4, 6);
  size_t frac_loop = cb->len;
  emit_mov_imm64(cb, 5, 10);
  cb_emit4(cb, arm64_udiv_reg(6, 2, 5));
  cb_emit4(cb, arm64_mul_reg(7, 6, 5));
  cb_emit4(cb, arm64_sub_reg(7, 2, 7));
  emit_mov_imm64(cb, 8, '0');
  cb_emit4(cb, arm64_add_reg(7, 7, 8));
  cb_emit4(cb, arm64_strb_imm(7, 1, 0));
  cb_emit4(cb, arm64_sub_imm(1, 1, 1));
  cb_emit4(cb, arm64_add_imm(2, 6, 0));
  cb_emit4(cb, arm64_sub_imm(4, 4, 1));
  cb_emit4(cb, arm64_cmp_imm0(4));
  int64_t dloop4 = ((int64_t)frac_loop - (int64_t)cb->len) / 4;
  cb_emit4(cb, arm64_bcond((int)dloop4, 1));
  cb_emit4(cb, arm64_add_imm(1, 31, 0));
  emit_mov_imm64(cb, 2, 6);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd_ffrac = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write_ffrac = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(31, 31, 96));
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // say_str (newline)
  out->off_say_str = cb->len;
  size_t call_saystr_inline = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_sub_imm(31, 31, 16));
  emit_mov_imm64(cb, 1, 10);
  cb_emit4(cb, arm64_strb_imm(1, 31, 0));
  cb_emit4(cb, arm64_add_imm(1, 31, 0));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd4 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write4 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // say (newline)
  out->off_say = cb->len;
  size_t call_say_inline = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_sub_imm(31, 31, 16));
  emit_mov_imm64(cb, 1, 10);
  cb_emit4(cb, arm64_strb_imm(1, 31, 0));
  cb_emit4(cb, arm64_add_imm(1, 31, 0));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd5 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write5 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // say_f (newline)
  out->off_say_f = cb->len;
  size_t call_sayf_inline = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_sub_imm(31, 31, 16));
  emit_mov_imm64(cb, 1, 10);
  cb_emit4(cb, arm64_strb_imm(1, 31, 0));
  cb_emit4(cb, arm64_add_imm(1, 31, 0));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd6 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write6 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // say_multi(vals:*i64, tags:*i64, argc:i64)
  out->off_say_multi = cb->len;
  // save callee-saved regs we use (x19-x22)
  cb_emit4(cb, arm64_sub_imm(31, 31, 32));
  cb_emit4(cb, arm64_str_imm(19, 31, 0));
  cb_emit4(cb, arm64_str_imm(20, 31, 1));
  cb_emit4(cb, arm64_str_imm(21, 31, 2));
  cb_emit4(cb, arm64_str_imm(22, 31, 3));
  // move args into callee-saved regs so calls don't clobber loop state
  cb_emit4(cb, arm64_add_imm(19, 0, 0)); // vals*
  cb_emit4(cb, arm64_add_imm(20, 1, 0)); // tags*
  cb_emit4(cb, arm64_add_imm(21, 2, 0)); // argc
  emit_mov_imm64(cb, 22, 0); // i
  size_t sm_loop = cb->len;
  cb_emit4(cb, arm64_cmp_reg(22, 21));
  size_t sm_jge = cb->len; cb_emit4(cb, arm64_bcond(10, 0));
  cb_emit4(cb, arm64_cmp_imm0(22));
  size_t sm_je_nospace = cb->len; cb_emit4(cb, arm64_bcond(0, 0));
  cb_emit4(cb, arm64_sub_imm(31, 31, 16));
  emit_mov_imm64(cb, 4, ' ');
  cb_emit4(cb, arm64_strb_imm(4, 31, 0));
  cb_emit4(cb, arm64_add_imm(1, 31, 0));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd7 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write7 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
  size_t sm_nospace = cb->len;
  {
    int64_t d = ((int64_t)sm_nospace - (int64_t)sm_je_nospace) / 4;
    put_u32_le(cb->data + sm_je_nospace, arm64_bcond((int)d, 0));
  }
  emit_mov_imm64(cb, 6, 8);
  cb_emit4(cb, arm64_mul_reg(6, 22, 6));
  cb_emit4(cb, arm64_add_reg(4, 20, 6));
  cb_emit4(cb, arm64_add_reg(5, 19, 6));
  cb_emit4(cb, arm64_ldr_reg(7, 4));
  cb_emit4(cb, arm64_ldr_reg(8, 5));
  emit_mov_imm64(cb, 9, 2);
  cb_emit4(cb, arm64_cmp_reg(7, 9));
  size_t sm_je_str = cb->len; cb_emit4(cb, arm64_bcond(0, 0));
  emit_mov_imm64(cb, 9, 1);
  cb_emit4(cb, arm64_cmp_reg(7, 9));
  size_t sm_je_flt = cb->len; cb_emit4(cb, arm64_bcond(0, 0));
  cb_emit4(cb, arm64_add_imm(0, 8, 0));
  size_t sm_call_say = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  size_t sm_jmp_next = cb->len; cb_emit4(cb, arm64_b_imm(0));
  size_t sm_str = cb->len;
  cb_emit4(cb, arm64_add_imm(0, 8, 0));
  size_t sm_call_saystr = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  size_t sm_jmp_next2 = cb->len; cb_emit4(cb, arm64_b_imm(0));
  size_t sm_flt = cb->len;
  cb_emit4(cb, arm64_add_imm(0, 8, 0));
  size_t sm_call_sayf = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  size_t sm_next = cb->len;
  cb_emit4(cb, arm64_add_imm(22, 22, 1));
  int64_t dloop2 = ((int64_t)sm_loop - (int64_t)cb->len) / 4;
  cb_emit4(cb, arm64_b_imm((int)dloop2));
  size_t sm_done = cb->len;
  cb_emit4(cb, arm64_sub_imm(31, 31, 16));
  emit_mov_imm64(cb, 4, 10);
  cb_emit4(cb, arm64_strb_imm(4, 31, 0));
  cb_emit4(cb, arm64_add_imm(1, 31, 0));
  emit_mov_imm64(cb, 2, 1);
  emit_mov_imm64(cb, 0, (uint64_t)(int64_t)-11);
  size_t call_getstd8 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  emit_mov_imm64(cb, 3, 0);
  emit_mov_imm64(cb, 4, 0);
  size_t call_write8 = cb->len; cb_emit4(cb, arm64_bl_imm(0));
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
  // restore callee-saved regs
  cb_emit4(cb, arm64_ldr_imm(19, 31, 0));
  cb_emit4(cb, arm64_ldr_imm(20, 31, 1));
  cb_emit4(cb, arm64_ldr_imm(21, 31, 2));
  cb_emit4(cb, arm64_ldr_imm(22, 31, 3));
  cb_emit4(cb, arm64_add_imm(31, 31, 32));
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // len helper (lstrlenA)
  out->off_len = cb->len;
  emit_call_iat(cb, iat_lstrlen);
  cb_emit4(cb, arm64_ret());

  // strcmp helper (lstrcmpA)
  out->off_strcmp = cb->len;
  emit_call_iat(cb, iat_lstrcmp);
  cb_emit4(cb, arm64_ret());
  out->off_tnstrcmp = out->off_strcmp;

  // memcpy/memmove/memcmp helpers
  out->off_memcpy = cb->len;
  emit_call_iat(cb, iat_memcpy);
  cb_emit4(cb, arm64_ret());

  out->off_memmove = cb->len;
  emit_call_iat(cb, iat_memmove);
  cb_emit4(cb, arm64_ret());

  out->off_memcmp = cb->len;
  emit_call_iat(cb, iat_memcmp);
  cb_emit4(cb, arm64_ret());

  // time_now_ns helper (GetSystemTimePreciseAsFileTime * 100)
  out->off_time_ns = cb->len;
  cb_emit4(cb, arm64_sub_imm(31, 31, 16));
  cb_emit4(cb, arm64_add_imm(0, 31, 0)); // x0 = &FILETIME
  emit_call_iat(cb, iat_gettime);
  cb_emit4(cb, arm64_ldr_imm(0, 31, 0)); // x0 = filetime
  emit_mov_imm64(cb, 1, 100);
  cb_emit4(cb, arm64_mul_reg(0, 0, 1));
  cb_emit4(cb, arm64_add_imm(31, 31, 16));
  cb_emit4(cb, arm64_ret());

  // time_now_ms helper
  out->off_time_ms = cb->len;
  {
    int64_t disp_time = ((int64_t)out->off_time_ns - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_bl_imm((int)disp_time));
  }
  emit_mov_imm64(cb, 1, 1000000);
  cb_emit4(cb, arm64_udiv_reg(0, 0, 1));
  cb_emit4(cb, arm64_ret());

  // time_now helper (seconds)
  out->off_time = cb->len;
  {
    int64_t disp_time = ((int64_t)out->off_time_ns - (int64_t)cb->len) / 4;
    cb_emit4(cb, arm64_bl_imm((int)disp_time));
  }
  emit_mov_imm64(cb, 1, 1000000000ULL);
  cb_emit4(cb, arm64_udiv_reg(0, 0, 1));
  cb_emit4(cb, arm64_ret());

  // malloc helper (VirtualAlloc)
  out->off_malloc = cb->len;
  cb_emit4(cb, arm64_add_imm(1, 0, 0)); // size -> x1
  emit_mov_imm64(cb, 0, 0); // lpAddress
  emit_mov_imm64(cb, 2, 0x3000);
  emit_mov_imm64(cb, 3, 0x04);
  emit_call_iat(cb, iat_valloc);
  cb_emit4(cb, arm64_ret());

  // free helper (VirtualFree)
  out->off_free = cb->len;
  emit_mov_imm64(cb, 1, 0);
  emit_mov_imm64(cb, 2, 0x8000);
  emit_call_iat(cb, iat_vfree);
  emit_mov_imm64(cb, 0, 0);
  cb_emit4(cb, arm64_ret());

  // patch calls to tn_getstd/tn_write and inline helpers
  {
    int64_t d1 = ((int64_t)out->off_getstd - (int64_t)call_getstd) / 4;
    put_u32_le(cb->data + call_getstd, arm64_bl_imm((int)d1));
    int64_t d2 = ((int64_t)out->off_write - (int64_t)call_write) / 4;
    put_u32_le(cb->data + call_write, arm64_bl_imm((int)d2));
    int64_t d3 = ((int64_t)out->off_getstd - (int64_t)call_getstd2) / 4;
    put_u32_le(cb->data + call_getstd2, arm64_bl_imm((int)d3));
    int64_t d4 = ((int64_t)out->off_write - (int64_t)call_write2) / 4;
    put_u32_le(cb->data + call_write2, arm64_bl_imm((int)d4));
    int64_t d5 = ((int64_t)out->off_getstd - (int64_t)call_getstd3) / 4;
    put_u32_le(cb->data + call_getstd3, arm64_bl_imm((int)d5));
    int64_t d6 = ((int64_t)out->off_write - (int64_t)call_write3) / 4;
    put_u32_le(cb->data + call_write3, arm64_bl_imm((int)d6));
    int64_t d7 = ((int64_t)out->off_say_str_inline - (int64_t)call_saystr_inline) / 4;
    put_u32_le(cb->data + call_saystr_inline, arm64_bl_imm((int)d7));
    int64_t d8 = ((int64_t)out->off_getstd - (int64_t)call_getstd4) / 4;
    put_u32_le(cb->data + call_getstd4, arm64_bl_imm((int)d8));
    int64_t d9 = ((int64_t)out->off_write - (int64_t)call_write4) / 4;
    put_u32_le(cb->data + call_write4, arm64_bl_imm((int)d9));
    int64_t d10 = ((int64_t)out->off_say_inline - (int64_t)call_say_inline) / 4;
    put_u32_le(cb->data + call_say_inline, arm64_bl_imm((int)d10));
    int64_t d11 = ((int64_t)out->off_getstd - (int64_t)call_getstd5) / 4;
    put_u32_le(cb->data + call_getstd5, arm64_bl_imm((int)d11));
    int64_t d12 = ((int64_t)out->off_write - (int64_t)call_write5) / 4;
    put_u32_le(cb->data + call_write5, arm64_bl_imm((int)d12));
    int64_t d13 = ((int64_t)out->off_say_f_inline - (int64_t)call_sayf_inline) / 4;
    put_u32_le(cb->data + call_sayf_inline, arm64_bl_imm((int)d13));
    int64_t d13b = ((int64_t)out->off_say_inline - (int64_t)call_sayf_int) / 4;
    put_u32_le(cb->data + call_sayf_int, arm64_bl_imm((int)d13b));
    int64_t d13c = ((int64_t)out->off_getstd - (int64_t)call_getstd_fdot) / 4;
    put_u32_le(cb->data + call_getstd_fdot, arm64_bl_imm((int)d13c));
    int64_t d13d = ((int64_t)out->off_write - (int64_t)call_write_fdot) / 4;
    put_u32_le(cb->data + call_write_fdot, arm64_bl_imm((int)d13d));
    int64_t d13e = ((int64_t)out->off_getstd - (int64_t)call_getstd_ffrac) / 4;
    put_u32_le(cb->data + call_getstd_ffrac, arm64_bl_imm((int)d13e));
    int64_t d13f = ((int64_t)out->off_write - (int64_t)call_write_ffrac) / 4;
    put_u32_le(cb->data + call_write_ffrac, arm64_bl_imm((int)d13f));
    int64_t d14 = ((int64_t)out->off_getstd - (int64_t)call_getstd6) / 4;
    put_u32_le(cb->data + call_getstd6, arm64_bl_imm((int)d14));
    int64_t d15 = ((int64_t)out->off_write - (int64_t)call_write6) / 4;
    put_u32_le(cb->data + call_write6, arm64_bl_imm((int)d15));
    int64_t d16 = ((int64_t)sm_done - (int64_t)sm_jge) / 4;
    put_u32_le(cb->data + sm_jge, arm64_bcond((int)d16, 10));
    int64_t d17 = ((int64_t)out->off_getstd - (int64_t)call_getstd7) / 4;
    put_u32_le(cb->data + call_getstd7, arm64_bl_imm((int)d17));
    int64_t d18 = ((int64_t)out->off_write - (int64_t)call_write7) / 4;
    put_u32_le(cb->data + call_write7, arm64_bl_imm((int)d18));
    int64_t d19 = ((int64_t)sm_str - (int64_t)sm_je_str) / 4;
    put_u32_le(cb->data + sm_je_str, arm64_bcond((int)d19, 0));
    int64_t d20 = ((int64_t)sm_flt - (int64_t)sm_je_flt) / 4;
    put_u32_le(cb->data + sm_je_flt, arm64_bcond((int)d20, 0));
    int64_t d21 = ((int64_t)sm_next - (int64_t)sm_jmp_next) / 4;
    put_u32_le(cb->data + sm_jmp_next, arm64_b_imm((int)d21));
    int64_t d22 = ((int64_t)sm_next - (int64_t)sm_jmp_next2) / 4;
    put_u32_le(cb->data + sm_jmp_next2, arm64_b_imm((int)d22));
    int64_t d23 = ((int64_t)out->off_say_inline - (int64_t)sm_call_say) / 4;
    put_u32_le(cb->data + sm_call_say, arm64_bl_imm((int)d23));
    int64_t d24 = ((int64_t)out->off_say_str - (int64_t)sm_call_saystr) / 4;
    put_u32_le(cb->data + sm_call_saystr, arm64_bl_imm((int)d24));
    int64_t d25 = ((int64_t)out->off_say_f - (int64_t)sm_call_sayf) / 4;
    put_u32_le(cb->data + sm_call_sayf, arm64_bl_imm((int)d25));
    int64_t d26 = ((int64_t)out->off_getstd - (int64_t)call_getstd8) / 4;
    put_u32_le(cb->data + call_getstd8, arm64_bl_imm((int)d26));
    int64_t d27 = ((int64_t)out->off_write - (int64_t)call_write8) / 4;
    put_u32_le(cb->data + call_write8, arm64_bl_imm((int)d27));
  }
}

int ir_arm64_codegen_text(IRModule* m, unsigned char** out_text, uint32_t* out_size, size_t* out_main_off){
  if(!m || !out_text || !out_size || !out_main_off) return -1;
  CodeBuf cb;
  cb_init(&cb, 2048);
  FuncInfo* finfo = (FuncInfo*)calloc((size_t)m->fn_n, sizeof(FuncInfo));
  if(!finfo){
    cb_free(&cb);
    return -1;
  }
  int fcount = m->fn_n;
  int main_idx = -1;
  for(int i=0;i<m->fn_n;i++){
    finfo[i].name = m->fns[i].name;
    finfo[i].len = m->fns[i].len;
    finfo[i].is_extern = m->fns[i].is_extern;
    finfo[i].off = 0;
    finfo[i].has_off = 0;
    if(!m->fns[i].is_extern && m->fns[i].name && strcmp(m->fns[i].name, "main") == 0){
      main_idx = i;
    }
  }

  CallPatch cpatch[256];
  int cpatch_n = 0;

  for(int i=0;i<m->fn_n;i++){
    if(m->fns[i].is_extern) continue;
    size_t off = 0;
    if(lower_func_arm64(&m->fns[i], &cb, finfo, fcount, &off, NULL,
                        cpatch, &cpatch_n, (int)(sizeof(cpatch)/sizeof(cpatch[0])),
                        NULL, 0, NULL, 0, 0, 0) != 0){
      cb_free(&cb);
      free(finfo);
      return -1;
    }
    finfo[i].off = off;
    finfo[i].has_off = 1;
  }

  for(int i=0;i<cpatch_n;i++){
    int tidx = cpatch[i].func_idx;
    if(tidx < 0 || tidx >= fcount || !finfo[tidx].has_off){
      cb_free(&cb);
      free(finfo);
      return -1;
    }
    size_t pos = cpatch[i].pos;
    size_t tgt = finfo[tidx].off;
    int64_t disp = ((int64_t)tgt - (int64_t)pos) / 4;
    if(disp < -(1<<25) || disp > ((1<<25)-1)){
      cb_free(&cb);
      free(finfo);
      return -1;
    }
    uint32_t ins = arm64_bl_imm((int)disp);
    put_u32_le(cb.data + pos, ins);
  }

  if(main_idx < 0 || !finfo[main_idx].has_off){
    cb_free(&cb);
    free(finfo);
    return -1;
  }

  *out_main_off = finfo[main_idx].off;
  *out_size = (uint32_t)cb.len;
  *out_text = cb.data;
  free(finfo);
  return 0;
}

int ir_arm64_codegen_text_win(IRModule* m, uint64_t image_base, const PeImportInfo* imp,
                              const uint32_t* str_rva, int str_n,
                              GlobalInfo* ginfo, int gcount,
                              unsigned char** out_text, uint32_t* out_size, size_t* out_main_off){
  if(!m || !out_text || !out_size || !out_main_off || !imp) return -1;
  CodeBuf cb;
  cb_init(&cb, 2048);
  static const char* arm64_builtins[] = {
    "say","say_str","say_f","say_multi","len","strcmp","tn_strcmp",
    "memcpy","memmove","memcmp","malloc","free",
    "time_now","time_now_ms","time_now_ns",
    "fopen","fclose","fread","fwrite","fseek","ftell","fflush",
    "io_err","io_eof","read_line","read_bytes","write_line",
    "stdin","stdout","stderr","get_cwd"
  };
  int builtin_n = (int)(sizeof(arm64_builtins)/sizeof(arm64_builtins[0]));
  int fcount = m->fn_n + builtin_n;
  FuncInfo* finfo = (FuncInfo*)calloc((size_t)fcount, sizeof(FuncInfo));
  if(!finfo){
    cb_free(&cb);
    return -1;
  }
  int main_idx = -1;
  for(int i=0;i<m->fn_n;i++){
    finfo[i].name = m->fns[i].name;
    finfo[i].len = m->fns[i].len;
    finfo[i].is_extern = m->fns[i].is_extern;
    finfo[i].off = 0;
    finfo[i].has_off = 0;
    if(!m->fns[i].is_extern && m->fns[i].name && strcmp(m->fns[i].name, "main") == 0){
      main_idx = i;
    }
  }
  for(int i=0;i<builtin_n;i++){
    int idx = m->fn_n + i;
    finfo[idx].name = arm64_builtins[i];
    finfo[idx].len = (int)strlen(arm64_builtins[i]);
    finfo[idx].is_extern = 1;
    finfo[idx].off = 0;
    finfo[idx].has_off = 0;
  }

  // Reachability: only lower functions reachable from main
  unsigned char* reach = (unsigned char*)calloc((size_t)m->fn_n, 1);
  int* stack = (int*)malloc(sizeof(int)*(size_t)(m->fn_n > 0 ? m->fn_n : 1));
  int sp = 0;
  int init_idx = -1;
  if(main_idx < 0){
    cb_free(&cb);
    free(finfo);
    free(reach);
    free(stack);
    return -1;
  }
  for(int i=0;i<m->fn_n;i++){
    if(finfo[i].is_extern) continue;
    if((finfo[i].len==9 && strncmp(finfo[i].name, "tezz_init", 9)==0) ||
       (finfo[i].len==11 && strncmp(finfo[i].name, "__tezz_init", 11)==0) ||
       (finfo[i].len>10 && finfo[i].name[finfo[i].len-10]=='.' &&
        strncmp(finfo[i].name + (finfo[i].len-9), "tezz_init", 9)==0) ||
       (finfo[i].len>12 && finfo[i].name[finfo[i].len-12]=='.' &&
        strncmp(finfo[i].name + (finfo[i].len-11), "__tezz_init", 11)==0)){
      init_idx = i;
      break;
    }
  }
  reach[main_idx] = 1;
  stack[sp++] = main_idx;
  if(init_idx >= 0 && init_idx != main_idx && !reach[init_idx]){
    reach[init_idx] = 1;
    stack[sp++] = init_idx;
  }
  while(sp > 0){
    int idx = stack[--sp];
    IRFunc* F = &m->fns[idx];
    for(int k=0;k<F->n;k++){
      IRIns* in = &F->ins[k];
      if((in->op == I_ADDRSYM || in->op == I_CALL) && in->name){
        int tidx = -1;
        for(int fi=0; fi<m->fn_n; fi++){
          if(finfo[fi].len == in->nlen && strncmp(finfo[fi].name, in->name, (size_t)in->nlen)==0){
            tidx = fi;
            break;
          }
        }
        if(tidx >= 0 && !finfo[tidx].is_extern && !reach[tidx]){
          reach[tidx] = 1;
          stack[sp++] = tidx;
        }
      }
    }
  }

  CallPatch cpatch[256];
  int cpatch_n = 0;

  for(int i=0;i<m->fn_n;i++){
    if(m->fns[i].is_extern) continue;
    if(!reach[i]) continue;
    size_t off = 0;
    if(lower_func_arm64(&m->fns[i], &cb, finfo, fcount, &off, NULL,
                        cpatch, &cpatch_n, (int)(sizeof(cpatch)/sizeof(cpatch[0])),
                        str_rva, str_n, ginfo, gcount, image_base, 0x1000) != 0){
      cb_free(&cb);
      free(finfo);
      free(reach);
      free(stack);
      return -1;
    }
    finfo[i].off = off;
    finfo[i].has_off = 1;
  }

  Arm64StubInfo si;
  memset(&si, 0, sizeof(si));
  build_win_arm64_stubs(&cb, image_base, imp, &si);
  map_stub(finfo, fcount, "tn_getstd", si.off_getstd);
  map_stub(finfo, fcount, "tn_write", si.off_write);
  map_stub(finfo, fcount, "say", si.off_say);
  map_stub(finfo, fcount, "say_str", si.off_say_str);
  map_stub(finfo, fcount, "say_f", si.off_say_f);
  map_stub(finfo, fcount, "say_multi", si.off_say_multi);
  map_stub(finfo, fcount, "len", si.off_len);
  map_stub(finfo, fcount, "strcmp", si.off_strcmp);
  map_stub(finfo, fcount, "tn_strcmp", si.off_tnstrcmp);
  map_stub(finfo, fcount, "memcpy", si.off_memcpy);
  map_stub(finfo, fcount, "memmove", si.off_memmove);
  map_stub(finfo, fcount, "memcmp", si.off_memcmp);
  map_stub(finfo, fcount, "time_now", si.off_time);
  map_stub(finfo, fcount, "time_now_ms", si.off_time_ms);
  map_stub(finfo, fcount, "time_now_ns", si.off_time_ns);
  map_stub(finfo, fcount, "malloc", si.off_malloc);
  map_stub(finfo, fcount, "free", si.off_free);
  map_stub(finfo, fcount, "fopen", si.off_fopen);
  map_stub(finfo, fcount, "fclose", si.off_fclose);
  map_stub(finfo, fcount, "fread", si.off_fread);
  map_stub(finfo, fcount, "fwrite", si.off_fwrite);
  map_stub(finfo, fcount, "fseek", si.off_fseek);
  map_stub(finfo, fcount, "ftell", si.off_ftell);
  map_stub(finfo, fcount, "fflush", si.off_fflush);
  map_stub(finfo, fcount, "io_err", si.off_io_err);
  map_stub(finfo, fcount, "io_eof", si.off_io_eof);
  map_stub(finfo, fcount, "read_line", si.off_read_line);
  map_stub(finfo, fcount, "read_bytes", si.off_read_bytes);
  map_stub(finfo, fcount, "write_line", si.off_write_line);
  map_stub(finfo, fcount, "stdin", si.off_stdin);
  map_stub(finfo, fcount, "stdout", si.off_stdout);
  map_stub(finfo, fcount, "stderr", si.off_stderr);
  map_stub(finfo, fcount, "get_cwd", si.off_get_cwd);

  free(reach);
  free(stack);

  for(int i=0;i<cpatch_n;i++){
    int tidx = cpatch[i].func_idx;
    if(tidx < 0 || tidx >= fcount || !finfo[tidx].has_off){
      cb_free(&cb);
      free(finfo);
      return -1;
    }
    size_t pos = cpatch[i].pos;
    size_t tgt = finfo[tidx].off;
    int64_t disp = ((int64_t)tgt - (int64_t)pos) / 4;
    if(disp < -(1<<25) || disp > ((1<<25)-1)){
      cb_free(&cb);
      free(finfo);
      return -1;
    }
    uint32_t ins = arm64_bl_imm((int)disp);
    put_u32_le(cb.data + pos, ins);
  }

  if(main_idx < 0 || !finfo[main_idx].has_off){
    cb_free(&cb);
    free(finfo);
    return -1;
  }
  *out_main_off = finfo[main_idx].off;
  *out_size = (uint32_t)cb.len;
  *out_text = cb.data;
  free(finfo);
  return 0;
}

typedef struct {
  size_t pos;
  int label;
  int cond;
  int is_cond;
} LabelPatch;

typedef struct {
  int reg;
  uint32_t off;
} AllocaMap;

static uint32_t find_alloca_off(AllocaMap* amap, int amap_n, int reg){
  for(int i=0;i<amap_n;i++){
    if(amap[i].reg == reg) return amap[i].off;
  }
  return 0;
}

static int cond_code(IRCmp c){
  switch(c){
    case C_EQ: return 0;
    case C_NEQ: return 1;
    case C_LT: return 11;
    case C_LTE: return 13;
    case C_GT: return 12;
    case C_GTE: return 10;
    default: return 0;
  }
}

static uint32_t reg_slot_off(int reg){ return (uint32_t)reg * 8u; }

static int lower_func_arm64(IRFunc* f, CodeBuf* cb, FuncInfo* finfo, int fcount,
                            size_t* out_off, size_t* out_entry,
                            CallPatch* cpatch, int* cpatch_n, int cpatch_cap,
                            const uint32_t* str_rva, int str_n,
                            GlobalInfo* ginfo, int gcount,
                            uint64_t image_base, uint32_t text_rva){
  if(!f || !cb) return -1;
  int nreg = (f->max_reg >= 0) ? (f->max_reg + 1) : f->next_reg;
  if(nreg <= 0) nreg = 1;

  // alloca map
  AllocaMap amap[256];
  int amap_n = 0;
  uint32_t off = (uint32_t)nreg * 8u;
  for(int i=0;i<f->n;i++){
    IRIns* in = &f->ins[i];
    if(in->op == I_ALLOCA){
      uint32_t align = (uint32_t)(in->align ? in->align : 8);
      if(align && (off % align)) off = (off + align - 1u) & ~(align - 1u);
      if(amap_n < (int)(sizeof(amap)/sizeof(amap[0]))){
        amap[amap_n].reg = in->a;
        amap[amap_n].off = off;
        amap_n++;
      }
      off += (uint32_t)in->size;
    }
  }
  uint32_t frame = off;
  if(frame & 15u) frame = (frame + 15u) & ~15u;

  // prologue
  size_t func_start = cb->len;
  if(out_off) *out_off = func_start;
  if(out_entry) *out_entry = func_start;
  cb_emit4(cb, 0xA9BF7BF0u); // stp x16,x17,[sp,#-16]! (scratch save)
  cb_emit4(cb, 0xA9BF7BFDu); // stp x29,x30,[sp,#-16]!
  cb_emit4(cb, 0x910003FDu); // mov x29, sp
  if(frame > 0){
    if(frame <= 0xFFFu){
      cb_emit4(cb, arm64_sub_imm(31, 31, frame));
    } else {
      emit_mov_imm64(cb, 16, frame);
      cb_emit4(cb, arm64_sub_reg(31, 31, 16));
    }
  }

  // store incoming args (x0..x7) into reg slots
  int preg[32];
  int pn = 0;
  for(int i=0;i<f->n && pn < f->param_count;i++){
    IRIns* pi = &f->ins[i];
    if(pi->op == I_LABEL) break;
    if(pi->op == I_STORE){
      preg[pn++] = pi->b;
    }
  }
  if(pn == 0){
    for(int i=0;i<f->param_count && i<8;i++){
      preg[pn++] = i;
    }
  }
  for(int i=0;i<pn && i<8;i++){
    emit_st_slot(cb, i, preg[i]);
  }

  // labels
  int max_label = f->next_label + 16;
  size_t* label_off = (size_t*)calloc((size_t)max_label, sizeof(size_t));
  unsigned char* label_set = (unsigned char*)calloc((size_t)max_label, 1);
  LabelPatch lpatch[256];
  int lpatch_n = 0;

  for(int i=0;i<f->n;i++){
    IRIns* in = &f->ins[i];
    switch(in->op){
      case I_LABEL:
        if(in->a >= 0 && in->a < max_label){
          label_off[in->a] = cb->len;
          label_set[in->a] = 1;
        }
        break;
      case I_ADDRSYM: {
        if(!in->name) return -1;
        int found = 0;
        for(int fi=0; fi<fcount; fi++){
          if(finfo[fi].name && finfo[fi].len == in->nlen &&
             strncmp(finfo[fi].name, in->name, (size_t)in->nlen)==0){
            if(finfo[fi].has_off){
              uint64_t addr = image_base + (uint64_t)text_rva + (uint64_t)finfo[fi].off;
              emit_mov_imm64(cb, 0, addr);
              emit_st_slot(cb, 0, in->a);
              found = 1;
            }
            break;
          }
        }
        if(!found && ginfo && gcount > 0){
          for(int gi=0; gi<gcount; gi++){
            if(ginfo[gi].name && ginfo[gi].len == in->nlen &&
               strncmp(ginfo[gi].name, in->name, (size_t)in->nlen)==0){
              if(!ginfo[gi].is_extern && ginfo[gi].rva){
                uint64_t addr = image_base + (uint64_t)ginfo[gi].rva;
                emit_mov_imm64(cb, 0, addr);
                emit_st_slot(cb, 0, in->a);
                found = 1;
              }
              break;
            }
          }
        }
        if(!found) return -1;
        break;
      }
      case I_SCONST:
        if(!str_rva || in->sid < 0 || in->sid >= str_n) return -1;
        emit_mov_imm64(cb, 0, image_base + (uint64_t)str_rva[in->sid]);
        emit_st_slot(cb, 0, in->a);
        break;
      case I_ICONST:
        emit_mov_imm64(cb, 0, (uint64_t)in->imm);
        emit_st_slot(cb, 0, in->a);
        break;
      case I_FCONST:
        emit_mov_imm64(cb, 0, (uint64_t)in->immf);
        emit_st_slot(cb, 0, in->a);
        break;
      case I_MOV:
        emit_ld_slot(cb, 0, in->b);
        emit_st_slot(cb, 0, in->a);
        break;
      case I_ALLOCA: {
        // store sp+offset into dst
        uint32_t o2 = find_alloca_off(amap, amap_n, in->a);
        if(o2 <= 0xFFFu){
          cb_emit4(cb, arm64_add_imm(0, 31, o2));
        } else {
          emit_mov_imm64(cb, 0, o2);
          cb_emit4(cb, arm64_add_reg(0, 31, 0));
        }
        emit_st_slot(cb, 0, in->a);
        break;
      }
      case I_LOAD:
        emit_ld_slot(cb, 0, in->b);
        if(in->size == 1){
          cb_emit4(cb, in->is_unsigned ? arm64_ldrb_imm(1, 0, 0) : arm64_ldrsb_x_imm(1, 0, 0));
        } else if(in->size == 2){
          cb_emit4(cb, in->is_unsigned ? arm64_ldrh_imm(1, 0, 0) : arm64_ldrsh_x_imm(1, 0, 0));
        } else if(in->size == 4){
          cb_emit4(cb, in->is_unsigned ? arm64_ldrw_imm(1, 0, 0) : arm64_ldrsw_imm(1, 0, 0));
        } else {
          cb_emit4(cb, arm64_ldr_reg(1, 0));
        }
        emit_st_slot(cb, 1, in->a);
        break;
      case I_STORE:
        emit_ld_slot(cb, 0, in->a);
        emit_ld_slot(cb, 1, in->b);
        if(in->size == 1){
          cb_emit4(cb, arm64_strb_imm(1, 0, 0));
        } else if(in->size == 2){
          cb_emit4(cb, arm64_strh_imm(1, 0, 0));
        } else if(in->size == 4){
          cb_emit4(cb, arm64_strw_imm(1, 0, 0));
        } else {
          cb_emit4(cb, arm64_str_reg(1, 0));
        }
        break;
      case I_GEP: {
        emit_ld_slot(cb, 0, in->b);
        if(in->imm >= 0 && in->imm <= 0xFFF){
          cb_emit4(cb, arm64_add_imm(0, 0, (uint32_t)in->imm));
        } else if(in->imm < 0 && in->imm >= -(long long)0xFFF){
          cb_emit4(cb, arm64_sub_imm(0, 0, (uint32_t)(-in->imm)));
        } else {
          emit_mov_imm64(cb, 1, (uint64_t)(int64_t)in->imm);
          cb_emit4(cb, arm64_add_reg(0, 0, 1));
        }
        emit_st_slot(cb, 0, in->a);
        break;
      }
      case I_BIN:
        emit_ld_slot(cb, 0, in->b);
        emit_ld_slot(cb, 1, in->c);
        if(in->binop == B_ADD) cb_emit4(cb, arm64_add_reg(0, 0, 1));
        else if(in->binop == B_SUB) cb_emit4(cb, arm64_sub_reg(0, 0, 1));
        else if(in->binop == B_MUL) cb_emit4(cb, arm64_mul_reg(0, 0, 1));
        else if(in->binop == B_DIV){
          cb_emit4(cb, arm64_sdiv_reg(2, 0, 1));
          cb_emit4(cb, arm64_add_imm(0, 2, 0));
        } else if(in->binop == B_MOD){
          cb_emit4(cb, arm64_sdiv_reg(2, 0, 1)); // q
          cb_emit4(cb, arm64_mul_reg(3, 2, 1));  // q*b
          cb_emit4(cb, arm64_sub_reg(0, 0, 3));  // a - q*b
        } else if(in->binop == B_AND) cb_emit4(cb, arm64_and_reg(0, 0, 1));
        else if(in->binop == B_OR) cb_emit4(cb, arm64_orr_reg(0, 0, 1));
        else if(in->binop == B_XOR) cb_emit4(cb, arm64_eor_reg(0, 0, 1));
        else if(in->binop == B_SHL) cb_emit4(cb, arm64_lslv_reg(0, 0, 1));
        else if(in->binop == B_SHR) cb_emit4(cb, arm64_asrv_reg(0, 0, 1));
        else return -1;
        emit_st_slot(cb, 0, in->a);
        break;
      case I_CMP: {
        emit_ld_slot(cb, 0, in->b);
        emit_ld_slot(cb, 1, in->c);
        cb_emit4(cb, arm64_cmp_reg(0, 1));
        cb_emit4(cb, arm64_cset(0, cond_code(in->cmpop)));
        emit_st_slot(cb, 0, in->a);
        break;
      }
      case I_FBIN:
        emit_ld_slot_fp(cb, 0, in->b);
        emit_ld_slot_fp(cb, 1, in->c);
        switch(in->fop){
          case F_ADD: cb_emit4(cb, arm64_fadd_dd(0, 0, 1)); break;
          case F_SUB: cb_emit4(cb, arm64_fsub_dd(0, 0, 1)); break;
          case F_MUL: cb_emit4(cb, arm64_fmul_dd(0, 0, 1)); break;
          case F_DIV: cb_emit4(cb, arm64_fdiv_dd(0, 0, 1)); break;
          default: return -1;
        }
        emit_st_slot_fp(cb, 0, in->a);
        break;
      case I_FCMP:
        emit_ld_slot_fp(cb, 0, in->b);
        emit_ld_slot_fp(cb, 1, in->c);
        cb_emit4(cb, arm64_fcmp_dd(0, 1));
        cb_emit4(cb, arm64_cset(0, cond_code(in->cmpop)));
        emit_st_slot(cb, 0, in->a);
        break;
      case I_I2F:
        emit_ld_slot(cb, 0, in->b);
        cb_emit4(cb, arm64_scvtf_dx(0, 0));
        emit_st_slot_fp(cb, 0, in->a);
        break;
      case I_F2I:
        emit_ld_slot_fp(cb, 0, in->b);
        cb_emit4(cb, arm64_fcvtzs_xd(0, 0));
        emit_st_slot(cb, 0, in->a);
        break;
      case I_JZ:
        emit_ld_slot(cb, 0, in->a);
        cb_emit4(cb, arm64_cmp_imm0(0));
        if(lpatch_n >= (int)(sizeof(lpatch)/sizeof(lpatch[0]))) return -1;
        lpatch[lpatch_n].pos = cb->len;
        lpatch[lpatch_n].label = in->b;
        lpatch[lpatch_n].cond = 0; // EQ
        lpatch[lpatch_n].is_cond = 1;
        lpatch_n++;
        cb_emit4(cb, arm64_bcond(0, 0)); // patched
        break;
      case I_JMP:
        if(lpatch_n >= (int)(sizeof(lpatch)/sizeof(lpatch[0]))) return -1;
        lpatch[lpatch_n].pos = cb->len;
        lpatch[lpatch_n].label = in->a;
        lpatch[lpatch_n].cond = 0;
        lpatch[lpatch_n].is_cond = 0;
        lpatch_n++;
        cb_emit4(cb, arm64_b_imm(0));
        break;
      case I_CALL: {
        if(!in->name) return -1;
        int tidx = -1;
        for(int k=0;k<fcount;k++){
          if(finfo[k].name && finfo[k].len==in->nlen &&
             strncmp(finfo[k].name, in->name, (size_t)in->nlen)==0){
            tidx = k;
            break;
          }
        }
        if(tidx < 0){
          fprintf(stderr, "buildexe: arm64 unresolved call '%.*s'\n", in->nlen, in->name);
          return -1;
        }
        // args -> x0..x7
        if(in->argc > 8){
          fprintf(stderr, "buildexe: arm64 call argc %d too large in %s\n",
                  in->argc, f->name ? f->name : "(anon)");
          return -1;
        }
        for(int a=0;a<in->argc && a<8;a++){
          emit_ld_slot(cb, a, in->args[a]);
        }
        if(*cpatch_n >= cpatch_cap) return -1;
        cpatch[*cpatch_n].pos = cb->len;
        cpatch[*cpatch_n].func_idx = tidx;
        (*cpatch_n)++;
        cb_emit4(cb, arm64_bl_imm(0));
        if(in->a >= 0){
          emit_st_slot(cb, 0, in->a);
        }
        break;
      }
      case I_CALLPTR: {
        if(in->argc > 8){
          fprintf(stderr, "buildexe: arm64 call argc %d too large in %s\n",
                  in->argc, f->name ? f->name : "(anon)");
          return -1;
        }
        for(int a=0;a<in->argc && a<8;a++){
          emit_ld_slot(cb, a, in->args[a]);
        }
        emit_ld_slot(cb, 16, in->b);
        cb_emit4(cb, arm64_blr(16));
        if(in->a >= 0){
          emit_st_slot(cb, 0, in->a);
        }
        break;
      }
      case I_RET:
        if(in->a >= 0){
          emit_ld_slot(cb, 0, in->a);
        } else {
          emit_mov_imm64(cb, 0, 0);
        }
        // epilogue
        if(frame > 0){
          if(frame <= 0xFFFu){
            cb_emit4(cb, arm64_add_imm(31, 31, frame));
          } else {
            emit_mov_imm64(cb, 16, frame);
            cb_emit4(cb, arm64_add_reg(31, 31, 16));
          }
        }
        cb_emit4(cb, 0xA8C17BFDu); // ldp x29,x30,[sp],#16
        cb_emit4(cb, 0xA8C17BF0u); // ldp x16,x17,[sp],#16
        cb_emit4(cb, arm64_ret());
        break;
      default:
        fprintf(stderr, "buildexe: arm64 unsupported op %d in %s\n",
                in->op, f->name ? f->name : "(anon)");
        return -1;
    }
  }

  // patch labels
  int patch_fail = 0;
  for(int i=0;i<lpatch_n;i++){
    int lab = lpatch[i].label;
    if(lab < 0 || lab >= max_label || !label_set[lab]) continue;
    size_t pos = lpatch[i].pos;
    int64_t target = (int64_t)label_off[lab];
    int64_t pc = (int64_t)pos;
    int64_t disp = (target - pc) / 4;
    if(lpatch[i].is_cond){
      if(disp < -(1<<18) || disp > ((1<<18)-1)){ patch_fail = 1; break; }
      uint32_t ins = arm64_bcond((int)disp, lpatch[i].cond);
      put_u32_le(cb->data + pos, ins);
    } else {
      if(disp < -(1<<25) || disp > ((1<<25)-1)){ patch_fail = 1; break; }
      uint32_t ins = arm64_b_imm((int)disp);
      put_u32_le(cb->data + pos, ins);
    }
  }
  free(label_off);
  free(label_set);
  if(patch_fail) return -1;
  return 0;
}

int ir_compile_to_elf_arm64_exe(IRModule* m, const char* out_exe){
  if(!m || !out_exe) return -1;
  const uint64_t base = 0x400000;
  const uint32_t text_rva = 0x1000;
  uint32_t rdata_rva = 0x2000;

  unsigned char* rdata = NULL;
  unsigned char* data = NULL;
  GlobalInfo* ginfo = NULL;
  uint32_t* str_rva = NULL;
  uint32_t rdata_size = 0;
  uint32_t data_size = 0;
  uint32_t data_rva = 0;
  uint64_t heap_cur_addr = 0;
  uint64_t heap_end_addr = 0;
  uint64_t heap_cur_var_addr = 0;
  uint64_t heap_end_var_addr = 0;
  uint64_t err_addr = 0;
  uint64_t stdin_addr = 0;
  uint64_t stdout_addr = 0;
  uint64_t stderr_addr = 0;

  CodeBuf cb;
  memset(&cb, 0, sizeof(cb));
  size_t entry_off = 0;

  for(int pass=0; pass<2; pass++){
    if(rdata){ free(rdata); rdata = NULL; }
    if(data){ free(data); data = NULL; }
    if(ginfo){ free(ginfo); ginfo = NULL; }
    if(str_rva){ free(str_rva); str_rva = NULL; }

    // rodata (strings)
    size_t cap = 256;
    rdata = (unsigned char*)malloc(cap);
    if(!rdata) return -1;
    memset(rdata, 0, cap);
    uint32_t off = 0;
    if(m->str_n > 0){
      str_rva = (uint32_t*)calloc((size_t)m->str_n, sizeof(uint32_t));
      if(!str_rva) return -1;
      for(int i=0;i<m->str_n;i++){
        uint32_t len = (uint32_t)m->strs[i].len + 1;
        uint32_t need = off + len;
        if(need > cap){
          size_t ncap = cap;
          while(ncap < need) ncap *= 2;
          rdata = (unsigned char*)realloc(rdata, ncap);
          if(!rdata) return -1;
          memset(rdata + cap, 0, ncap - cap);
          cap = ncap;
        }
        str_rva[i] = rdata_rva + off;
        memcpy(rdata + off, m->strs[i].bytes, len);
        off += len;
      }
    }
    rdata_size = off;

    // data (globals)
    data_rva = align_up_u32(rdata_rva + rdata_size, 16);
    int gcount = m->global_n;
    if(gcount > 0){
      ginfo = (GlobalInfo*)calloc((size_t)gcount, sizeof(GlobalInfo));
      size_t dcap = 256;
      data = (unsigned char*)malloc(dcap);
      if(!ginfo || !data) return -1;
      memset(data, 0, dcap);
      uint32_t doff = 0;
      for(int i=0;i<gcount;i++){
        IRGlobal* g = &m->globals[i];
        ginfo[i].name = g->name;
        ginfo[i].len = g->len;
        ginfo[i].is_extern = g->is_extern;
        if(g->is_extern){
          ginfo[i].rva = 0;
          continue;
        }
        int align = g->align ? g->align : 8;
        doff = align_up_u32(doff, (uint32_t)align);
        uint32_t need2 = doff + (uint32_t)g->size;
        if(need2 > dcap){
          size_t ncap = dcap;
          while(ncap < need2) ncap *= 2;
          data = (unsigned char*)realloc(data, ncap);
          if(!data) return -1;
          memset(data + dcap, 0, ncap - dcap);
          dcap = ncap;
        }
        ginfo[i].rva = data_rva + doff;
        if(g->has_init){
          if(g->ty && g->ty->k == TY_F64){
            uint64_t bits = (uint64_t)g->init_fbits;
            for(int b=0;b<g->size && b<8;b++){
              data[doff + (uint32_t)b] = (unsigned char)((bits >> (8*b)) & 0xFF);
            }
          } else {
            uint64_t val = (uint64_t)g->init_int;
            for(int b=0;b<g->size && b<8;b++){
              data[doff + (uint32_t)b] = (unsigned char)((val >> (8*b)) & 0xFF);
            }
          }
        }
        doff = need2;
      }
      data_size = doff;
    } else {
      data_size = 0;
    }

    // stdin/stdout/stderr FILE structs + err slot
    const uint32_t file_struct_size = 32;
    uint32_t file_base_off = align_up_u32(data_size, 8);
    uint32_t stdin_off = file_base_off;
    uint32_t stdout_off = stdin_off + file_struct_size;
    uint32_t stderr_off = stdout_off + file_struct_size;
    uint32_t err_off = stderr_off + file_struct_size;
    uint32_t file_total = err_off + 8;
    if(file_total > data_size){
      size_t ncap = data ? (size_t)data_size : 0;
      if(ncap < file_total) ncap = file_total;
      data = (unsigned char*)realloc(data, ncap);
      if(!data) return -1;
      if(data_size < file_total){
        memset(data + data_size, 0, ncap - data_size);
      }
      data_size = file_total;
    }
    stdin_addr = base + (uint64_t)data_rva + (uint64_t)stdin_off;
    stdout_addr = base + (uint64_t)data_rva + (uint64_t)stdout_off;
    stderr_addr = base + (uint64_t)data_rva + (uint64_t)stderr_off;
    err_addr = base + (uint64_t)data_rva + (uint64_t)err_off;
    // fd, eof, err, pushback(-1)
    put_u64_le(data + stdin_off + 0, 0);
    put_u64_le(data + stdin_off + 8, 0);
    put_u64_le(data + stdin_off + 16, 0);
    put_u64_le(data + stdin_off + 24, 0xFFFFFFFFFFFFFFFFULL);
    put_u64_le(data + stdout_off + 0, 1);
    put_u64_le(data + stdout_off + 8, 0);
    put_u64_le(data + stdout_off + 16, 0);
    put_u64_le(data + stdout_off + 24, 0xFFFFFFFFFFFFFFFFULL);
    put_u64_le(data + stderr_off + 0, 2);
    put_u64_le(data + stderr_off + 8, 0);
    put_u64_le(data + stderr_off + 16, 0);
    put_u64_le(data + stderr_off + 24, 0xFFFFFFFFFFFFFFFFULL);

    // heap region (bump allocator)
    const uint32_t heap_size = 1u << 20;
    uint32_t heap_base_off = align_up_u32(data_size, 16);
    uint32_t heap_cur_off = heap_base_off + heap_size;
    uint32_t heap_end_off = heap_cur_off + 8;
    uint32_t total_data = heap_end_off + 8;
    if(total_data > data_size){
      size_t ncap = data ? (size_t)data_size : 0;
      if(ncap < total_data) ncap = total_data;
      data = (unsigned char*)realloc(data, ncap);
      if(!data) return -1;
      memset(data + data_size, 0, ncap - data_size);
      data_size = total_data;
    }
    heap_cur_addr = base + (uint64_t)data_rva + (uint64_t)heap_base_off;
    heap_end_addr = heap_cur_addr + (uint64_t)heap_size;
    heap_cur_var_addr = base + (uint64_t)data_rva + (uint64_t)heap_cur_off;
    heap_end_var_addr = base + (uint64_t)data_rva + (uint64_t)heap_end_off;
    put_u64_le(data + heap_cur_off, heap_cur_addr);
    put_u64_le(data + heap_end_off, heap_end_addr);

    // codegen
    cb_init(&cb, 2048);
    static const char* arm64_builtins[] = {
      "say","say_str","say_f","say_multi","len","strcmp","tn_strcmp",
      "memcpy","memmove","memcmp","malloc","free",
      "time_now","time_now_ms","time_now_ns",
      "fopen","fclose","fread","fwrite","fseek","ftell","fflush",
      "io_err","io_eof","read_line","read_bytes","write_line",
      "stdin","stdout","stderr","get_cwd"
    };
    int builtin_n = (int)(sizeof(arm64_builtins)/sizeof(arm64_builtins[0]));
    int fcount = m->fn_n + builtin_n;
    FuncInfo* finfo = (FuncInfo*)calloc((size_t)fcount, sizeof(FuncInfo));
    if(!finfo){ cb_free(&cb); return -1; }
    int main_idx = -1;
    for(int i=0;i<m->fn_n;i++){
      finfo[i].name = m->fns[i].name;
      finfo[i].len = m->fns[i].len;
      finfo[i].is_extern = m->fns[i].is_extern;
      finfo[i].off = 0;
      finfo[i].has_off = 0;
      if(!m->fns[i].is_extern && m->fns[i].name){
        if((m->fns[i].len==4 && strncmp(m->fns[i].name, "main", 4)==0) ||
           (m->fns[i].len>5 && m->fns[i].name[m->fns[i].len-5]=='.' &&
            strncmp(m->fns[i].name + (m->fns[i].len-4), "main", 4)==0)){
          main_idx = i;
        }
      }
    }
    for(int i=0;i<builtin_n;i++){
      int idx = m->fn_n + i;
      finfo[idx].name = arm64_builtins[i];
      finfo[idx].len = (int)strlen(arm64_builtins[i]);
      finfo[idx].is_extern = 1;
      finfo[idx].off = 0;
      finfo[idx].has_off = 0;
    }

    // Reachability: only lower functions reachable from main
    unsigned char* reach = (unsigned char*)calloc((size_t)m->fn_n, 1);
    int* stack = (int*)malloc(sizeof(int)*(size_t)(m->fn_n > 0 ? m->fn_n : 1));
    int sp = 0;
    int init_idx = -1;
    if(main_idx < 0){
      fprintf(stderr, "buildexe: arm64 missing main\n");
      free(reach);
      free(stack);
      cb_free(&cb);
      free(finfo);
      return -1;
    }
    for(int i=0;i<m->fn_n;i++){
      if(finfo[i].is_extern) continue;
      if((finfo[i].len==9 && strncmp(finfo[i].name, "tezz_init", 9)==0) ||
         (finfo[i].len==11 && strncmp(finfo[i].name, "__tezz_init", 11)==0) ||
         (finfo[i].len>10 && finfo[i].name[finfo[i].len-10]=='.' &&
          strncmp(finfo[i].name + (finfo[i].len-9), "tezz_init", 9)==0) ||
         (finfo[i].len>12 && finfo[i].name[finfo[i].len-12]=='.' &&
          strncmp(finfo[i].name + (finfo[i].len-11), "__tezz_init", 11)==0)){
        init_idx = i;
        break;
      }
    }
    reach[main_idx] = 1;
    stack[sp++] = main_idx;
    if(init_idx >= 0 && init_idx != main_idx && !reach[init_idx]){
      reach[init_idx] = 1;
      stack[sp++] = init_idx;
    }
    while(sp > 0){
      int idx = stack[--sp];
      IRFunc* F = &m->fns[idx];
      for(int k=0;k<F->n;k++){
        IRIns* in = &F->ins[k];
        if((in->op == I_ADDRSYM || in->op == I_CALL) && in->name){
          int tidx = -1;
          for(int fi=0; fi<m->fn_n; fi++){
            if(finfo[fi].len == in->nlen && strncmp(finfo[fi].name, in->name, (size_t)in->nlen)==0){
              tidx = fi;
              break;
            }
          }
          if(tidx >= 0 && !finfo[tidx].is_extern && !reach[tidx]){
            reach[tidx] = 1;
            stack[sp++] = tidx;
          }
        }
      }
    }

    CallPatch cpatch[256];
    int cpatch_n = 0;

    for(int i=0;i<m->fn_n;i++){
      if(m->fns[i].is_extern) continue;
      if(!reach[i]) continue;
      size_t off2 = 0;
      if(lower_func_arm64(&m->fns[i], &cb, finfo, fcount, &off2, NULL,
                          cpatch, &cpatch_n, (int)(sizeof(cpatch)/sizeof(cpatch[0])),
                          str_rva, m->str_n, ginfo, gcount, base, text_rva) != 0){
        cb_free(&cb);
        free(finfo);
        free(reach);
        free(stack);
        fprintf(stderr, "buildexe: arm64 backend unsupported op in function %s\n",
                m->fns[i].name ? m->fns[i].name : "(anon)");
        return -1;
      }
      finfo[i].off = off2;
      finfo[i].has_off = 1;
    }

    // append Linux syscall stubs for externs
    Arm64StubInfo si;
    memset(&si, 0, sizeof(si));
    build_linux_arm64_stubs(&cb, &si, heap_cur_var_addr, heap_end_var_addr,
                            err_addr, stdin_addr, stdout_addr, stderr_addr);
    map_stub(finfo, fcount, "tn_exit", si.off_exit);
    map_stub(finfo, fcount, "tn_getstd", si.off_getstd);
    map_stub(finfo, fcount, "tn_write", si.off_write);
    map_stub(finfo, fcount, "tn_gettime", si.off_gettime);
    map_stub(finfo, fcount, "say", si.off_say);
    map_stub(finfo, fcount, "say_str", si.off_say_str);
    map_stub(finfo, fcount, "say_f", si.off_say_f);
    map_stub(finfo, fcount, "say_multi", si.off_say_multi);
    map_stub(finfo, fcount, "len", si.off_len);
    map_stub(finfo, fcount, "strcmp", si.off_strcmp);
    map_stub(finfo, fcount, "tn_strcmp", si.off_tnstrcmp);
    map_stub(finfo, fcount, "memcpy", si.off_memcpy);
    map_stub(finfo, fcount, "memmove", si.off_memmove);
    map_stub(finfo, fcount, "memcmp", si.off_memcmp);
    map_stub(finfo, fcount, "time_now", si.off_time);
    map_stub(finfo, fcount, "time_now_ms", si.off_time_ms);
    map_stub(finfo, fcount, "time_now_ns", si.off_time_ns);
    map_stub(finfo, fcount, "malloc", si.off_malloc);
    map_stub(finfo, fcount, "free", si.off_free);
    map_stub(finfo, fcount, "fopen", si.off_fopen);
    map_stub(finfo, fcount, "fclose", si.off_fclose);
    map_stub(finfo, fcount, "fread", si.off_fread);
    map_stub(finfo, fcount, "fwrite", si.off_fwrite);
    map_stub(finfo, fcount, "fseek", si.off_fseek);
    map_stub(finfo, fcount, "ftell", si.off_ftell);
    map_stub(finfo, fcount, "fflush", si.off_fflush);
    map_stub(finfo, fcount, "io_err", si.off_io_err);
    map_stub(finfo, fcount, "io_eof", si.off_io_eof);
    map_stub(finfo, fcount, "read_bytes", si.off_read_bytes);
    map_stub(finfo, fcount, "read_line", si.off_read_line);
    map_stub(finfo, fcount, "write_line", si.off_write_line);
    map_stub(finfo, fcount, "stdin", si.off_stdin);
    map_stub(finfo, fcount, "stdout", si.off_stdout);
    map_stub(finfo, fcount, "stderr", si.off_stderr);
    map_stub(finfo, fcount, "get_cwd", si.off_get_cwd);

    free(reach);
    free(stack);

    // patch calls
    for(int i=0;i<cpatch_n;i++){
      int tidx = cpatch[i].func_idx;
      if(tidx < 0 || tidx >= fcount || !finfo[tidx].has_off){
        char unresolved_name[128];
        const char* unresolved = "(anon)";
        if(tidx >= 0 && tidx < fcount && finfo[tidx].name){
          snprintf(unresolved_name, sizeof(unresolved_name), "%s", finfo[tidx].name);
          unresolved = unresolved_name;
        }
        cb_free(&cb);
        free(finfo);
        fprintf(stderr, "buildexe: arm64 unresolved extern %s\n",
                unresolved);
        return -1;
      }
      size_t pos = cpatch[i].pos;
      size_t tgt = finfo[tidx].off;
      int64_t disp = ((int64_t)tgt - (int64_t)pos) / 4;
      if(disp < -(1<<25) || disp > ((1<<25)-1)){
        cb_free(&cb);
        free(finfo);
        fprintf(stderr, "buildexe: arm64 call target out of range\n");
        return -1;
      }
      uint32_t ins = arm64_bl_imm((int)disp);
      put_u32_le(cb.data + pos, ins);
    }

    if(main_idx < 0 || finfo[main_idx].is_extern){
      cb_free(&cb);
      free(finfo);
      fprintf(stderr, "buildexe: arm64 missing main\n");
      return -1;
    }

    // entry stub: call main, then exit(code)
    entry_off = cb.len;
    int64_t disp = ((int64_t)finfo[main_idx].off - (int64_t)entry_off) / 4;
    if(disp < -(1<<25) || disp > ((1<<25)-1)){
      cb_free(&cb);
      free(finfo);
      fprintf(stderr, "buildexe: arm64 main out of range\n");
      return -1;
    }
    cb_emit4(&cb, arm64_bl_imm((int)disp));
    emit_mov_imm64(&cb, 8, 93);
    cb_emit4(&cb, arm64_svc_0());
    free(finfo);

    uint32_t need_rdata = align_up_u32(text_rva + (uint32_t)cb.len, 0x1000);
    if(need_rdata > rdata_rva && pass == 0){
      cb_free(&cb);
      rdata_rva = need_rdata;
      continue;
    }
    break;
  }

  const uint64_t entry = base + (uint64_t)text_rva + (uint64_t)entry_off;
  const uint32_t phoff = 64;
  const uint32_t file_align = 0x1000;
  const uint32_t code_off = 0x1000;
  const uint32_t code_size = (uint32_t)cb.len;
  const uint32_t rdata_off = align_up_u32(code_off + code_size, 0x1000);
  const uint32_t data_off = rdata_off + (data_rva - rdata_rva);
  const uint32_t seg2_size = (data_off - rdata_off) + data_size;
  const uint32_t file_size = rdata_off + seg2_size;

  FILE* f = fopen(out_exe, "wb");
  if(!f){ cb_free(&cb); return -1; }

  unsigned char ident[16];
  memset(ident, 0, sizeof(ident));
  ident[0] = 0x7F; ident[1] = 'E'; ident[2] = 'L'; ident[3] = 'F';
  ident[4] = 2; // 64-bit
  ident[5] = 1; // little endian
  ident[6] = 1; // version
  fwrite(ident, 1, sizeof(ident), f);
  write_u16(f, 2);       // ET_EXEC
  write_u16(f, 0xB7);    // EM_AARCH64
  write_u32(f, 1);       // version
  write_u64(f, entry);
  write_u64(f, phoff);
  write_u64(f, 0);
  write_u32(f, 0);
  write_u16(f, 64);
  write_u16(f, 56);
  write_u16(f, 2);
  write_u16(f, 0);
  write_u16(f, 0);
  write_u16(f, 0);

  // Program header 0: RX text
  write_u32(f, 1);             // PT_LOAD
  write_u32(f, 5);             // R | X
  write_u64(f, 0);             // p_offset
  write_u64(f, base);          // p_vaddr
  write_u64(f, base);          // p_paddr
  write_u64(f, code_off + code_size);
  write_u64(f, code_off + code_size);
  write_u64(f, file_align);

  // Program header 1: RW data/rodata
  write_u32(f, 1);             // PT_LOAD
  write_u32(f, 6);             // R | W
  write_u64(f, rdata_off);
  write_u64(f, base + (uint64_t)rdata_rva);
  write_u64(f, base + (uint64_t)rdata_rva);
  write_u64(f, seg2_size);
  write_u64(f, seg2_size);
  write_u64(f, file_align);

  long cur = ftell(f);
  while(cur < (long)code_off){ fputc(0, f); cur++; }
  fwrite(cb.data, 1, code_size, f);
  cur = ftell(f);
  while(cur < (long)rdata_off){ fputc(0, f); cur++; }
  if(rdata_size) fwrite(rdata, 1, rdata_size, f);
  cur = ftell(f);
  while(cur < (long)data_off){ fputc(0, f); cur++; }
  if(data_size) fwrite(data, 1, data_size, f);
  fclose(f);
  cb_free(&cb);
  if(rdata) free(rdata);
  if(data) free(data);
  if(ginfo) free(ginfo);
  if(str_rva) free(str_rva);
  (void)file_size;
  return 0;
}

int ir_compile_to_macho_arm64_exe(IRModule* m, const char* out_exe){
  if(!m || !out_exe) return -1;

  CodeBuf cb;
  cb_init(&cb, 2048);
  FuncInfo* finfo = (FuncInfo*)calloc((size_t)m->fn_n, sizeof(FuncInfo));
  if(!finfo){
    cb_free(&cb);
    return -1;
  }
  int fcount = m->fn_n;
  int main_idx = -1;
  for(int i=0;i<m->fn_n;i++){
    finfo[i].name = m->fns[i].name;
    finfo[i].len = m->fns[i].len;
    finfo[i].is_extern = m->fns[i].is_extern;
    finfo[i].off = 0;
    finfo[i].has_off = 0;
    if(!m->fns[i].is_extern && m->fns[i].name && strcmp(m->fns[i].name, "main") == 0){
      main_idx = i;
    }
  }

  CallPatch cpatch[256];
  int cpatch_n = 0;

  for(int i=0;i<m->fn_n;i++){
    if(m->fns[i].is_extern) continue;
    size_t off = 0;
    if(lower_func_arm64(&m->fns[i], &cb, finfo, fcount, &off, NULL,
                        cpatch, &cpatch_n, (int)(sizeof(cpatch)/sizeof(cpatch[0])),
                        NULL, 0, NULL, 0, 0, 0) != 0){
      cb_free(&cb);
      free(finfo);
      fprintf(stderr, "buildexe: arm64 backend unsupported op in function %s\n",
              m->fns[i].name ? m->fns[i].name : "(anon)");
      return -1;
    }
    finfo[i].off = off;
    finfo[i].has_off = 1;
  }

  // append macOS syscall stubs for externs
  Arm64StubInfo si;
  memset(&si, 0, sizeof(si));
  build_macos_arm64_stubs(&cb, &si);
  map_stub(finfo, fcount, "tn_exit", si.off_exit);
  map_stub(finfo, fcount, "tn_getstd", si.off_getstd);
  map_stub(finfo, fcount, "tn_write", si.off_write);
  map_stub(finfo, fcount, "tn_gettime", si.off_gettime);
  map_stub(finfo, fcount, "tn_valloc", si.off_valloc);
  map_stub(finfo, fcount, "tn_vfree", si.off_vfree);

  // patch calls
  for(int i=0;i<cpatch_n;i++){
    int tidx = cpatch[i].func_idx;
    if(tidx < 0 || tidx >= fcount || !finfo[tidx].has_off){
      char unresolved_name[128];
      const char* unresolved = "(anon)";
      if(tidx >= 0 && tidx < fcount && finfo[tidx].name){
        snprintf(unresolved_name, sizeof(unresolved_name), "%s", finfo[tidx].name);
        unresolved = unresolved_name;
      }
      cb_free(&cb);
      free(finfo);
      fprintf(stderr, "buildexe: arm64 unresolved extern %s\n",
              unresolved);
      return -1;
    }
    size_t pos = cpatch[i].pos;
    size_t tgt = finfo[tidx].off;
    int64_t disp = ((int64_t)tgt - (int64_t)pos) / 4;
    if(disp < -(1<<25) || disp > ((1<<25)-1)){
      cb_free(&cb);
      free(finfo);
      fprintf(stderr, "buildexe: arm64 call target out of range\n");
      return -1;
    }
    uint32_t ins = arm64_bl_imm((int)disp);
    put_u32_le(cb.data + pos, ins);
  }

  if(main_idx < 0 || !finfo[main_idx].has_off){
    cb_free(&cb);
    free(finfo);
    fprintf(stderr, "buildexe: arm64 missing main\n");
    return -1;
  }

  // entry stub: call main, then exit(code)
  size_t entry_off = cb.len;
  int64_t disp = ((int64_t)finfo[main_idx].off - (int64_t)entry_off) / 4;
  if(disp < -(1<<25) || disp > ((1<<25)-1)){
    cb_free(&cb);
    free(finfo);
    fprintf(stderr, "buildexe: arm64 main out of range\n");
    return -1;
  }
  cb_emit4(&cb, arm64_bl_imm((int)disp));
  // macOS exit syscall: x16 = 0x2000001, svc #0x80
  emit_mov_imm64(&cb, 16, 0x2000001ULL);
  cb_emit4(&cb, arm64_svc_imm(0x80));
  free(finfo);
  uint32_t code_size = (uint32_t)cb.len;

  const uint32_t MH_MAGIC_64 = 0xFEEDFACF;
  const uint32_t CPU_TYPE_ARM64 = 0x0100000C;
  const uint32_t CPU_SUBTYPE_ARM64_ALL = 0;
  const uint32_t MH_EXECUTE = 2;
  const uint32_t LC_SEGMENT_64 = 0x19;
  const uint32_t LC_MAIN = 0x80000028;

  const uint64_t base = 0x100000000ULL;
  const uint32_t file_align = 0x1000;

  const uint32_t sizeof_mach_header = 32;
  const uint32_t sizeof_segment = 72;
  const uint32_t sizeof_section = 80;
  const uint32_t sizeof_main = 24;

  const uint32_t sizeof_cmds = sizeof_segment + sizeof_section + sizeof_main;
  const uint32_t header_size = sizeof_mach_header + sizeof_cmds;
  const uint32_t code_off = (header_size + file_align - 1) & ~(file_align - 1);

  FILE* f = fopen(out_exe, "wb");
  if(!f){ cb_free(&cb); return -1; }

  write_u32(f, MH_MAGIC_64);
  write_u32(f, CPU_TYPE_ARM64);
  write_u32(f, CPU_SUBTYPE_ARM64_ALL);
  write_u32(f, MH_EXECUTE);
  write_u32(f, 2); // ncmds
  write_u32(f, sizeof_cmds);
  write_u32(f, 0);
  write_u32(f, 0);

  // LC_SEGMENT_64 __TEXT
  write_u32(f, LC_SEGMENT_64);
  write_u32(f, sizeof_segment + sizeof_section);
  char segname[16] = {0};
  memcpy(segname, "__TEXT", 6);
  fwrite(segname, 1, 16, f);
  write_u64(f, base);
  write_u64(f, (uint64_t)code_off + code_size);
  write_u64(f, 0);
  write_u64(f, (uint64_t)code_off + code_size);
  write_u32(f, 7);
  write_u32(f, 5);
  write_u32(f, 1);
  write_u32(f, 0);

  // __text section
  char sectname[16] = {0};
  memcpy(sectname, "__text", 6);
  fwrite(sectname, 1, 16, f);
  fwrite(segname, 1, 16, f);
  write_u64(f, base + code_off);
  write_u64(f, code_size);
  write_u32(f, code_off);
  write_u32(f, 4);
  write_u32(f, 0);
  write_u32(f, 0);
  write_u32(f, 0x80000400);
  write_u32(f, 0);
  write_u32(f, 0);
  write_u32(f, 0);

  // LC_MAIN
  write_u32(f, LC_MAIN);
  write_u32(f, sizeof_main);
  write_u64(f, code_off);
  write_u64(f, 0);

  long cur = ftell(f);
  while(cur < (long)code_off){
    fputc(0, f);
    cur++;
  }
  fwrite(cb.data, 1, code_size, f);
  fclose(f);
  cb_free(&cb);
  return 0;
}
