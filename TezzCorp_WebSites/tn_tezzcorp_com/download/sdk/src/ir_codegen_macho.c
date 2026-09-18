// src/ir_codegen_macho.c
#include "ir_codegen_macho.h"
#include "ir_codegen_pe.h"
#include "util.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static uint32_t align_up_u32(uint32_t v, uint32_t a){
  return (v + (a - 1)) & ~(a - 1);
}

static void write_u32(FILE* f, uint32_t v){ fwrite(&v, 1, 4, f); }
static void write_u64(FILE* f, uint64_t v){ fwrite(&v, 1, 8, f); }

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

static uint32_t read_u32_le(const unsigned char* p){
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static uint64_t read_u64_le(const unsigned char* p){
  uint64_t lo = (uint64_t)read_u32_le(p);
  uint64_t hi = (uint64_t)read_u32_le(p + 4);
  return lo | (hi << 32);
}

enum {
  IMP_EXIT = 0,
  IMP_GETSTD,
  IMP_WRITE,
  IMP_LSTRLEN,
  IMP_LSTRCMP,
  IMP_GETTIME,
  IMP_VALLOC,
  IMP_VFREE,
  IMP_WSPRINTF,
  IMP_SPRINTF,
  IMP_FOPEN,
  IMP_FCLOSE,
  IMP_FREAD,
  IMP_FWRITE,
  IMP_FSEEK,
  IMP_FTELL,
  IMP_FFLUSH,
  IMP_IO_ERR,
  IMP_IO_EOF,
  IMP_READ_LINE,
  IMP_READ_BYTES,
  IMP_WRITE_LINE,
  IMP_STDIN,
  IMP_STDOUT,
  IMP_STDERR,
  IMP_COUNT
};

static void emit1(unsigned char* buf, size_t* off, unsigned char v){ buf[(*off)++] = v; }
static void emit4(unsigned char* buf, size_t* off, uint32_t v){
  memcpy(buf + *off, &v, 4);
  *off += 4;
}
static void emit8(unsigned char* buf, size_t* off, uint64_t v){
  memcpy(buf + *off, &v, 8);
  *off += 8;
}
static void emit_bytes(unsigned char* buf, size_t* off, const unsigned char* data, size_t n){
  memcpy(buf + *off, data, n);
  *off += n;
}

typedef struct {
  size_t off_exit;
  size_t off_getstd;
  size_t off_write;
  size_t off_strlen;
  size_t off_strcmp;
  size_t off_gettime;
  size_t off_valloc;
  size_t off_vfree;
  size_t off_sprintf;
  size_t off_fopen;
  size_t off_fclose;
  size_t off_fread;
  size_t off_fwrite;
  size_t off_fseek;
  size_t off_ftell;
  size_t off_fflush;
  size_t off_io_err;
  size_t off_io_eof;
  size_t off_read_line;
  size_t off_read_bytes;
  size_t off_write_line;
  size_t off_stdin;
  size_t off_stdout;
  size_t off_stderr;
  size_t size;
} MachStubInfo;

static int build_macho_stubs(unsigned char* buf, size_t cap, uint32_t sys_write, uint32_t sys_exit,
                             uint64_t heap_cur_addr, uint64_t heap_end_addr, uint64_t heap_free_addr,
                             uint64_t err_addr, uint64_t stdin_addr, uint64_t stdout_addr, uint64_t stderr_addr,
                             MachStubInfo* out){
  if(!buf || !out) return -1;
  size_t o = 0;
  const uint32_t sys_read = sys_write - 1u;
  const uint32_t sys_open = sys_write + 1u;
  const uint32_t sys_close = sys_write + 2u;
  const uint32_t sys_lseek = 0x20000C7u;

  // tn_exit(code)
  out->off_exit = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xCF", 3);
  emit1(buf, &o, 0xB8); emit4(buf, &o, sys_exit);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05\xC3", 3);

  // tn_getstd(handle)
  out->off_getstd = o;
  emit1(buf, &o, 0xB8); emit4(buf, &o, 1);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xF9\xF4", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x75\x05", 2);
  emit1(buf, &o, 0xB8); emit4(buf, &o, 2);
  emit1(buf, &o, 0xC3);

  // tn_write(handle, buf, len, written*, overlapped)
  out->off_write = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x4D\x89\xCB", 3);
  emit1(buf, &o, 0xB8); emit4(buf, &o, sys_write);
  emit_bytes(buf, &o, (const unsigned char*)"\xBF\x01\x00\x00\x00", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xD6", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xC2", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x4D\x85\xDB", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x03", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3);
  emit1(buf, &o, 0xB8); emit4(buf, &o, 1);
  emit1(buf, &o, 0xC3);

  // tn_lstrlen
  out->off_strlen = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x80\x3C\x01\x00", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\xEB\xF5", 2);
  emit1(buf, &o, 0xC3);

  // tn_lstrcmp
  out->off_strcmp = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x44\x0F\xB6\x04\x01", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x44\x0F\xB6\x0C\x02", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x45\x39\xC8", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x75\x0A", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x45\x85\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x0C", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\xEB\xE7", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x44\x89\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x29\xC8", 3);
  emit1(buf, &o, 0xC3);
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);

  // tn_gettime -> gettimeofday
  out->off_gettime = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xC9", 3); // mov r9, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x20", 4); // sub rsp, 32
  emit1(buf, &o, 0xB8); emit4(buf, &o, 0x2000074u); // gettimeofday
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xE7", 3); // mov rdi, rsp
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xF6", 2); // xor esi, esi
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2); // syscall
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x04\x24", 4); // sec
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\xC1\x00\xCA\x9A\x3B", 7); // 1e9
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xF7\xE1", 3); // mul rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x8B\x44\x24\x08", 5); // usec
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x6B\xC0\x64", 4); // imul r8, 1000
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x01\xC0", 3); // add
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x01", 3); // store
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x20", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);

  // tn_valloc (free list + bump)
  out->off_valloc = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xC8", 3); // mov r8, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x83\xC0\x0F", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x83\xE0\xF0", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBA", 2); emit8(buf, &o, heap_cur_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, heap_end_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xB9", 2); emit8(buf, &o, heap_free_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x8B\x01", 3); // mov rax, [r9]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x31\xD2", 3); // xor rdx, rdx
  size_t loop = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2);
  size_t j_bump = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x08", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x39\xC1", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x82", 2);
  size_t j_next = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x48\x08", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xD2", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2);
  size_t j_sethead = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x4A\x08", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\xE9", 1); size_t j_got = o; emit4(buf, &o, 0);
  size_t sethead = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x09", 3);
  size_t got = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\x00", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8D\x40\x10", 4);
  emit1(buf, &o, 0xC3);
  size_t next = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xC2", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x40\x08", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\xE9", 1); size_t j_loop = o; emit4(buf, &o, 0);
  size_t bump = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x8B\x02", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xC1", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x01\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x3B\x03", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x87", 2);
  size_t j_fail = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x02", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\x01", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8D\x41\x10", 4);
  emit1(buf, &o, 0xC3);
  size_t fail_off = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  {
    int32_t d = (int32_t)(bump - (j_bump + 4));
    memcpy(buf + j_bump, &d, 4);
    d = (int32_t)(next - (j_next + 4));
    memcpy(buf + j_next, &d, 4);
    d = (int32_t)(sethead - (j_sethead + 4));
    memcpy(buf + j_sethead, &d, 4);
    d = (int32_t)(got - (j_got + 4));
    memcpy(buf + j_got, &d, 4);
    d = (int32_t)(loop - (j_loop + 4));
    memcpy(buf + j_loop, &d, 4);
    d = (int32_t)(fail_off - (j_fail + 4));
    memcpy(buf + j_fail, &d, 4);
  }

  // tn_vfree
  out->off_vfree = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x12", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xE9\x10", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xB9", 2); emit8(buf, &o, heap_free_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x8B\x01", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x41\x08", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x09", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);

  // tn_sprintf(buf, fmt, bits) -> fixed 6-digit float (no libc)
  out->off_sprintf = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x60", 4); // sub rsp, 96
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xC9", 3); // mov r9, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xC0", 3); // mov rax, r8
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x0F\xBA\xE0\x3F", 5); // bt r8, 63
  emit_bytes(buf, &o, (const unsigned char*)"\x72\x00", 2);
  size_t j_neg = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\xEB\x00", 2);
  size_t j_skip = o - 1;
  size_t neg_start = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x41\xC6\x01\x2D", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC1", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBA\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x7F", 10);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x21\xD0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xC0", 3);
  size_t neg_end = o;
  buf[j_neg] = (unsigned char)((int)neg_start - ((int)j_neg + 1));
  buf[j_skip] = (unsigned char)((int)neg_end - ((int)j_skip + 1));
  emit_bytes(buf, &o, (const unsigned char*)"\x66\x41\x0F\x6E\xC0", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\xF2\x48\x0F\x2C\xC0", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x04\x24", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x8D\x54\x24\x3F", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xF8\x00", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x75\x0B", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x41\xC6\x02\x30", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xCA", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\xEB\x12", 2);
  size_t int_loop = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xD2", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\xC1\x0A\x00\x00\x00", 7);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xF7\xF1", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x80\xC2\x30", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x88\x12", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xCA", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x75", 1); emit1(buf, &o, (unsigned char)((int)int_loop - ((int)o + 1)));
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC2", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x8D\x5C\x24\x3F", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xD9", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x29\xD1", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC1", 3);
  size_t copy1 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x8A\x12", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x88\x11", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC2", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC1", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC9", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x75", 1); emit1(buf, &o, (unsigned char)((int)copy1 - ((int)o + 1)));
  emit_bytes(buf, &o, (const unsigned char*)"\x41\xC6\x01\x2E", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC1", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x04\x24", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x66\x41\x0F\x6E\xC0", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\xF2\x48\x0F\x2A\xC8", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\xF2\x0F\x5C\xC1", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\xF2\x0F\x59\x05", 4); size_t mul_disp = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\xF2\x48\x0F\x2C\xC0", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x8D\x54\x24\x1F", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x41\xB3\x06", 3);
  size_t frac_loop = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xD2", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\xC1\x0A\x00\x00\x00", 7);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xF7\xF1", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x80\xC2\x30", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x88\x12", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xCA", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x41\xFE\xCB", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x75", 1); emit1(buf, &o, (unsigned char)((int)frac_loop - ((int)o + 1)));
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x8D\x54\x24\x1A", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\xB9\x06\x00\x00\x00", 5);
  size_t copy2 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x8A\x12", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x88\x11", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC2", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC1", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC9", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x75", 1); emit1(buf, &o, (unsigned char)((int)copy2 - ((int)o + 1)));
  emit_bytes(buf, &o, (const unsigned char*)"\x41\xC6\x01\x00", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x60", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  size_t const_pos = o;
  emit8(buf, &o, 0x412E848000000000ULL);
  {
    int32_t disp = (int32_t)(const_pos - (mul_disp + 4));
    memcpy(buf + mul_disp, &disp, 4);
  }

  // tn_io_err() -> i64
  out->off_io_err = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xA1", 2); emit8(buf, &o, err_addr); // mov rax, [err_addr]
  emit1(buf, &o, 0xC3);

  // tn_io_eof(f) -> i64
  out->off_io_eof = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x05", 2); // je ret1
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x41\x08", 4); // mov rax, [rcx+8]
  emit1(buf, &o, 0xC3);
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\x01\x00\x00\x00\xC3", 6);

  // tn_fopen(path, mode) -> FILE*
  out->off_fopen = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_fopen_null = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xD2", 3); // test rdx, rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_fopen_null2 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x8A\x02", 2); // mov al, [rdx]
  emit_bytes(buf, &o, (const unsigned char*)"\x3C\x72", 2); // cmp al, 'r'
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je mode_r
  size_t j_mode_r = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x3C\x77", 2); // cmp al, 'w'
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je mode_w
  size_t j_mode_w = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x3C\x61", 2); // cmp al, 'a'
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je mode_a
  size_t j_mode_a = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3); // xor eax,eax; ret
  size_t mode_r = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xF6", 2); // xor esi, esi (O_RDONLY)
  emit_bytes(buf, &o, (const unsigned char*)"\xEB\x00", 2); // jmp open
  size_t j_mode_open = o - 1;
  size_t mode_w = o;
  emit1(buf, &o, 0xBE); emit4(buf, &o, 0x601); // O_WRONLY|O_CREAT|O_TRUNC
  emit_bytes(buf, &o, (const unsigned char*)"\xEB\x00", 2); // jmp open
  size_t j_mode_open2 = o - 1;
  size_t mode_a = o;
  emit1(buf, &o, 0xBE); emit4(buf, &o, 0x209); // O_WRONLY|O_CREAT|O_APPEND
  size_t mode_open = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xCF", 3); // mov rdi, rcx
  emit1(buf, &o, 0xBA); emit4(buf, &o, 0x1A4); // mov edx, 0644
  emit1(buf, &o, 0xB8); emit4(buf, &o, sys_open); // open
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2); // syscall
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x82", 2); // jc err
  size_t j_fopen_err = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x30", 4); // sub rsp,48
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x44\x24\x20", 5); // [rsp+0x20]=fd
  emit_bytes(buf, &o, (const unsigned char*)"\xB9\x20\x00\x00\x00", 5); // mov ecx,32
  emit1(buf, &o, 0xE8); size_t call_valloc = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x54\x24\x20", 5); // mov rdx, [rsp+0x20]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x30", 4); // add rsp,48
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_fopen_ret0 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x10", 3); // [rax]=fd
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\x40\x08\x00\x00\x00\x00", 8); // eof=0
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\x40\x10\x00\x00\x00\x00", 8); // err=0
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\x40\x18\xFF\xFF\xFF\xFF", 8); // pushback=-1
  emit1(buf, &o, 0xC3);
  size_t fopen_err = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3); // err = rax
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3); // ret 0
  size_t fopen_ret0 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  buf[j_fopen_null] = (unsigned char)((int)fopen_ret0 - ((int)j_fopen_null + 1));
  buf[j_fopen_null2] = (unsigned char)((int)fopen_ret0 - ((int)j_fopen_null2 + 1));
  buf[j_mode_r] = (unsigned char)((int)mode_r - ((int)j_mode_r + 1));
  buf[j_mode_w] = (unsigned char)((int)mode_w - ((int)j_mode_w + 1));
  buf[j_mode_a] = (unsigned char)((int)mode_a - ((int)j_mode_a + 1));
  buf[j_mode_open] = (unsigned char)((int)mode_open - ((int)j_mode_open + 1));
  buf[j_mode_open2] = (unsigned char)((int)mode_open - ((int)j_mode_open2 + 1));
  buf[j_fopen_ret0] = (unsigned char)((int)fopen_ret0 - ((int)j_fopen_ret0 + 1));
  {
    int32_t disp = (int32_t)(fopen_err - (j_fopen_err + 4));
    memcpy(buf + j_fopen_err, &disp, 4);
    disp = (int32_t)(out->off_valloc - (call_valloc + 4));
    memcpy(buf + call_valloc, &disp, 4);
  }

  // tn_fclose(FILE*) -> int
  out->off_fclose = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je retm1
  size_t j_fclose_null = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xC9", 3); // mov r9, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xB8", 2); emit8(buf, &o, stdin_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x39\xC1", 3); // cmp rcx, r8
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_fclose_ret0_1 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xB8", 2); emit8(buf, &o, stdout_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x39\xC1", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2);
  size_t j_fclose_ret0_2 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xB8", 2); emit8(buf, &o, stderr_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x39\xC1", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2);
  size_t j_fclose_ret0_3 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x39", 3); // mov rdi,[rcx]
  emit1(buf, &o, 0xB8); emit4(buf, &o, sys_close);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x82", 2); // jc err
  size_t j_fclose_err = o; emit4(buf, &o, 0);
  size_t fclose_ok = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xC9", 3); // mov rcx, r9
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x20", 4); // sub rsp,32
  emit1(buf, &o, 0xE8); size_t call_vfree = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x20", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3); // ret 0
  size_t fclose_err = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6); // -1
  size_t fclose_ret0 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  size_t fclose_retm1 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  buf[j_fclose_null] = (unsigned char)((int)fclose_retm1 - ((int)j_fclose_null + 1));
  buf[j_fclose_ret0_1] = (unsigned char)((int)fclose_ret0 - ((int)j_fclose_ret0_1 + 1));
  buf[j_fclose_ret0_2] = (unsigned char)((int)fclose_ret0 - ((int)j_fclose_ret0_2 + 1));
  buf[j_fclose_ret0_3] = (unsigned char)((int)fclose_ret0 - ((int)j_fclose_ret0_3 + 1));
  {
    int32_t disp = (int32_t)(fclose_err - (j_fclose_err + 4));
    memcpy(buf + j_fclose_err, &disp, 4);
    disp = (int32_t)(out->off_vfree - (call_vfree + 4));
    memcpy(buf + call_vfree, &disp, 4);
  }
  (void)fclose_ok;

  // tn_fread(buf, size, count, f) -> i64
  out->off_fread = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x4D\x85\xC9", 3); // test r9, r9
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_fread_ret0a = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_fread_ret0b = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xD0", 3); // mov rax, rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x0F\xAF\xC0", 4); // imul rax, r8
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x7E\x00", 2); // jle ret0
  size_t j_fread_ret0c = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x8B\x39", 3); // mov rdi, [r9]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xCE", 3); // mov rsi, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xC2", 3); // mov rdx, rax
  emit1(buf, &o, 0xB8); emit4(buf, &o, sys_read);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x82", 2); // jc err
  size_t j_fread_err = o; emit4(buf, &o, 0);
  size_t fread_ok = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xF8\x00", 4); // cmp rax,0
  emit_bytes(buf, &o, (const unsigned char*)"\x75\x09", 2); // jne noeof
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xC7\x41\x08\x01\x00\x00\x00", 8); // eof=1
  emit_bytes(buf, &o, (const unsigned char*)"\xC3", 1);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xC7\x41\x08\x00\x00\x00\x00", 8); // eof=0
  emit1(buf, &o, 0xC3);
  size_t fread_err = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  size_t fread_ret0 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  {
    int32_t disp = (int32_t)(fread_err - (j_fread_err + 4));
    memcpy(buf + j_fread_err, &disp, 4);
  }
  buf[j_fread_ret0a] = (unsigned char)((int)fread_ret0 - ((int)j_fread_ret0a + 1));
  buf[j_fread_ret0b] = (unsigned char)((int)fread_ret0 - ((int)j_fread_ret0b + 1));
  buf[j_fread_ret0c] = (unsigned char)((int)fread_ret0 - ((int)j_fread_ret0c + 1));
  (void)fread_ok;

  // tn_fwrite(buf, size, count, f) -> i64
  out->off_fwrite = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x4D\x85\xC9", 3); // test r9, r9
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_fwrite_ret0a = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_fwrite_ret0b = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xD0", 3); // mov rax, rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x0F\xAF\xC0", 4); // imul rax, r8
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x7E\x00", 2); // jle ret0
  size_t j_fwrite_ret0c = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x8B\x39", 3); // mov rdi, [r9]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xCE", 3); // mov rsi, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xC2", 3); // mov rdx, rax
  emit1(buf, &o, 0xB8); emit4(buf, &o, sys_write);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x82", 2); // jc err
  size_t j_fwrite_err = o; emit4(buf, &o, 0);
  emit1(buf, &o, 0xC3);
  size_t fwrite_err = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  size_t fwrite_ret0 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  {
    int32_t disp = (int32_t)(fwrite_err - (j_fwrite_err + 4));
    memcpy(buf + j_fwrite_err, &disp, 4);
  }
  buf[j_fwrite_ret0a] = (unsigned char)((int)fwrite_ret0 - ((int)j_fwrite_ret0a + 1));
  buf[j_fwrite_ret0b] = (unsigned char)((int)fwrite_ret0 - ((int)j_fwrite_ret0b + 1));
  buf[j_fwrite_ret0c] = (unsigned char)((int)fwrite_ret0 - ((int)j_fwrite_ret0c + 1));

  // tn_fseek(f, off, whence) -> int
  out->off_fseek = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x03", 2); // je retm1
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xC9", 3); // mov r9, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x39", 3); // mov rdi, [rcx]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xD6", 3); // mov rsi, rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xC2", 3); // mov rdx, r8
  emit1(buf, &o, 0xB8); emit4(buf, &o, sys_lseek);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x82", 2); // jc err
  size_t j_fseek_err = o; emit4(buf, &o, 0);
  size_t fseek_ok = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xC7\x41\x08\x00\x00\x00\x00", 8); // eof=0
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3); // ret 0
  size_t fseek_err = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  size_t fseek_retm1 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  {
    int32_t disp = (int32_t)(fseek_err - (j_fseek_err + 4));
    memcpy(buf + j_fseek_err, &disp, 4);
  }
  (void)fseek_ok;
  (void)fseek_retm1;

  // tn_ftell(f) -> i64
  out->off_ftell = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x03", 2); // je retm1
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x39", 3); // mov rdi, [rcx]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x31\xF6", 3); // xor rsi, rsi
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\xC2\x01\x00\x00\x00", 7); // mov rdx,1
  emit1(buf, &o, 0xB8); emit4(buf, &o, sys_lseek);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x82", 2); // jc err
  size_t j_ftell_err = o; emit4(buf, &o, 0);
  emit1(buf, &o, 0xC3);
  size_t ftell_err = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  size_t ftell_retm1 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  {
    int32_t disp = (int32_t)(ftell_err - (j_ftell_err + 4));
    memcpy(buf + j_ftell_err, &disp, 4);
  }
  (void)ftell_retm1;

  // tn_fflush(f) -> 0
  out->off_fflush = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);

  // tn_io_err() -> i64
  out->off_io_err = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xA1", 2); emit8(buf, &o, err_addr); // mov rax, [err_addr]
  emit1(buf, &o, 0xC3);

  // tn_io_eof(f) -> i64
  out->off_io_eof = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x05", 2); // je ret1
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x41\x08", 4); // mov rax, [rcx+8]
  emit1(buf, &o, 0xC3);
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\x01\x00\x00\x00\xC3", 6);

  // tn_read_bytes(f, buf, n) -> i64
  out->off_read_bytes = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_rb_ret0a = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xD2", 3); // test rdx, rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_rb_ret0b = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x4D\x85\xC0", 3); // test r8, r8
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_rb_ret0c = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xC9", 3); // mov r9, rcx (save FILE*)
  emit_bytes(buf, &o, (const unsigned char*)"\x45\x31\xD2", 3); // xor r10d, r10d (total=0)
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x8B\x41\x18", 4); // mov rax, [r9+24] pushback
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xF8\xFF", 4); // cmp rax, -1
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je no_pb
  size_t j_rb_no_pb = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x88\x02", 2); // mov [rdx], al
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xC7\x41\x18\xFF\xFF\xFF\xFF", 8); // pushback=-1
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC2", 3); // inc r10
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC8", 3); // dec r8
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC2\x01", 4); // add rdx, 1
  emit_bytes(buf, &o, (const unsigned char*)"\x4D\x85\xC0", 3); // test r8, r8
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je done_total
  size_t j_rb_done_total = o - 1;
  size_t rb_no_pb = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x8B\x39", 3); // mov rdi, [r9]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xD6", 3); // mov rsi, rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xC2", 3); // mov rdx, r8
  emit1(buf, &o, 0xB8); emit4(buf, &o, sys_read);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x82", 2); // jc err
  size_t j_rb_err = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xF8\x00", 4); // cmp rax,0
  emit_bytes(buf, &o, (const unsigned char*)"\x75\x00", 2); // jne not_eof
  size_t j_rb_not_eof = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xC7\x41\x08\x01\x00\x00\x00", 8); // eof=1
  emit_bytes(buf, &o, (const unsigned char*)"\xEB\x00", 2); // jmp after_eof
  size_t j_rb_after_eof = o - 1;
  size_t rb_not_eof = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xC7\x41\x08\x00\x00\x00\x00", 8); // eof=0
  size_t rb_after_eof = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x01\xD0", 3); // add rax, r10
  emit1(buf, &o, 0xC3);
  size_t rb_err = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3); // err=rax
  emit_bytes(buf, &o, (const unsigned char*)"\x4D\x85\xD2", 3); // test r10, r10
  emit_bytes(buf, &o, (const unsigned char*)"\x75\x00", 2); // jne return_total
  size_t j_rb_return_total = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3); // ret0
  size_t rb_return_total = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xD0", 3); // mov rax, r10
  emit1(buf, &o, 0xC3);
  size_t rb_done_total = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xD0", 3);
  emit1(buf, &o, 0xC3);
  size_t rb_ret0 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  buf[j_rb_ret0a] = (unsigned char)((int)rb_ret0 - ((int)j_rb_ret0a + 1));
  buf[j_rb_ret0b] = (unsigned char)((int)rb_ret0 - ((int)j_rb_ret0b + 1));
  buf[j_rb_ret0c] = (unsigned char)((int)rb_ret0 - ((int)j_rb_ret0c + 1));
  buf[j_rb_no_pb] = (unsigned char)((int)rb_no_pb - ((int)j_rb_no_pb + 1));
  buf[j_rb_done_total] = (unsigned char)((int)rb_done_total - ((int)j_rb_done_total + 1));
  buf[j_rb_not_eof] = (unsigned char)((int)rb_not_eof - ((int)j_rb_not_eof + 1));
  buf[j_rb_after_eof] = (unsigned char)((int)rb_after_eof - ((int)j_rb_after_eof + 1));
  buf[j_rb_return_total] = (unsigned char)((int)rb_return_total - ((int)j_rb_return_total + 1));
  {
    int32_t disp = (int32_t)(rb_err - (j_rb_err + 4));
    memcpy(buf + j_rb_err, &disp, 4);
  }

  // tn_read_line(f) -> *u8
  out->off_read_line = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2); size_t j_rl_ret0 = o; emit4(buf, &o, 0); // je ret0
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x60", 4); // sub rsp,96
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x4C\x24\x48", 5); // [rsp+72]=rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\x44\x24\x20\x00\x00\x00\x00", 9); // buf=0
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\x44\x24\x28\x00\x00\x00\x00", 9); // len=0
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\x44\x24\x30\x80\x00\x00\x00", 9); // cap=128
  emit_bytes(buf, &o, (const unsigned char*)"\xB9\x81\x00\x00\x00", 5); // mov ecx,129
  emit1(buf, &o, 0xE8); size_t call_rl_valloc1 = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2); size_t j_rl_fail = o; emit4(buf, &o, 0); // je fail
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x44\x24\x20", 5); // buf=rax
  size_t rl_loop = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x4C\x24\x48", 5); // mov rcx,[rsp+72]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8D\x54\x24\x38", 5); // lea rdx,[rsp+56]
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xC7\xC0\x01\x00\x00\x00", 7); // mov r8,1
  emit1(buf, &o, 0xE8); size_t call_rl_read1 = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x8E", 2); size_t j_rl_done = o; emit4(buf, &o, 0); // jle done
  emit_bytes(buf, &o, (const unsigned char*)"\x8A\x44\x24\x38", 4); // mov al,[rsp+56]
  emit_bytes(buf, &o, (const unsigned char*)"\x3C\x0A", 2); // '\n'
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2); size_t j_rl_done2 = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x3C\x0D", 2); // '\r'
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x85", 2); size_t j_rl_append = o; emit4(buf, &o, 0);
  // handle CR
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x4C\x24\x48", 5); // mov rcx,[rsp+72]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8D\x54\x24\x38", 5); // lea rdx,[rsp+56]
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xC7\xC0\x01\x00\x00\x00", 7); // mov r8,1
  emit1(buf, &o, 0xE8); size_t call_rl_read2 = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xF8\x01", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x85", 2); size_t j_rl_done3 = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x8A\x44\x24\x38", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x3C\x0A", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2); size_t j_rl_done4 = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x4C\x24\x48", 5); // mov rcx,[rsp+72]
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\xB6\x44\x24\x38", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x41\x18", 4); // pushback
  emit_bytes(buf, &o, (const unsigned char*)"\xE9", 1); size_t j_rl_done5 = o; emit4(buf, &o, 0);
  size_t rl_append = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x44\x24\x28", 5); // len
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x54\x24\x30", 5); // cap
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8D\x40\x01", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x39\xD0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x82", 2); size_t j_rl_have = o; emit4(buf, &o, 0);
  // grow
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x01\xD2", 3); // cap*=2
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x54\x24\x30", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xD1", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC1", 3);
  emit1(buf, &o, 0xE8); size_t call_rl_valloc2 = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2); size_t j_rl_fail2 = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x44\x24\x40", 5); // newbuf
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xC1", 3); // dest
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x54\x24\x20", 5); // old buf
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x8B\x44\x24\x28", 5); // len
  emit_bytes(buf, &o, (const unsigned char*)"\x4D\x85\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); size_t j_rl_copy_done = o - 1;
  size_t rl_copy_loop = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x8A\x02", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x88\x01", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC1", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC2", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC8", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x75\x00", 2); size_t j_rl_copy_loop = o - 1;
  size_t rl_copy_done = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x4C\x24\x20", 5); // old buf
  emit1(buf, &o, 0xE8); size_t call_rl_vfree = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x44\x24\x40", 5); // newbuf
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x44\x24\x20", 5); // buf=newbuf
  size_t rl_have_space = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x4C\x24\x20", 5); // buf
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x54\x24\x28", 5); // len
  emit_bytes(buf, &o, (const unsigned char*)"\x8A\x44\x24\x38", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x88\x04\x11", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC2", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x54\x24\x28", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\xE9", 1); size_t j_rl_loop = o; emit4(buf, &o, 0);
  size_t rl_done = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x44\x24\x20", 5); // buf
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x54\x24\x28", 5); // len
  emit_bytes(buf, &o, (const unsigned char*)"\xC6\x04\x10\x00", 4); // buf[len]=0
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x44\x24\x20", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x60", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\xC3", 1);
  size_t rl_fail = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x4C\x24\x20", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); size_t j_rl_fail_ret = o - 1;
  emit1(buf, &o, 0xE8); size_t call_rl_vfree2 = o; emit4(buf, &o, 0);
  size_t rl_fail_ret = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x60", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\xC3", 1);
  size_t rl_ret0 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  {
    int32_t disp = (int32_t)(rl_ret0 - (j_rl_ret0 + 4));
    memcpy(buf + j_rl_ret0, &disp, 4);
    disp = (int32_t)(rl_fail - (j_rl_fail + 4));
    memcpy(buf + j_rl_fail, &disp, 4);
    disp = (int32_t)(rl_done - (j_rl_done + 4));
    memcpy(buf + j_rl_done, &disp, 4);
    disp = (int32_t)(rl_done - (j_rl_done2 + 4));
    memcpy(buf + j_rl_done2, &disp, 4);
    disp = (int32_t)(rl_append - (j_rl_append + 4));
    memcpy(buf + j_rl_append, &disp, 4);
    disp = (int32_t)(rl_done - (j_rl_done3 + 4));
    memcpy(buf + j_rl_done3, &disp, 4);
    disp = (int32_t)(rl_done - (j_rl_done4 + 4));
    memcpy(buf + j_rl_done4, &disp, 4);
    disp = (int32_t)(rl_done - (j_rl_done5 + 4));
    memcpy(buf + j_rl_done5, &disp, 4);
    disp = (int32_t)(rl_have_space - (j_rl_have + 4));
    memcpy(buf + j_rl_have, &disp, 4);
    disp = (int32_t)(rl_fail - (j_rl_fail2 + 4));
    memcpy(buf + j_rl_fail2, &disp, 4);
    disp = (int32_t)(rl_copy_done - (j_rl_copy_done + 1));
    buf[j_rl_copy_done] = (unsigned char)disp;
    disp = (int32_t)(rl_copy_loop - (j_rl_copy_loop + 1));
    buf[j_rl_copy_loop] = (unsigned char)disp;
    disp = (int32_t)(rl_loop - (j_rl_loop + 4));
    memcpy(buf + j_rl_loop, &disp, 4);
    disp = (int32_t)(rl_fail_ret - (j_rl_fail_ret + 1));
    buf[j_rl_fail_ret] = (unsigned char)disp;
    disp = (int32_t)(out->off_valloc - (call_rl_valloc1 + 4));
    memcpy(buf + call_rl_valloc1, &disp, 4);
    disp = (int32_t)(out->off_read_bytes - (call_rl_read1 + 4));
    memcpy(buf + call_rl_read1, &disp, 4);
    disp = (int32_t)(out->off_read_bytes - (call_rl_read2 + 4));
    memcpy(buf + call_rl_read2, &disp, 4);
    disp = (int32_t)(out->off_valloc - (call_rl_valloc2 + 4));
    memcpy(buf + call_rl_valloc2, &disp, 4);
    disp = (int32_t)(out->off_vfree - (call_rl_vfree + 4));
    memcpy(buf + call_rl_vfree, &disp, 4);
    disp = (int32_t)(out->off_vfree - (call_rl_vfree2 + 4));
    memcpy(buf + call_rl_vfree2, &disp, 4);
  }

  // tn_write_line(f, s) -> int
  out->off_write_line = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je retm1
  size_t j_wl_retm1 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xC9", 3); // mov r9, rcx (f)
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xD2", 3); // mov r10, rdx (s)
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0", 2); // xor eax, eax (len=0)
  emit_bytes(buf, &o, (const unsigned char*)"\x4D\x85\xD2", 3); // test r10, r10
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je len_done
  size_t j_wl_len_done = o - 1;
  size_t wl_len_loop = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x80\x3C\x02\x00", 5); // cmp byte [r10+rax],0
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je len_done
  size_t j_wl_len_done2 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC0", 3); // inc rax
  emit_bytes(buf, &o, (const unsigned char*)"\xEB\x00", 2); // jmp len_loop
  size_t j_wl_len_loop = o - 1;
  size_t wl_len_done = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je write_nl
  size_t j_wl_write_nl = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x8B\x39", 3); // mov rdi, [r9]
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xD6", 3); // mov rsi, r10
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xC2", 3); // mov rdx, rax
  emit1(buf, &o, 0xB8); emit4(buf, &o, sys_write); // sys_write
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x72\x00", 2); // jc err
  size_t j_wl_err1 = o - 1;
  size_t wl_write_nl = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x10", 4); // sub rsp,16
  emit_bytes(buf, &o, (const unsigned char*)"\xC6\x04\x24\x0A", 4); // '\n'
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x8B\x39", 3); // mov rdi, [r9]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8D\x34\x24", 4); // lea rsi, [rsp]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\xC2\x01\x00\x00\x00", 7); // len=1
  emit1(buf, &o, 0xB8); emit4(buf, &o, sys_write);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x10", 4); // add rsp,16
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x72\x00", 2); // jc err
  size_t j_wl_err2 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3); // ret 0
  size_t wl_err = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3); // err = rax
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  size_t wl_retm1 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  buf[j_wl_retm1] = (unsigned char)((int)wl_retm1 - ((int)j_wl_retm1 + 1));
  buf[j_wl_len_done] = (unsigned char)((int)wl_len_done - ((int)j_wl_len_done + 1));
  buf[j_wl_len_done2] = (unsigned char)((int)wl_len_done - ((int)j_wl_len_done2 + 1));
  buf[j_wl_len_loop] = (unsigned char)((int)wl_len_loop - ((int)j_wl_len_loop + 1));
  buf[j_wl_write_nl] = (unsigned char)((int)wl_write_nl - ((int)j_wl_write_nl + 1));
  buf[j_wl_err1] = (unsigned char)((int)wl_err - ((int)j_wl_err1 + 1));
  buf[j_wl_err2] = (unsigned char)((int)wl_err - ((int)j_wl_err2 + 1));

  // stdin/stdout/stderr
  out->off_stdin = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xB8", 2); emit8(buf, &o, stdin_addr);
  emit1(buf, &o, 0xC3);
  out->off_stdout = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xB8", 2); emit8(buf, &o, stdout_addr);
  emit1(buf, &o, 0xC3);
  out->off_stderr = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xB8", 2); emit8(buf, &o, stderr_addr);
  emit1(buf, &o, 0xC3);

  out->size = o;
  return (o <= cap) ? 0 : -1;
}

int ir_compile_to_macho_exe(IRModule* m, const char* out_exe){
  if(!m || !out_exe) return -1;

  const uint64_t base = 0x100000000ULL;
  const uint32_t text_rva = 0x1000;
  uint32_t rdata_rva = 0x2000;
  const uint32_t page = 0x1000;
  const uint32_t ptr_size = 8;

  unsigned char* text = NULL;
  uint32_t text_size = 0;
  size_t entry_off = 0;

  unsigned char* rdata = NULL;
  uint32_t rdata_size = 0;
  unsigned char* data = NULL;
  uint32_t data_size = 0;
  GlobalInfo* ginfo = NULL;
  uint32_t* str_rva = NULL;

  for(int pass=0; pass<2; pass++){
    free(rdata); rdata = NULL;
    free(data); data = NULL;
    free(ginfo); ginfo = NULL;
    free(str_rva); str_rva = NULL;
    free(text); text = NULL;
    rdata_size = 0;
    data_size = 0;

    // rdata buffer
    size_t cap = 4096;
    rdata = (unsigned char*)malloc(cap);
    if(!rdata) return -1;
    memset(rdata, 0, cap);
    uint32_t off = (uint32_t)(IMP_COUNT * ptr_size);

    const char* fmt = "%lld";
    const char* fmtf = "%g";
    const char* nl = "\n";
    const char* sp = " ";

    uint32_t fmt_rva = rdata_rva + off;
    uint32_t need = off + (uint32_t)strlen(fmt) + 1;
    if(need > cap){ cap = need * 2; rdata = (unsigned char*)realloc(rdata, cap); if(!rdata) return -1; }
    memcpy(rdata + off, fmt, strlen(fmt) + 1); off += (uint32_t)strlen(fmt) + 1;

    uint32_t fmtf_rva = rdata_rva + off;
    need = off + (uint32_t)strlen(fmtf) + 1;
    if(need > cap){ cap = need * 2; rdata = (unsigned char*)realloc(rdata, cap); if(!rdata) return -1; }
    memcpy(rdata + off, fmtf, strlen(fmtf) + 1); off += (uint32_t)strlen(fmtf) + 1;

    uint32_t nl_rva = rdata_rva + off;
    need = off + (uint32_t)strlen(nl) + 1;
    if(need > cap){ cap = need * 2; rdata = (unsigned char*)realloc(rdata, cap); if(!rdata) return -1; }
    memcpy(rdata + off, nl, strlen(nl) + 1); off += (uint32_t)strlen(nl) + 1;

    uint32_t sp_rva = rdata_rva + off;
    need = off + (uint32_t)strlen(sp) + 1;
    if(need > cap){ cap = need * 2; rdata = (unsigned char*)realloc(rdata, cap); if(!rdata) return -1; }
    memcpy(rdata + off, sp, strlen(sp) + 1); off += (uint32_t)strlen(sp) + 1;

    int str_n = m->str_n;
    if(str_n > 0){
      str_rva = (uint32_t*)calloc((size_t)str_n, sizeof(uint32_t));
      if(!str_rva) return -1;
      for(int i=0;i<str_n;i++){
        uint32_t len = (uint32_t)m->strs[i].len + 1;
        need = off + len;
        if(need > cap){ cap = need * 2; rdata = (unsigned char*)realloc(rdata, cap); if(!rdata) return -1; }
        str_rva[i] = rdata_rva + off;
        memcpy(rdata + off, m->strs[i].bytes, len);
        off += len;
      }
    }
    rdata_size = off;

    // data (globals)
    uint32_t data_rva = align_up_u32(rdata_rva + rdata_size, page);
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
    }

    // bump heap
    const uint32_t heap_size = 1u << 20;
    uint32_t heap_base_off = align_up_u32(data_size, 16);
    uint32_t heap_cur_off = heap_base_off + heap_size;
    uint32_t heap_end_off = heap_cur_off + 8;
    uint32_t heap_free_off = heap_end_off + 8;
    uint32_t total_data = heap_free_off + 8;
    const uint32_t file_struct_size = 32;
    uint32_t file_base_off = align_up_u32(total_data, 8);
    uint32_t stdin_off = file_base_off;
    uint32_t stdout_off = stdin_off + file_struct_size;
    uint32_t stderr_off = stdout_off + file_struct_size;
    uint32_t err_off = stderr_off + file_struct_size;
    total_data = err_off + 8;
    if(total_data > data_size){
      size_t ncap = (data ? data_size : 0);
      if(ncap < total_data) ncap = total_data;
      data = (unsigned char*)realloc(data, ncap);
      if(!data) return -1;
      memset(data + data_size, 0, ncap - data_size);
      data_size = total_data;
    }
    uint64_t heap_base_addr = base + (uint64_t)data_rva + (uint64_t)heap_base_off;
    uint64_t heap_end_val = heap_base_addr + heap_size;
    uint64_t heap_cur_addr = base + (uint64_t)data_rva + (uint64_t)heap_cur_off;
    uint64_t heap_end_addr = base + (uint64_t)data_rva + (uint64_t)heap_end_off;
    uint64_t heap_free_addr = base + (uint64_t)data_rva + (uint64_t)heap_free_off;
    uint64_t stdin_addr = base + (uint64_t)data_rva + (uint64_t)stdin_off;
    uint64_t stdout_addr = base + (uint64_t)data_rva + (uint64_t)stdout_off;
    uint64_t stderr_addr = base + (uint64_t)data_rva + (uint64_t)stderr_off;
    uint64_t err_addr = base + (uint64_t)data_rva + (uint64_t)err_off;
    put_u64_le(data + heap_cur_off, heap_base_addr);
    put_u64_le(data + heap_end_off, heap_end_val);
    put_u64_le(data + heap_free_off, 0);
    // stdin/stdout/stderr FILE structs: fd, eof, err, pushback(-1)
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
    put_u64_le(data + err_off, 0);

    PeImportInfo imp;
    memset(&imp, 0, sizeof(imp));
    imp.runtime_flags = PE_IMPORT_RUNTIME_TN_ALLOC;
    imp.import_size = IMP_COUNT * ptr_size;
    imp.iat_exit    = rdata_rva + (uint32_t)(IMP_EXIT * ptr_size);
    imp.iat_getstd  = rdata_rva + (uint32_t)(IMP_GETSTD * ptr_size);
    imp.iat_write   = rdata_rva + (uint32_t)(IMP_WRITE * ptr_size);
    imp.iat_lstrlen = rdata_rva + (uint32_t)(IMP_LSTRLEN * ptr_size);
    imp.iat_lstrcmp = rdata_rva + (uint32_t)(IMP_LSTRCMP * ptr_size);
    imp.iat_gettime = rdata_rva + (uint32_t)(IMP_GETTIME * ptr_size);
    imp.iat_valloc  = rdata_rva + (uint32_t)(IMP_VALLOC * ptr_size);
    imp.iat_vfree   = rdata_rva + (uint32_t)(IMP_VFREE * ptr_size);
    imp.iat_wsprintf= rdata_rva + (uint32_t)(IMP_WSPRINTF * ptr_size);
    imp.iat_sprintf = rdata_rva + (uint32_t)(IMP_SPRINTF * ptr_size);
    imp.iat_fopen   = rdata_rva + (uint32_t)(IMP_FOPEN * ptr_size);
    imp.iat_fclose  = rdata_rva + (uint32_t)(IMP_FCLOSE * ptr_size);
    imp.iat_fread   = rdata_rva + (uint32_t)(IMP_FREAD * ptr_size);
    imp.iat_fwrite  = rdata_rva + (uint32_t)(IMP_FWRITE * ptr_size);
    imp.iat_fseek   = rdata_rva + (uint32_t)(IMP_FSEEK * ptr_size);
    imp.iat_ftell   = rdata_rva + (uint32_t)(IMP_FTELL * ptr_size);
    imp.iat_fflush  = rdata_rva + (uint32_t)(IMP_FFLUSH * ptr_size);
    imp.iat_io_err  = rdata_rva + (uint32_t)(IMP_IO_ERR * ptr_size);
    imp.iat_io_eof  = rdata_rva + (uint32_t)(IMP_IO_EOF * ptr_size);
    imp.iat_read_line  = rdata_rva + (uint32_t)(IMP_READ_LINE * ptr_size);
    imp.iat_read_bytes = rdata_rva + (uint32_t)(IMP_READ_BYTES * ptr_size);
    imp.iat_write_line = rdata_rva + (uint32_t)(IMP_WRITE_LINE * ptr_size);
    imp.iat_stdin   = rdata_rva + (uint32_t)(IMP_STDIN * ptr_size);
    imp.iat_stdout  = rdata_rva + (uint32_t)(IMP_STDOUT * ptr_size);
    imp.iat_stderr  = rdata_rva + (uint32_t)(IMP_STDERR * ptr_size);

    int ok = ir_x64_codegen(m, text_rva, str_rva, str_n, fmt_rva, fmtf_rva, nl_rva, sp_rva,
                            &imp, ginfo, gcount, &text, &text_size, &entry_off);
    if(ok != 0) return -1;

    unsigned char stubs[4096];
    MachStubInfo si;
    memset(&si, 0, sizeof(si));
    if(build_macho_stubs(stubs, sizeof(stubs), 0x2000004u, 0x2000001u,
                         heap_cur_addr, heap_end_addr, heap_free_addr,
                         err_addr, stdin_addr, stdout_addr, stderr_addr,
                         &si) != 0){
      return -1;
    }
    size_t text_base = text_size;
    unsigned char* new_text = (unsigned char*)realloc(text, text_size + (uint32_t)si.size);
    if(!new_text) return -1;
    text = new_text;
    memcpy(text + text_size, stubs, si.size);
    text_size += (uint32_t)si.size;

    uint32_t need_rdata = align_up_u32(text_rva + text_size, page);
    if(need_rdata > rdata_rva && pass == 0){
      free(rdata); rdata = NULL;
      free(data); data = NULL;
      free(ginfo); ginfo = NULL;
      free(str_rva); str_rva = NULL;
      free(text); text = NULL;
      rdata_rva = need_rdata;
      continue;
    }

    uint64_t text_base_addr = base + (uint64_t)text_rva;
    put_u64_le(rdata + IMP_EXIT * ptr_size, text_base_addr + text_base + si.off_exit);
    put_u64_le(rdata + IMP_GETSTD * ptr_size, text_base_addr + text_base + si.off_getstd);
    put_u64_le(rdata + IMP_WRITE * ptr_size, text_base_addr + text_base + si.off_write);
    put_u64_le(rdata + IMP_LSTRLEN * ptr_size, text_base_addr + text_base + si.off_strlen);
    put_u64_le(rdata + IMP_LSTRCMP * ptr_size, text_base_addr + text_base + si.off_strcmp);
    put_u64_le(rdata + IMP_GETTIME * ptr_size, text_base_addr + text_base + si.off_gettime);
    put_u64_le(rdata + IMP_VALLOC * ptr_size, text_base_addr + text_base + si.off_valloc);
    put_u64_le(rdata + IMP_VFREE * ptr_size, text_base_addr + text_base + si.off_vfree);
    put_u64_le(rdata + IMP_WSPRINTF * ptr_size, text_base_addr + text_base + si.off_sprintf);
    put_u64_le(rdata + IMP_SPRINTF * ptr_size, text_base_addr + text_base + si.off_sprintf);
    put_u64_le(rdata + IMP_FOPEN * ptr_size, text_base_addr + text_base + si.off_fopen);
    put_u64_le(rdata + IMP_FCLOSE * ptr_size, text_base_addr + text_base + si.off_fclose);
    put_u64_le(rdata + IMP_FREAD * ptr_size, text_base_addr + text_base + si.off_fread);
    put_u64_le(rdata + IMP_FWRITE * ptr_size, text_base_addr + text_base + si.off_fwrite);
    put_u64_le(rdata + IMP_FSEEK * ptr_size, text_base_addr + text_base + si.off_fseek);
    put_u64_le(rdata + IMP_FTELL * ptr_size, text_base_addr + text_base + si.off_ftell);
    put_u64_le(rdata + IMP_FFLUSH * ptr_size, text_base_addr + text_base + si.off_fflush);
    put_u64_le(rdata + IMP_IO_ERR * ptr_size, text_base_addr + text_base + si.off_io_err);
    put_u64_le(rdata + IMP_IO_EOF * ptr_size, text_base_addr + text_base + si.off_io_eof);
    put_u64_le(rdata + IMP_READ_LINE * ptr_size, text_base_addr + text_base + si.off_read_line);
    put_u64_le(rdata + IMP_READ_BYTES * ptr_size, text_base_addr + text_base + si.off_read_bytes);
    put_u64_le(rdata + IMP_WRITE_LINE * ptr_size, text_base_addr + text_base + si.off_write_line);
    put_u64_le(rdata + IMP_STDIN * ptr_size, text_base_addr + text_base + si.off_stdin);
    put_u64_le(rdata + IMP_STDOUT * ptr_size, text_base_addr + text_base + si.off_stdout);
    put_u64_le(rdata + IMP_STDERR * ptr_size, text_base_addr + text_base + si.off_stderr);

    const uint32_t MH_MAGIC_64 = 0xFEEDFACF;
    const uint32_t CPU_TYPE_X86_64 = 0x01000007;
    const uint32_t CPU_SUBTYPE_X86_64_ALL = 3;
    const uint32_t MH_EXECUTE = 2;
    const uint32_t LC_SEGMENT_64 = 0x19;
    const uint32_t LC_MAIN = 0x80000028;

    const uint32_t sizeof_mach_header = 32;
    const uint32_t sizeof_segment = 72;
    const uint32_t sizeof_section = 80;
    const uint32_t sizeof_main = 24;

    const uint32_t sizeof_cmds = (sizeof_segment + sizeof_section) * 2 + sizeof_main;
    const uint32_t header_size = sizeof_mach_header + sizeof_cmds;

    uint32_t text_off = align_up_u32(header_size, page);
    uint32_t rdata_off = align_up_u32(text_off + text_size, page);
    uint64_t entry_off_file = (uint64_t)text_off + (uint64_t)entry_off;

    FILE* f = fopen(out_exe, "wb");
    if(!f) return -1;

    // Mach-O header
    write_u32(f, MH_MAGIC_64);
    write_u32(f, CPU_TYPE_X86_64);
    write_u32(f, CPU_SUBTYPE_X86_64_ALL);
    write_u32(f, MH_EXECUTE);
    write_u32(f, 3); // ncmds
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
    write_u64(f, (uint64_t)(text_off + text_size));
    write_u64(f, 0);
    write_u64(f, (uint64_t)(text_off + text_size));
    write_u32(f, 7); // maxprot
    write_u32(f, 5); // initprot
    write_u32(f, 1); // nsects
    write_u32(f, 0);

    // __text section
    char sectname[16] = {0};
    memcpy(sectname, "__text", 6);
    fwrite(sectname, 1, 16, f);
    fwrite(segname, 1, 16, f);
    write_u64(f, base + text_rva);
    write_u64(f, text_size);
    write_u32(f, text_off);
    write_u32(f, 4);
    write_u32(f, 0);
    write_u32(f, 0);
    write_u32(f, 0x80000400);
    write_u32(f, 0);
    write_u32(f, 0);
    write_u32(f, 0);

    // LC_SEGMENT_64 __DATA
    write_u32(f, LC_SEGMENT_64);
    write_u32(f, sizeof_segment + sizeof_section);
    char segname2[16] = {0};
    memcpy(segname2, "__DATA", 6);
    fwrite(segname2, 1, 16, f);
    write_u64(f, base + rdata_rva);
    write_u64(f, (uint64_t)(rdata_size + data_size));
    write_u64(f, rdata_off);
    write_u64(f, (uint64_t)(rdata_size + data_size));
    write_u32(f, 7);
    write_u32(f, 3);
    write_u32(f, 1);
    write_u32(f, 0);

    // __data section
    char sectname2[16] = {0};
    memcpy(sectname2, "__data", 6);
    fwrite(sectname2, 1, 16, f);
    fwrite(segname2, 1, 16, f);
    write_u64(f, base + rdata_rva);
    write_u64(f, (uint64_t)(rdata_size + data_size));
    write_u32(f, rdata_off);
    write_u32(f, 4);
    write_u32(f, 0);
    write_u32(f, 0);
    write_u32(f, 0);
    write_u32(f, 0);
    write_u32(f, 0);
    write_u32(f, 0);

    // LC_MAIN
    write_u32(f, LC_MAIN);
    write_u32(f, sizeof_main);
    write_u64(f, entry_off_file);
    write_u64(f, 0);

    // pad to text_off
    long cur = ftell(f);
    while(cur < (long)text_off){
      fputc(0, f);
      cur++;
    }
    fwrite(text, 1, text_size, f);

    // pad to rdata_off
    cur = ftell(f);
    while(cur < (long)rdata_off){
      fputc(0, f);
      cur++;
    }
    fwrite(rdata, 1, rdata_size, f);
    fwrite(data, 1, data_size, f);
    fclose(f);
    return 0;
  }

  return -1;
}

int ir_verify_macho_exe(const char* path){
  if(!path) return -1;
  FILE* f = fopen(path, "rb");
  if(!f) return -1;
  fseek(f, 0, SEEK_END);
  long fsz = ftell(f);
  if(fsz <= 0){
    fclose(f);
    return -1;
  }
  fseek(f, 0, SEEK_SET);
  unsigned char* buf = (unsigned char*)malloc((size_t)fsz);
  if(!buf){
    fclose(f);
    return -1;
  }
  size_t n = fread(buf, 1, (size_t)fsz, f);
  fclose(f);
  if(n != (size_t)fsz){
    free(buf);
    return -1;
  }
  if((size_t)fsz < 32){
    free(buf);
    return -1;
  }

  const uint32_t MH_MAGIC_64 = 0xFEEDFACF;
  const uint32_t CPU_TYPE_X86_64 = 0x01000007;
  const uint32_t MH_EXECUTE = 2;
  const uint32_t LC_SEGMENT_64 = 0x19;
  const uint32_t LC_MAIN = 0x80000028;
  const uint32_t VM_PROT_EXECUTE = 0x4;

  uint32_t magic = read_u32_le(buf);
  if(magic != MH_MAGIC_64){
    free(buf);
    return -1;
  }
  uint32_t cputype = read_u32_le(buf + 4);
  if(cputype != CPU_TYPE_X86_64){
    free(buf);
    return -1;
  }
  uint32_t filetype = read_u32_le(buf + 12);
  if(filetype != MH_EXECUTE){
    free(buf);
    return -1;
  }
  uint32_t ncmds = read_u32_le(buf + 16);
  uint32_t sizeofcmds = read_u32_le(buf + 20);
  if(ncmds == 0){
    free(buf);
    return -1;
  }
  if(sizeofcmds > (uint32_t)((size_t)fsz - 32)){
    free(buf);
    return -1;
  }

  size_t cmds_start = 32;
  size_t cmds_end = cmds_start + (size_t)sizeofcmds;
  if(cmds_end > (size_t)fsz){
    free(buf);
    return -1;
  }

  typedef struct {
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t initprot;
  } MachSeg;

  MachSeg* segs = (MachSeg*)calloc((size_t)ncmds, sizeof(MachSeg));
  int seg_n = 0;
  int has_text = 0;
  uint64_t text_fileoff = 0;
  uint64_t text_filesize = 0;
  uint32_t text_initprot = 0;

  int have_entry = 0;
  uint64_t entryoff = 0;

  size_t off = cmds_start;
  for(uint32_t i = 0; i < ncmds; i++){
    if(off + 8 > cmds_end){
      free(segs);
      free(buf);
      return -1;
    }
    uint32_t cmd = read_u32_le(buf + off);
    uint32_t cmdsize = read_u32_le(buf + off + 4);
    if(cmdsize < 8 || off + (size_t)cmdsize > cmds_end){
      free(segs);
      free(buf);
      return -1;
    }
    if(cmd == LC_SEGMENT_64){
      if(cmdsize < 72){
        free(segs);
        free(buf);
        return -1;
      }
      char segname[17];
      memset(segname, 0, sizeof(segname));
      memcpy(segname, buf + off + 8, 16);
      uint64_t fileoff = read_u64_le(buf + off + 40);
      uint64_t filesize = read_u64_le(buf + off + 48);
      uint32_t initprot = read_u32_le(buf + off + 60);
      if(fileoff > (uint64_t)fsz || filesize > (uint64_t)fsz - fileoff){
        free(segs);
        free(buf);
        return -1;
      }
      if(seg_n < (int)ncmds){
        segs[seg_n].fileoff = fileoff;
        segs[seg_n].filesize = filesize;
        segs[seg_n].initprot = initprot;
        seg_n++;
      }
      if(strncmp(segname, "__TEXT", 6) == 0){
        has_text = 1;
        text_fileoff = fileoff;
        text_filesize = filesize;
        text_initprot = initprot;
      }
    } else if(cmd == LC_MAIN){
      if(cmdsize < 24){
        free(segs);
        free(buf);
        return -1;
      }
      entryoff = read_u64_le(buf + off + 8);
      have_entry = 1;
    }
    off += cmdsize;
  }

  if(off != cmds_end){
    free(segs);
    free(buf);
    return -1;
  }
  if(!have_entry || entryoff >= (uint64_t)fsz){
    free(segs);
    free(buf);
    return -1;
  }

  int entry_ok = 0;
  if(has_text){
    if(text_filesize == 0){
      free(segs);
      free(buf);
      return -1;
    }
    if((text_initprot & VM_PROT_EXECUTE) != 0 &&
       entryoff >= text_fileoff &&
       entryoff < text_fileoff + text_filesize){
      entry_ok = 1;
    }
  }
  if(!entry_ok){
    for(int i = 0; i < seg_n; i++){
      if((segs[i].initprot & VM_PROT_EXECUTE) == 0) continue;
      if(entryoff >= segs[i].fileoff && entryoff < segs[i].fileoff + segs[i].filesize){
        entry_ok = 1;
        break;
      }
    }
  }

  free(segs);
  free(buf);
  return entry_ok ? 0 : -1;
}
