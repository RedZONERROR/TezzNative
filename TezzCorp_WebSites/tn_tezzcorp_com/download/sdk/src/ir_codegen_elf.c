// src/ir_codegen_elf.c
#include "ir_codegen_elf.h"
#include "ir_codegen_pe.h"
#include "util.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

static uint32_t align_up_u32(uint32_t v, uint32_t a){
  return (v + (a - 1)) & ~(a - 1);
}

static void write_u16(FILE* f, uint16_t v){ fwrite(&v, 1, 2, f); }
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

enum {
  IMP_EXIT = 0,
  IMP_GETSTD,
  IMP_WRITE,
  IMP_LSTRLEN,
  IMP_LSTRCMP,
  IMP_GETTIME,
  IMP_SLEEP,
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
  IMP_GETFILEATTR,
  IMP_CREATEDIR,
  IMP_REMOVEDIR,
  IMP_DELETEFILE,
  IMP_MOVEFILE,
  IMP_LISTDIR,
  IMP_PROCRUN,
  IMP_PROCOUT,
  IMP_NET_AF_INET,
  IMP_NET_AF_INET6,
  IMP_NET_SOCK_STREAM,
  IMP_NET_SOCK_DGRAM,
  IMP_NET_IPPROTO_TCP,
  IMP_NET_IPPROTO_UDP,
  IMP_NET_INIT,
  IMP_NET_CLEANUP,
  IMP_NET_LAST_ERROR,
  IMP_NET_SOCKET,
  IMP_NET_CLOSE,
  IMP_NET_CONNECT,
  IMP_NET_BIND,
  IMP_NET_LISTEN,
  IMP_NET_ACCEPT,
  IMP_NET_SEND,
  IMP_NET_RECV,
  IMP_NET_SET_BLOCKING,
  IMP_NET_SET_TIMEOUT,
  IMP_NET_RESOLVE,
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

static size_t emit_js32_placeholder(unsigned char* buf, size_t* off){
  emit_bytes(buf, off, (const unsigned char*)"\x0F\x88", 2);
  size_t pos = *off;
  emit4(buf, off, 0);
  return pos;
}

static void patch_rel32(unsigned char* buf, size_t pos, size_t target){
  int32_t disp = (int32_t)((int64_t)target - (int64_t)(pos + 4u));
  memcpy(buf + pos, &disp, 4);
}

static void emit_linux_errno_tail(unsigned char* buf, size_t* off, uint64_t err_addr){
  emit_bytes(buf, off, (const unsigned char*)"\x48\x89\xC2", 3); // mov rdx, rax
  emit_bytes(buf, off, (const unsigned char*)"\x48\xF7\xDA", 3); // neg rdx
  emit_bytes(buf, off, (const unsigned char*)"\x48\xB8", 2); emit8(buf, off, err_addr);
  emit_bytes(buf, off, (const unsigned char*)"\x48\x89\x10", 3); // mov [rax], rdx
  emit_bytes(buf, off, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6); // mov eax,-1; ret
}

static void emit_linux_syscall_return(unsigned char* buf, size_t* off, uint64_t err_addr, int zero_on_success){
  emit_bytes(buf, off, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  size_t j_fail = emit_js32_placeholder(buf, off);
  if(zero_on_success){
    emit_bytes(buf, off, (const unsigned char*)"\x31\xC0", 2); // xor eax, eax
  }
  emit1(buf, off, 0xC3);
  size_t fail = *off;
  emit_linux_errno_tail(buf, off, err_addr);
  patch_rel32(buf, j_fail, fail);
}

typedef struct {
  size_t off_exit;
  size_t off_getstd;
  size_t off_write;
  size_t off_strlen;
  size_t off_strcmp;
  size_t off_gettime;
  size_t off_sleep;
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
  size_t off_getfileattr;
  size_t off_createdir;
  size_t off_removedir;
  size_t off_deletefile;
  size_t off_movefile;
  size_t off_list_dir;
  size_t off_proc_run;
  size_t off_proc_out;
  size_t off_net_af_inet;
  size_t off_net_af_inet6;
  size_t off_net_sock_stream;
  size_t off_net_sock_dgram;
  size_t off_net_ipproto_tcp;
  size_t off_net_ipproto_udp;
  size_t off_net_init;
  size_t off_net_cleanup;
  size_t off_net_last_error;
  size_t off_net_socket;
  size_t off_net_close;
  size_t off_net_parse_ipv4;
  size_t off_net_connect;
  size_t off_net_bind;
  size_t off_net_listen;
  size_t off_net_accept;
  size_t off_net_send;
  size_t off_net_recv;
  size_t off_net_set_blocking;
  size_t off_net_set_timeout;
  size_t off_net_resolve;
  size_t size;
} ElfStubInfo;

static int build_linux_stubs(unsigned char* buf, size_t cap, uint32_t sys_write, uint32_t sys_exit,
                             uint64_t heap_cur_addr, uint64_t heap_end_addr, uint64_t heap_free_addr,
                             uint64_t err_addr, uint64_t stdin_addr, uint64_t stdout_addr, uint64_t stderr_addr,
                             ElfStubInfo* out){
  if(!buf || !out) return -1;
  size_t o = 0;

  // tn_exit(code) - Windows ABI (rcx)
  out->off_exit = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xCF", 3); // mov rdi, rcx
  emit1(buf, &o, 0xB8); emit4(buf, &o, sys_exit);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05\xC3", 3); // syscall; ret

  // tn_getstd(handle) -> fd
  out->off_getstd = o;
  emit1(buf, &o, 0xB8); emit4(buf, &o, 1); // mov eax,1
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xF9\xF4", 4); // cmp rcx, -12
  emit_bytes(buf, &o, (const unsigned char*)"\x75\x05", 2); // jne +5
  emit1(buf, &o, 0xB8); emit4(buf, &o, 2); // mov eax,2
  emit1(buf, &o, 0xC3);

  // tn_write(handle, buf, len, written*, overlapped) -> 1
  out->off_write = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x4D\x89\xCA", 3); // mov r10, r9
  emit1(buf, &o, 0xB8); emit4(buf, &o, sys_write); // mov eax, sys_write
  emit_bytes(buf, &o, (const unsigned char*)"\x89\xCF", 2); // mov edi, ecx (fd)
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xD6", 3); // mov rsi, rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xC2", 3); // mov rdx, r8
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2); // syscall
  emit_bytes(buf, &o, (const unsigned char*)"\x4D\x85\xD2", 3); // test r10, r10
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x03", 2); // je +3
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x02", 3); // mov [r10], rax
  emit1(buf, &o, 0xB8); emit4(buf, &o, 1); // mov eax,1
  emit1(buf, &o, 0xC3);

  // tn_lstrlen(s) -> len
  out->off_strlen = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0", 2); // xor eax,eax
  emit_bytes(buf, &o, (const unsigned char*)"\x80\x3C\x01\x00", 4); // cmp byte [rcx+rax],0
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x05", 2); // je +5
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC0", 3); // inc rax
  emit_bytes(buf, &o, (const unsigned char*)"\xEB\xF5", 2); // jmp -11
  emit1(buf, &o, 0xC3);

  // tn_lstrcmp(a, b) -> int
  out->off_strcmp = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0", 2); // xor eax,eax
  emit_bytes(buf, &o, (const unsigned char*)"\x44\x0F\xB6\x04\x01", 5); // movzx r8d, byte [rcx+rax]
  emit_bytes(buf, &o, (const unsigned char*)"\x44\x0F\xB6\x0C\x02", 5); // movzx r9d, byte [rdx+rax]
  emit_bytes(buf, &o, (const unsigned char*)"\x45\x39\xC8", 3); // cmp r8d, r9d
  emit_bytes(buf, &o, (const unsigned char*)"\x75\x0A", 2); // jne diff
  emit_bytes(buf, &o, (const unsigned char*)"\x45\x85\xC0", 3); // test r8d, r8d
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x0C", 2); // je equal
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC0", 3); // inc rax
  emit_bytes(buf, &o, (const unsigned char*)"\xEB\xE7", 2); // jmp loop
  emit_bytes(buf, &o, (const unsigned char*)"\x44\x89\xC0", 3); // mov eax, r8d
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x29\xC8", 3); // sub eax, r9d
  emit1(buf, &o, 0xC3);
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3); // xor eax,eax; ret

  // tn_gettime(&out) -> clock_gettime(CLOCK_REALTIME)
  out->off_gettime = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xC9", 3); // mov r9, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x20", 4); // sub rsp, 32
  emit1(buf, &o, 0xB8); emit4(buf, &o, 228); // mov eax, clock_gettime
  emit_bytes(buf, &o, (const unsigned char*)"\xBF\x00\x00\x00\x00", 5); // mov edi, 0
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xE6", 3); // mov rsi, rsp
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2); // syscall
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x04\x24", 4); // mov rax, [rsp] sec
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\xC1\x00\xCA\x9A\x3B", 7); // mov rcx, 1e9
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xF7\xE1", 3); // mul rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x8B\x44\x24\x08", 5); // mov r8, [rsp+8] nsec
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x01\xC0", 3); // add rax, r8
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x01", 3); // mov [r9], rax
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x20", 4); // add rsp, 32
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3); // xor eax,eax; ret

  // tn_sleep_ms(ms) -> nanosleep({ms/1000, (ms%1000)*1e6}, NULL)
  out->off_sleep = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x7E\x00", 2); // jle done
  size_t j_sleep_done = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x20", 4); // sub rsp, 32
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xC8", 3); // mov rax, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x31\xD2", 3); // xor rdx, rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xC7\xC0\xE8\x03\x00\x00", 7); // mov r8, 1000
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xF7\xF0", 3); // div r8
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x04\x24", 4); // mov [rsp], rax
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x69\xD2\x40\x42\x0F\x00", 7); // imul rdx, rdx, 1e6
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x54\x24\x08", 5); // mov [rsp+8], rdx
  emit1(buf, &o, 0xB8); emit4(buf, &o, 35); // mov eax, nanosleep
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xE7", 3); // mov rdi, rsp
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xF6", 2); // xor esi, esi
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2); // syscall
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x20", 4); // add rsp, 32
  size_t sleep_done = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3); // xor eax,eax; ret
  buf[j_sleep_done] = (unsigned char)((int)sleep_done - ((int)j_sleep_done + 1));

  // tn_valloc(size) -> free list + bump allocator
  out->off_valloc = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xC8", 3); // mov r8, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x83\xC0\x1F", 4); // +16 header, align
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x83\xE0\xF0", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBA", 2); emit8(buf, &o, heap_cur_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, heap_end_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xB9", 2); emit8(buf, &o, heap_free_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x8B\x01", 3); // mov rax, [r9] (cur)
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x31\xD2", 3); // xor rdx, rdx (prev)
  size_t loop = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2);
  size_t j_bump = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x08", 3); // mov rcx, [rax] size
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x39\xC1", 3); // cmp rcx, r8
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x82", 2);
  size_t j_next = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x48\x08", 4); // mov rcx, [rax+8] next
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xD2", 3); // test rdx, rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2);
  size_t j_sethead = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x4A\x08", 4); // mov [rdx+8], rcx
  emit_bytes(buf, &o, (const unsigned char*)"\xE9", 1); size_t j_got = o; emit4(buf, &o, 0);
  size_t sethead = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x09", 3); // mov [r9], rcx
  size_t got = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\x00", 3); // mov [rax], r8
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8D\x40\x10", 4); // lea rax, [rax+16]
  emit1(buf, &o, 0xC3);
  size_t next = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xC2", 3); // mov rdx, rax (prev=cur)
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x40\x08", 4); // mov rax, [rax+8]
  emit_bytes(buf, &o, (const unsigned char*)"\xE9", 1); size_t j_loop = o; emit4(buf, &o, 0);
  size_t bump = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x8B\x02", 3); // mov rax, [r10]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xC1", 3); // mov rcx, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x01\xC0", 3); // add rax, r8
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x3B\x03", 3); // cmp rax, [r11]
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x87", 2);
  size_t j_fail = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x02", 3); // mov [r10], rax
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\x01", 3); // mov [rcx], r8
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8D\x41\x10", 4); // lea rax, [rcx+16]
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

  // tn_vfree(ptr) -> 0 (push to free list)
  out->off_vfree = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x12", 2); // je done
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xE9\x10", 4); // sub rcx, 16
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xB9", 2); emit8(buf, &o, heap_free_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x8B\x01", 3); // mov rax, [r9]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x41\x08", 4); // mov [rcx+8], rax
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x09", 3); // mov [r9], rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);

  // tn_sprintf(buf, fmt, bits) -> fixed 6-digit float (no libc)
  out->off_sprintf = o;
  // prologue
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x60", 4); // sub rsp, 96
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xC9", 3); // mov r9, rcx (buf)
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xC0", 3); // mov rax, r8 (bits)
  // sign
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x0F\xBA\xE0\x3F", 5); // bt r8, 63
  emit_bytes(buf, &o, (const unsigned char*)"\x72\x00", 2); // jc rel8
  size_t j_neg = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\xEB\x00", 2); // jmp rel8
  size_t j_skip = o - 1;
  size_t neg_start = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x41\xC6\x01\x2D", 4); // mov byte [r9], '-'
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC1", 3); // inc r9
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBA\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x7F", 10); // mov r10, 0x7fff...
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x21\xD0", 3); // and rax, r10
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xC0", 3); // mov r8, rax
  size_t neg_end = o;
  buf[j_neg] = (unsigned char)((int)neg_start - ((int)j_neg + 1));
  buf[j_skip] = (unsigned char)((int)neg_end - ((int)j_skip + 1));
  // int part
  // movq xmm0, r8 (needs REX.W + REX.B to preserve full 64-bit payload)
  emit_bytes(buf, &o, (const unsigned char*)"\x66\x49\x0F\x6E\xC0", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\xF2\x48\x0F\x2C\xC0", 5); // cvttsd2si rax, xmm0
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x04\x24", 4); // mov [rsp], rax
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x8D\x54\x24\x3F", 5); // lea r10, [rsp+63]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xF8\x00", 4); // cmp rax, 0
  // if int part != 0 jump to int_loop body (9-byte zero-case fast path)
  emit_bytes(buf, &o, (const unsigned char*)"\x75\x09", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x41\xC6\x02\x30", 4); // mov byte [r10], '0'
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xCA", 3); // dec r10
  // skip full int_loop body and land at int-done copy path (26-byte jump)
  emit_bytes(buf, &o, (const unsigned char*)"\xEB\x1A", 2);
  // int loop
  size_t int_loop = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xD2", 2); // xor edx, edx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\xC1\x0A\x00\x00\x00", 7); // mov rcx, 10
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xF7\xF1", 3); // div rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x80\xC2\x30", 3); // add dl, '0'
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x88\x12", 3); // mov [r10], dl
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xCA", 3); // dec r10
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x75", 1); emit1(buf, &o, (unsigned char)((int)int_loop - ((int)o + 1))); // jne int_loop
  // int done
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC2", 3); // inc r10
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x8D\x5C\x24\x3F", 5); // lea r11, [rsp+63]
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xD9", 3); // mov rcx, r11
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x29\xD1", 3); // sub rcx, r10
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC1", 3); // inc rcx
  size_t copy1 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x8A\x12", 3); // mov dl, [r10]
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x88\x11", 3); // mov [r9], dl
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC2", 3); // inc r10
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC1", 3); // inc r9
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC9", 3); // dec rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x75", 1); emit1(buf, &o, (unsigned char)((int)copy1 - ((int)o + 1)));
  emit_bytes(buf, &o, (const unsigned char*)"\x41\xC6\x01\x2E", 4); // '.'
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC1", 3);
  // frac
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x04\x24", 4); // mov rax, [rsp]
  // movq xmm0, r8 (needs REX.W + REX.B to preserve full 64-bit payload)
  emit_bytes(buf, &o, (const unsigned char*)"\x66\x49\x0F\x6E\xC0", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\xF2\x48\x0F\x2A\xC8", 5); // cvtsi2sd xmm1, rax
  emit_bytes(buf, &o, (const unsigned char*)"\xF2\x0F\x5C\xC1", 4); // subsd xmm0, xmm1
  emit_bytes(buf, &o, (const unsigned char*)"\xF2\x0F\x59\x05", 4); size_t mul_disp = o; emit4(buf, &o, 0); // mulsd xmm0, [rip+disp]
  emit_bytes(buf, &o, (const unsigned char*)"\xF2\x48\x0F\x2C\xC0", 5); // cvttsd2si rax, xmm0
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x8D\x54\x24\x1F", 5); // lea r10, [rsp+31]
  emit_bytes(buf, &o, (const unsigned char*)"\x41\xB3\x06", 3); // mov r11b, 6
  size_t frac_loop = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xD2", 2); // xor edx, edx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\xC1\x0A\x00\x00\x00", 7); // mov rcx,10
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xF7\xF1", 3); // div rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x80\xC2\x30", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x88\x12", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xCA", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x41\xFE\xCB", 3); // dec r11b
  emit_bytes(buf, &o, (const unsigned char*)"\x75", 1); emit1(buf, &o, (unsigned char)((int)frac_loop - ((int)o + 1)));
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x8D\x54\x24\x1A", 5); // lea r10, [rsp+26]
  emit_bytes(buf, &o, (const unsigned char*)"\xB9\x06\x00\x00\x00", 5); // mov ecx,6
  size_t copy2 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x8A\x12", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x41\x88\x11", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC2", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC1", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC9", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x75", 1); emit1(buf, &o, (unsigned char)((int)copy2 - ((int)o + 1)));
  emit_bytes(buf, &o, (const unsigned char*)"\x41\xC6\x01\x00", 4); // null
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x60", 4); // add rsp,96
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  // const 1e6
  size_t const_pos = o;
  emit8(buf, &o, 0x412E848000000000ULL);
  {
    int32_t disp = (int32_t)(const_pos - (mul_disp + 4));
    memcpy(buf + mul_disp, &disp, 4);
  }

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
  emit1(buf, &o, 0xBE); emit4(buf, &o, 0x241); // mov esi, O_WRONLY|O_CREAT|O_TRUNC
  emit_bytes(buf, &o, (const unsigned char*)"\xEB\x00", 2); // jmp open
  size_t j_mode_open2 = o - 1;
  size_t mode_a = o;
  emit1(buf, &o, 0xBE); emit4(buf, &o, 0x441); // mov esi, O_WRONLY|O_CREAT|O_APPEND
  size_t mode_open = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xCF", 3); // mov rdi, rcx
  emit1(buf, &o, 0xBA); emit4(buf, &o, 0x1A4); // mov edx, 0644
  emit1(buf, &o, 0xB8); emit4(buf, &o, 2); // mov eax, 2 (open)
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2); // syscall
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x79\x00", 2); // jns ok
  size_t j_fopen_ok = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xF7\xD8", 3); // neg rax
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr); // mov r11, err_addr
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3); // mov [r11], rax
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3); // xor eax,eax; ret
  size_t fopen_ok = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x30", 4); // sub rsp, 48
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x44\x24\x20", 5); // mov [rsp+0x20], rax
  emit_bytes(buf, &o, (const unsigned char*)"\xB9\x20\x00\x00\x00", 5); // mov ecx, 32
  emit1(buf, &o, 0xE8); size_t call_valloc = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x54\x24\x20", 5); // mov rdx, [rsp+0x20]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x30", 4); // add rsp, 48
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_fopen_ret0 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x10", 3); // mov [rax], rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\x40\x08\x00\x00\x00\x00", 8); // eof=0
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\x40\x10\x00\x00\x00\x00", 8); // err=0
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\x40\x18\xFF\xFF\xFF\xFF", 8); // pushback=-1
  emit1(buf, &o, 0xC3);
  size_t fopen_ret0 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  buf[j_fopen_null] = (unsigned char)((int)fopen_ret0 - ((int)j_fopen_null + 1));
  buf[j_fopen_null2] = (unsigned char)((int)fopen_ret0 - ((int)j_fopen_null2 + 1));
  buf[j_mode_r] = (unsigned char)((int)mode_r - ((int)j_mode_r + 1));
  buf[j_mode_w] = (unsigned char)((int)mode_w - ((int)j_mode_w + 1));
  buf[j_mode_a] = (unsigned char)((int)mode_a - ((int)j_mode_a + 1));
  buf[j_mode_open] = (unsigned char)((int)mode_open - ((int)j_mode_open + 1));
  buf[j_mode_open2] = (unsigned char)((int)mode_open - ((int)j_mode_open2 + 1));
  buf[j_fopen_ok] = (unsigned char)((int)fopen_ok - ((int)j_fopen_ok + 1));
  buf[j_fopen_ret0] = (unsigned char)((int)fopen_ret0 - ((int)j_fopen_ret0 + 1));
  {
    int32_t disp = (int32_t)(out->off_valloc - (call_valloc + 4));
    memcpy(buf + call_valloc, &disp, 4);
  }

  // tn_fclose(FILE*) -> int
  out->off_fclose = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je retm1
  size_t j_fclose_null = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xC9", 3); // mov r9, rcx (save FILE*)
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xB8", 2); emit8(buf, &o, stdin_addr); // mov r8, stdin_addr
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x39\xC1", 3); // cmp rcx, r8
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_fclose_ret0_1 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xB8", 2); emit8(buf, &o, stdout_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x39\xC1", 3); // cmp rcx, r8
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_fclose_ret0_2 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xB8", 2); emit8(buf, &o, stderr_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x39\xC1", 3); // cmp rcx, r8
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je ret0
  size_t j_fclose_ret0_3 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x39", 3); // mov rdi, [rcx]
  emit1(buf, &o, 0xB8); emit4(buf, &o, 3); // mov eax, 3 (close)
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2); // syscall
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x79\x00", 2); // jns ok
  size_t j_fclose_ok = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xF7\xD8", 3); // neg rax
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3); // mov [r11], rax
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6); // mov eax,-1; ret
  size_t fclose_ok = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xC9", 3); // mov rcx, r9 (restore FILE*)
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x20", 4); // sub rsp, 32
  emit1(buf, &o, 0xE8); size_t call_vfree = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x20", 4); // add rsp, 32
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3); // ret 0
  size_t fclose_ret0 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  size_t fclose_retm1 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  buf[j_fclose_null] = (unsigned char)((int)fclose_retm1 - ((int)j_fclose_null + 1));
  buf[j_fclose_ret0_1] = (unsigned char)((int)fclose_ret0 - ((int)j_fclose_ret0_1 + 1));
  buf[j_fclose_ret0_2] = (unsigned char)((int)fclose_ret0 - ((int)j_fclose_ret0_2 + 1));
  buf[j_fclose_ret0_3] = (unsigned char)((int)fclose_ret0 - ((int)j_fclose_ret0_3 + 1));
  buf[j_fclose_ok] = (unsigned char)((int)fclose_ok - ((int)j_fclose_ok + 1));
  {
    int32_t disp = (int32_t)(out->off_vfree - (call_vfree + 4));
    memcpy(buf + call_vfree, &disp, 4);
  }
  // patch ret0 jumps
  (void)fclose_ret0;

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
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x8B\x39", 3); // mov rdi, [r9] fd
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xCE", 3); // mov rsi, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xC2", 3); // mov rdx, rax
  emit1(buf, &o, 0xB8); emit4(buf, &o, 0); // sys_read
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x79\x00", 2); // jns ok
  size_t j_fread_ok = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xF7\xD8", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  size_t fread_ok = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xF8\x00", 4); // cmp rax,0
  emit_bytes(buf, &o, (const unsigned char*)"\x75\x09", 2); // jne noeof
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xC7\x41\x08\x01\x00\x00\x00", 8); // eof=1
  emit_bytes(buf, &o, (const unsigned char*)"\xC3", 1);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xC7\x41\x08\x00\x00\x00\x00", 8); // eof=0
  emit1(buf, &o, 0xC3);
  size_t fread_ret0 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  buf[j_fread_ok] = (unsigned char)((int)fread_ok - ((int)j_fread_ok + 1));
  buf[j_fread_ret0a] = (unsigned char)((int)fread_ret0 - ((int)j_fread_ret0a + 1));
  buf[j_fread_ret0b] = (unsigned char)((int)fread_ret0 - ((int)j_fread_ret0b + 1));
  buf[j_fread_ret0c] = (unsigned char)((int)fread_ret0 - ((int)j_fread_ret0c + 1));

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
  emit1(buf, &o, 0xB8); emit4(buf, &o, 1); // sys_write
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x79\x00", 2); // jns ok
  size_t j_fwrite_ok = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xF7\xD8", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  size_t fwrite_ok = o;
  emit1(buf, &o, 0xC3);
  size_t fwrite_ret0 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  buf[j_fwrite_ok] = (unsigned char)((int)fwrite_ok - ((int)j_fwrite_ok + 1));
  buf[j_fwrite_ret0a] = (unsigned char)((int)fwrite_ret0 - ((int)j_fwrite_ret0a + 1));
  buf[j_fwrite_ret0b] = (unsigned char)((int)fwrite_ret0 - ((int)j_fwrite_ret0b + 1));
  buf[j_fwrite_ret0c] = (unsigned char)((int)fwrite_ret0 - ((int)j_fwrite_ret0c + 1));

  // tn_fseek(f, off, whence) -> int
  out->off_fseek = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x03", 2); // je retm1
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\xC9", 3); // mov r9, rcx (save FILE*)
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x39", 3); // mov rdi, [rcx]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xD6", 3); // mov rsi, rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xC2", 3); // mov rdx, r8
  emit1(buf, &o, 0xB8); emit4(buf, &o, 8); // sys_lseek
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x79\x00", 2); // jns ok
  size_t j_fseek_ok = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xF7\xD8", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  size_t fseek_ok = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xC7\x41\x08\x00\x00\x00\x00", 8); // eof=0
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  size_t fseek_retm1 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  buf[j_fseek_ok] = (unsigned char)((int)fseek_ok - ((int)j_fseek_ok + 1));
  (void)fseek_retm1;

  // tn_ftell(f) -> i64
  out->off_ftell = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x03", 2); // je retm1
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x39", 3); // mov rdi, [rcx]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x31\xF6", 3); // xor rsi, rsi
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\xC2\x01\x00\x00\x00", 7); // mov rdx,1 (SEEK_CUR)
  emit1(buf, &o, 0xB8); emit4(buf, &o, 8); // sys_lseek
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x79\x00", 2); // jns ok
  size_t j_ftell_ok = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xF7\xD8", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  size_t ftell_ok = o;
  emit1(buf, &o, 0xC3);
  size_t ftell_retm1 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  buf[j_ftell_ok] = (unsigned char)((int)ftell_ok - ((int)j_ftell_ok + 1));
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
  emit1(buf, &o, 0xB8); emit4(buf, &o, 0); // sys_read
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x79\x00", 2); // jns ok
  size_t j_rb_ok = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xF7\xD8", 3); // neg rax
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3); // mov [r11], rax
  emit_bytes(buf, &o, (const unsigned char*)"\x4D\x85\xD2", 3); // test r10, r10
  emit_bytes(buf, &o, (const unsigned char*)"\x75\x00", 2); // jne return_total
  size_t j_rb_return_total = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3); // xor eax,eax; ret
  size_t rb_return_total = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xD0", 3); // mov rax, r10
  emit1(buf, &o, 0xC3);
  size_t rb_ok = o;
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
  size_t rb_done_total = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xD0", 3); // mov rax, r10
  emit1(buf, &o, 0xC3);
  size_t rb_ret0 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  buf[j_rb_ret0a] = (unsigned char)((int)rb_ret0 - ((int)j_rb_ret0a + 1));
  buf[j_rb_ret0b] = (unsigned char)((int)rb_ret0 - ((int)j_rb_ret0b + 1));
  buf[j_rb_ret0c] = (unsigned char)((int)rb_ret0 - ((int)j_rb_ret0c + 1));
  buf[j_rb_no_pb] = (unsigned char)((int)rb_no_pb - ((int)j_rb_no_pb + 1));
  buf[j_rb_done_total] = (unsigned char)((int)rb_done_total - ((int)j_rb_done_total + 1));
  buf[j_rb_ok] = (unsigned char)((int)rb_ok - ((int)j_rb_ok + 1));
  buf[j_rb_return_total] = (unsigned char)((int)rb_return_total - ((int)j_rb_return_total + 1));
  buf[j_rb_not_eof] = (unsigned char)((int)rb_not_eof - ((int)j_rb_not_eof + 1));
  buf[j_rb_after_eof] = (unsigned char)((int)rb_after_eof - ((int)j_rb_after_eof + 1));

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
  emit1(buf, &o, 0xB8); emit4(buf, &o, 1); // sys_write
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x78\x00", 2); // js err
  size_t j_wl_err1 = o - 1;
  size_t wl_write_nl = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x10", 4); // sub rsp,16
  emit_bytes(buf, &o, (const unsigned char*)"\xC6\x04\x24\x0A", 4); // mov byte [rsp], '\n'
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x8B\x39", 3); // mov rdi, [r9]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8D\x34\x24", 4); // lea rsi, [rsp]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\xC2\x01\x00\x00\x00", 7); // mov rdx,1
  emit1(buf, &o, 0xB8); emit4(buf, &o, 1); // sys_write
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x10", 4); // add rsp,16
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x78\x00", 2); // js err
  size_t j_wl_err2 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3); // ret 0
  size_t wl_err = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xF7\xD8", 3); // neg rax
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xBB", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x49\x89\x03", 3); // mov [r11], rax
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6); // mov eax,-1; ret
  size_t wl_retm1 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  buf[j_wl_retm1] = (unsigned char)((int)wl_retm1 - ((int)j_wl_retm1 + 1));
  buf[j_wl_len_done] = (unsigned char)((int)wl_len_done - ((int)j_wl_len_done + 1));
  buf[j_wl_len_done2] = (unsigned char)((int)wl_len_done - ((int)j_wl_len_done2 + 1));
  buf[j_wl_len_loop] = (unsigned char)((int)wl_len_loop - ((int)j_wl_len_loop + 1));
  buf[j_wl_write_nl] = (unsigned char)((int)wl_write_nl - ((int)j_wl_write_nl + 1));
  buf[j_wl_err1] = (unsigned char)((int)wl_err - ((int)j_wl_err1 + 1));
  buf[j_wl_err2] = (unsigned char)((int)wl_err - ((int)j_wl_err2 + 1));

  // tn_read_line(f) -> *u8
  out->off_read_line = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2); size_t j_rl_ret0 = o; emit4(buf, &o, 0); // je ret0
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x60", 4); // sub rsp,96
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x4C\x24\x48", 5); // mov [rsp+72], rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\x44\x24\x20\x00\x00\x00\x00", 9); // buf=0
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\x44\x24\x28\x00\x00\x00\x00", 9); // len=0
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\x44\x24\x30\x80\x00\x00\x00", 9); // cap=128
  emit_bytes(buf, &o, (const unsigned char*)"\xB9\x81\x00\x00\x00", 5); // mov ecx,129
  emit1(buf, &o, 0xE8); size_t call_rl_valloc1 = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2); size_t j_rl_fail = o; emit4(buf, &o, 0); // je fail
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x44\x24\x20", 5); // buf=rax
  size_t rl_loop = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x4C\x24\x48", 5); // mov rcx, [rsp+72]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8D\x54\x24\x38", 5); // lea rdx, [rsp+56]
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xC7\xC0\x01\x00\x00\x00", 7); // mov r8,1
  emit1(buf, &o, 0xE8); size_t call_rl_read1 = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x8E", 2); size_t j_rl_done = o; emit4(buf, &o, 0); // jle done
  emit_bytes(buf, &o, (const unsigned char*)"\x8A\x44\x24\x38", 4); // mov al, [rsp+56]
  emit_bytes(buf, &o, (const unsigned char*)"\x3C\x0A", 2); // cmp al, '\n'
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2); size_t j_rl_done2 = o; emit4(buf, &o, 0); // je done
  emit_bytes(buf, &o, (const unsigned char*)"\x3C\x0D", 2); // cmp al, '\r'
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x85", 2); size_t j_rl_append = o; emit4(buf, &o, 0); // jne append
  // handle CR
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x4C\x24\x48", 5); // mov rcx, [rsp+72]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8D\x54\x24\x38", 5); // lea rdx, [rsp+56]
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xC7\xC0\x01\x00\x00\x00", 7); // mov r8,1
  emit1(buf, &o, 0xE8); size_t call_rl_read2 = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xF8\x01", 4); // cmp rax,1
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x85", 2); size_t j_rl_done3 = o; emit4(buf, &o, 0); // jne done
  emit_bytes(buf, &o, (const unsigned char*)"\x8A\x44\x24\x38", 4); // mov al, [rsp+56]
  emit_bytes(buf, &o, (const unsigned char*)"\x3C\x0A", 2); // cmp al,'\n'
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2); size_t j_rl_done4 = o; emit4(buf, &o, 0); // je done
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x4C\x24\x48", 5); // mov rcx, [rsp+72]
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\xB6\x44\x24\x38", 5); // movzx eax, byte [rsp+56]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x41\x18", 4); // mov [rcx+24], rax
  emit_bytes(buf, &o, (const unsigned char*)"\xE9", 1); size_t j_rl_done5 = o; emit4(buf, &o, 0); // jmp done
  size_t rl_append = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x44\x24\x28", 5); // mov rax, [rsp+40] len
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x54\x24\x30", 5); // mov rdx, [rsp+48] cap
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8D\x40\x01", 4); // lea rax, [rax+1]
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x39\xD0", 3); // cmp rax, rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x82", 2); size_t j_rl_have = o; emit4(buf, &o, 0); // jb have_space
  // grow
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x01\xD2", 3); // add rdx, rdx (newcap)
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x54\x24\x30", 5); // store newcap
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xD1", 3); // mov rcx, rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC1", 3); // inc rcx
  emit1(buf, &o, 0xE8); size_t call_rl_valloc2 = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax, rax
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2); size_t j_rl_fail2 = o; emit4(buf, &o, 0); // je fail
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x44\x24\x40", 5); // newbuf temp
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xC1", 3); // mov rcx, rax (dest)
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x54\x24\x20", 5); // mov rdx, old buf
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x8B\x44\x24\x28", 5); // mov r8, len
  emit_bytes(buf, &o, (const unsigned char*)"\x4D\x85\xC0", 3); // test r8, r8
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je copy_done
  size_t j_rl_copy_done = o - 1;
  size_t rl_copy_loop = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x8A\x02", 2); // mov al, [rdx]
  emit_bytes(buf, &o, (const unsigned char*)"\x88\x01", 2); // mov [rcx], al
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC1", 3); // inc rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC2", 3); // inc rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x49\xFF\xC8", 3); // dec r8
  emit_bytes(buf, &o, (const unsigned char*)"\x75\x00", 2); // jne loop
  size_t j_rl_copy_loop = o - 1;
  size_t rl_copy_done = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x4C\x24\x20", 5); // mov rcx, old buf
  emit1(buf, &o, 0xE8); size_t call_rl_vfree = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x44\x24\x40", 5); // mov rax, newbuf
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x44\x24\x20", 5); // buf=newbuf
  size_t rl_have_space = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x4C\x24\x20", 5); // mov rcx, buf
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x54\x24\x28", 5); // mov rdx, len
  emit_bytes(buf, &o, (const unsigned char*)"\x8A\x44\x24\x38", 4); // mov al, [rsp+56]
  emit_bytes(buf, &o, (const unsigned char*)"\x88\x04\x11", 3); // mov [rcx+rdx], al
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xFF\xC2", 3); // inc rdx
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x54\x24\x28", 5); // len=rdx
  emit_bytes(buf, &o, (const unsigned char*)"\xE9", 1); size_t j_rl_loop = o; emit4(buf, &o, 0); // jmp loop
  size_t rl_done = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x44\x24\x20", 5); // mov rax, buf
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x54\x24\x28", 5); // mov rdx, len
  emit_bytes(buf, &o, (const unsigned char*)"\xC6\x04\x10\x00", 4); // buf[len]=0
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x44\x24\x20", 5); // mov rax, buf
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x60", 4); // add rsp,96
  emit_bytes(buf, &o, (const unsigned char*)"\xC3", 1);
  size_t rl_fail = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x4C\x24\x20", 5); // mov rcx, buf
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je fail_ret
  size_t j_rl_fail_ret = o - 1;
  emit1(buf, &o, 0xE8); size_t call_rl_vfree2 = o; emit4(buf, &o, 0);
  size_t rl_fail_ret = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0", 2); // xor eax,eax
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x60", 4); // add rsp,96
  emit_bytes(buf, &o, (const unsigned char*)"\xC3", 1);
  size_t rl_ret0 = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3); // xor eax,eax; ret
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
    disp = (int32_t)(rl_copy_done - ((int)j_rl_copy_done + 1));
    buf[j_rl_copy_done] = (unsigned char)disp;
    disp = (int32_t)(rl_copy_loop - ((int)j_rl_copy_loop + 1));
    buf[j_rl_copy_loop] = (unsigned char)disp;
    disp = (int32_t)(rl_loop - (j_rl_loop + 4));
    memcpy(buf + j_rl_loop, &disp, 4);
    disp = (int32_t)(rl_fail_ret - ((int)j_rl_fail_ret + 1));
    buf[j_rl_fail_ret] = (unsigned char)disp;
    disp = (int32_t)(out->off_valloc - (call_rl_valloc1 + 4));
    memcpy(buf + call_rl_valloc1, &disp, 4);
    disp = (int32_t)(out->off_valloc - (call_rl_valloc2 + 4));
    memcpy(buf + call_rl_valloc2, &disp, 4);
    disp = (int32_t)(out->off_vfree - (call_rl_vfree + 4));
    memcpy(buf + call_rl_vfree, &disp, 4);
    disp = (int32_t)(out->off_vfree - (call_rl_vfree2 + 4));
    memcpy(buf + call_rl_vfree2, &disp, 4);
    disp = (int32_t)(out->off_read_bytes - (call_rl_read1 + 4));
    memcpy(buf + call_rl_read1, &disp, 4);
    disp = (int32_t)(out->off_read_bytes - (call_rl_read2 + 4));
    memcpy(buf + call_rl_read2, &disp, 4);
  }

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

  // GetFileAttributesA-compatible shim for shared x64 lowering:
  // returns -1 for missing, 16 for directories, 0 for regular files.
  out->off_getfileattr = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx, rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je missing
  size_t j_attr_missing0 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x81\xEC\xA0\x00\x00\x00", 7); // sub rsp,160
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xCE", 3); // mov rsi, rcx (path)
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\xC7\x9C\xFF\xFF\xFF", 7); // mov rdi,-100 (AT_FDCWD)
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xE2", 3); // mov rdx,rsp (stat buf)
  emit_bytes(buf, &o, (const unsigned char*)"\x45\x31\xD2", 3); // xor r10d,r10d
  emit1(buf, &o, 0xB8); emit4(buf, &o, 262); // newfstatat
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3); // test rax,rax
  emit_bytes(buf, &o, (const unsigned char*)"\x78\x00", 2); // js missing_stack
  size_t j_attr_missing_stack = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x8B\x44\x24\x18", 4); // mov eax,[rsp+24] st_mode
  emit_bytes(buf, &o, (const unsigned char*)"\x25\x00\xF0\x00\x00", 5); // and eax,S_IFMT
  emit_bytes(buf, &o, (const unsigned char*)"\x3D\x00\x40\x00\x00", 5); // cmp eax,S_IFDIR
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x94\xC0", 3); // sete al
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x0F\xB6\xC0", 4); // movzx rax,al
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC1\xE0\x04", 4); // shl rax,4
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x81\xC4\xA0\x00\x00\x00", 7); // add rsp,160
  emit1(buf, &o, 0xC3);
  size_t attr_missing_stack = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x81\xC4\xA0\x00\x00\x00", 7); // add rsp,160
  size_t attr_missing = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\xC0\xFF\xFF\xFF\xFF", 7); // mov rax,-1
  emit1(buf, &o, 0xC3);
  buf[j_attr_missing0] = (unsigned char)((int)attr_missing - ((int)j_attr_missing0 + 1));
  buf[j_attr_missing_stack] = (unsigned char)((int)attr_missing_stack - ((int)j_attr_missing_stack + 1));

  // CreateDirectoryA-compatible shim: returns nonzero on success.
  out->off_createdir = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3); // test rcx,rcx
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2); // je fail
  size_t j_mkdir_fail0 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xCF", 3); // mov rdi,rcx
  emit1(buf, &o, 0xBE); emit4(buf, &o, 0777); // mov esi,0777
  emit1(buf, &o, 0xB8); emit4(buf, &o, 83); // mkdir
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x78\x00", 2); // js fail
  size_t j_mkdir_fail1 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\x01\x00\x00\x00\xC3", 6);
  size_t mkdir_fail = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  buf[j_mkdir_fail0] = (unsigned char)((int)mkdir_fail - ((int)j_mkdir_fail0 + 1));
  buf[j_mkdir_fail1] = (unsigned char)((int)mkdir_fail - ((int)j_mkdir_fail1 + 1));

  // RemoveDirectoryA-compatible shim: returns nonzero on success.
  out->off_removedir = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2);
  size_t j_rmdir_fail0 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xCF", 3);
  emit1(buf, &o, 0xB8); emit4(buf, &o, 84); // rmdir
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x78\x00", 2);
  size_t j_rmdir_fail1 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\x01\x00\x00\x00\xC3", 6);
  size_t rmdir_fail = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  buf[j_rmdir_fail0] = (unsigned char)((int)rmdir_fail - ((int)j_rmdir_fail0 + 1));
  buf[j_rmdir_fail1] = (unsigned char)((int)rmdir_fail - ((int)j_rmdir_fail1 + 1));

  // C remove-compatible shim: returns 0 on success, -1 on failure.
  out->off_deletefile = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2);
  size_t j_unlink_fail0 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xCF", 3);
  emit1(buf, &o, 0xB8); emit4(buf, &o, 87); // unlink
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x78\x00", 2);
  size_t j_unlink_fail1 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  size_t unlink_fail = o;
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  buf[j_unlink_fail0] = (unsigned char)((int)unlink_fail - ((int)j_unlink_fail0 + 1));
  buf[j_unlink_fail1] = (unsigned char)((int)unlink_fail - ((int)j_unlink_fail1 + 1));

  // C rename-compatible shim: returns 0 on success, -1 on failure.
  out->off_movefile = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC9", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2);
  size_t j_rename_fail0 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xD2", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x74\x00", 2);
  size_t j_rename_fail1 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xCF", 3); // old
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xD6", 3); // new
  emit1(buf, &o, 0xB8); emit4(buf, &o, 82); // rename
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x85\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x78\x00", 2);
  size_t j_rename_fail2 = o - 1;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);
  size_t rename_fail = o;
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
  buf[j_rename_fail0] = (unsigned char)((int)rename_fail - ((int)j_rename_fail0 + 1));
  buf[j_rename_fail1] = (unsigned char)((int)rename_fail - ((int)j_rename_fail1 + 1));
  buf[j_rename_fail2] = (unsigned char)((int)rename_fail - ((int)j_rename_fail2 + 1));

  // list_dir(path) -> newline-delimited entry names using openat/getdents64.
  // The direct-native primitive stays non-recursive; recursive listing and glob
  // are bridged through the stdlib helpers that build on this primitive.
  out->off_list_dir = o;
  {
    static const unsigned char list_dir_code[] = {
      0x48, 0x85, 0xC9, 0x0F, 0x84, 0x29, 0x01, 0x00, 0x00, 0x48, 0x81, 0xEC,
      0x00, 0x11, 0x00, 0x00, 0x48, 0x89, 0xCE, 0x48, 0xC7, 0xC7, 0x9C, 0xFF,
      0xFF, 0xFF, 0xBA, 0x00, 0x00, 0x01, 0x00, 0x45, 0x31, 0xD2, 0xB8, 0x01,
      0x01, 0x00, 0x00, 0x0F, 0x05, 0x48, 0x85, 0xC0, 0x0F, 0x88, 0xF6, 0x00,
      0x00, 0x00, 0x48, 0x89, 0x04, 0x24, 0xB9, 0x00, 0x20, 0x00, 0x00, 0xE8,
      0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xD4, 0x00, 0x00,
      0x00, 0x48, 0x89, 0x44, 0x24, 0x08, 0xC6, 0x00, 0x00, 0x48, 0x8B, 0x3C,
      0x24, 0x48, 0x8D, 0xB4, 0x24, 0x80, 0x00, 0x00, 0x00, 0xBA, 0x00, 0x10,
      0x00, 0x00, 0xB8, 0xD9, 0x00, 0x00, 0x00, 0x0F, 0x05, 0x48, 0x89, 0x44,
      0x24, 0x18, 0x48, 0x8B, 0x3C, 0x24, 0xB8, 0x03, 0x00, 0x00, 0x00, 0x0F,
      0x05, 0x48, 0x8B, 0x44, 0x24, 0x18, 0x48, 0x85, 0xC0, 0x0F, 0x88, 0xA1,
      0x00, 0x00, 0x00, 0x4C, 0x8D, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00, 0x4D,
      0x8D, 0x0C, 0x00, 0x4C, 0x8B, 0x54, 0x24, 0x08, 0x45, 0x31, 0xDB, 0x4D,
      0x39, 0xC8, 0x73, 0x56, 0x41, 0x0F, 0xB7, 0x48, 0x10, 0x85, 0xC9, 0x7E,
      0x4D, 0x49, 0x8D, 0x50, 0x13, 0x8A, 0x02, 0x3C, 0x2E, 0x75, 0x12, 0x8A,
      0x42, 0x01, 0x84, 0xC0, 0x74, 0x37, 0x3C, 0x2E, 0x75, 0x07, 0x8A, 0x42,
      0x02, 0x84, 0xC0, 0x74, 0x2C, 0x49, 0x81, 0xFB, 0xFE, 0x1F, 0x00, 0x00,
      0x73, 0x28, 0x8A, 0x02, 0x84, 0xC0, 0x74, 0x0C, 0x43, 0x88, 0x04, 0x1A,
      0x49, 0xFF, 0xC3, 0x48, 0xFF, 0xC2, 0xEB, 0xE5, 0x49, 0x81, 0xFB, 0xFE,
      0x1F, 0x00, 0x00, 0x73, 0x0D, 0x43, 0xC6, 0x04, 0x1A, 0x0A, 0x49, 0xFF,
      0xC3, 0x49, 0x01, 0xC8, 0xEB, 0xA5, 0x4D, 0x85, 0xDB, 0x74, 0x13, 0x49,
      0xFF, 0xCB, 0x43, 0xC6, 0x04, 0x1A, 0x00, 0x4C, 0x89, 0xD0, 0x48, 0x81,
      0xC4, 0x00, 0x11, 0x00, 0x00, 0xC3, 0x41, 0xC6, 0x02, 0x00, 0x4C, 0x89,
      0xD0, 0x48, 0x81, 0xC4, 0x00, 0x11, 0x00, 0x00, 0xC3, 0x48, 0x8B, 0x3C,
      0x24, 0xB8, 0x03, 0x00, 0x00, 0x00, 0x0F, 0x05, 0x31, 0xC0, 0x48, 0x81,
      0xC4, 0x00, 0x11, 0x00, 0x00, 0xC3, 0x31, 0xC0, 0xC3
    };
    size_t list_start = o;
    emit_bytes(buf, &o, list_dir_code, sizeof(list_dir_code));
    {
      size_t call_imm = list_start + 0x3Cu;
      int32_t disp = (int32_t)(out->off_valloc - (call_imm + 4u));
      memcpy(buf + call_imm, &disp, 4);
    }
  }

  // proc_run(cmd) -> /bin/sh -c cmd wait status using fork/execve/wait4.
  out->off_proc_run = o;
  {
    static const unsigned char proc_run_code[] = {
      0x48, 0x85, 0xC9, 0x75, 0x06, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0xC3, 0x55,
      0x48, 0x89, 0xE5, 0x53, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x89,
      0xCB, 0xB8, 0x39, 0x00, 0x00, 0x00, 0x0F, 0x05, 0x48, 0x85, 0xC0, 0x78,
      0x70, 0x74, 0x1B, 0x49, 0x89, 0xC4, 0x48, 0x8D, 0x34, 0x24, 0x4C, 0x89,
      0xE7, 0x31, 0xD2, 0x45, 0x31, 0xD2, 0xB8, 0x3D, 0x00, 0x00, 0x00, 0x0F,
      0x05, 0x8B, 0x04, 0x24, 0xEB, 0x58, 0x48, 0xB8, 0x2F, 0x62, 0x69, 0x6E,
      0x2F, 0x73, 0x68, 0x00, 0x48, 0x89, 0x44, 0x24, 0x20, 0xC7, 0x44, 0x24,
      0x30, 0x2D, 0x63, 0x00, 0x00, 0x48, 0x8D, 0x7C, 0x24, 0x20, 0x48, 0x89,
      0x7C, 0x24, 0x38, 0x48, 0x8D, 0x44, 0x24, 0x30, 0x48, 0x89, 0x44, 0x24,
      0x40, 0x48, 0x89, 0x5C, 0x24, 0x48, 0x48, 0xC7, 0x44, 0x24, 0x50, 0x00,
      0x00, 0x00, 0x00, 0x48, 0x8D, 0x74, 0x24, 0x38, 0x31, 0xD2, 0xB8, 0x3B,
      0x00, 0x00, 0x00, 0x0F, 0x05, 0xBF, 0x7F, 0x00, 0x00, 0x00, 0xB8, 0x3C,
      0x00, 0x00, 0x00, 0x0F, 0x05, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x83,
      0xC4, 0x60, 0x41, 0x5C, 0x5B, 0x5D, 0xC3
    };
    emit_bytes(buf, &o, proc_run_code, sizeof(proc_run_code));
  }

  // proc_out(cmd) -> bounded stdout capture using pipe/fork/execve/read/wait4.
  out->off_proc_out = o;
  {
    static const unsigned char proc_out_code[] = {
      0x48, 0x85, 0xC9, 0x75, 0x03, 0x31, 0xC0, 0xC3, 0x55, 0x48, 0x89, 0xE5,
      0x53, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC,
      0x90, 0x00, 0x00, 0x00, 0x48, 0x89, 0xCB, 0x48, 0x8D, 0x3C, 0x24, 0xB8,
      0x16, 0x00, 0x00, 0x00, 0x0F, 0x05, 0x48, 0x85, 0xC0, 0x0F, 0x88, 0x3C,
      0x01, 0x00, 0x00, 0x44, 0x8B, 0x24, 0x24, 0x44, 0x8B, 0x6C, 0x24, 0x04,
      0xB8, 0x39, 0x00, 0x00, 0x00, 0x0F, 0x05, 0x48, 0x85, 0xC0, 0x0F, 0x88,
      0x97, 0x00, 0x00, 0x00, 0x0F, 0x84, 0xA7, 0x00, 0x00, 0x00, 0x49, 0x89,
      0xC6, 0x44, 0x89, 0xEF, 0xB8, 0x03, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xB9,
      0x00, 0x80, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0,
      0x74, 0x53, 0x49, 0x89, 0xC7, 0x31, 0xDB, 0x48, 0x81, 0xFB, 0xFF, 0x7F,
      0x00, 0x00, 0x73, 0x1D, 0x44, 0x89, 0xE7, 0x49, 0x8D, 0x34, 0x1F, 0xBA,
      0xFF, 0x7F, 0x00, 0x00, 0x48, 0x29, 0xDA, 0x31, 0xC0, 0x0F, 0x05, 0x48,
      0x85, 0xC0, 0x7E, 0x05, 0x48, 0x01, 0xC3, 0xEB, 0xDA, 0x41, 0xC6, 0x04,
      0x1F, 0x00, 0x44, 0x89, 0xE7, 0xB8, 0x03, 0x00, 0x00, 0x00, 0x0F, 0x05,
      0x4C, 0x89, 0xF7, 0x31, 0xF6, 0x31, 0xD2, 0x45, 0x31, 0xD2, 0xB8, 0x3D,
      0x00, 0x00, 0x00, 0x0F, 0x05, 0x4C, 0x89, 0xF8, 0xE9, 0xB0, 0x00, 0x00,
      0x00, 0x44, 0x89, 0xE7, 0xB8, 0x03, 0x00, 0x00, 0x00, 0x0F, 0x05, 0x4C,
      0x89, 0xF7, 0x31, 0xF6, 0x31, 0xD2, 0x45, 0x31, 0xD2, 0xB8, 0x3D, 0x00,
      0x00, 0x00, 0x0F, 0x05, 0x31, 0xC0, 0xE9, 0x8E, 0x00, 0x00, 0x00, 0x44,
      0x89, 0xE7, 0xB8, 0x03, 0x00, 0x00, 0x00, 0x0F, 0x05, 0x44, 0x89, 0xEF,
      0xB8, 0x03, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xEB, 0x76, 0x44, 0x89, 0xE7,
      0xB8, 0x03, 0x00, 0x00, 0x00, 0x0F, 0x05, 0x44, 0x89, 0xEF, 0xBE, 0x01,
      0x00, 0x00, 0x00, 0xB8, 0x21, 0x00, 0x00, 0x00, 0x0F, 0x05, 0x44, 0x89,
      0xEF, 0xB8, 0x03, 0x00, 0x00, 0x00, 0x0F, 0x05, 0x48, 0xB8, 0x2F, 0x62,
      0x69, 0x6E, 0x2F, 0x73, 0x68, 0x00, 0x48, 0x89, 0x44, 0x24, 0x20, 0xC7,
      0x44, 0x24, 0x30, 0x2D, 0x63, 0x00, 0x00, 0x48, 0x8D, 0x7C, 0x24, 0x20,
      0x48, 0x89, 0x7C, 0x24, 0x38, 0x48, 0x8D, 0x44, 0x24, 0x30, 0x48, 0x89,
      0x44, 0x24, 0x40, 0x48, 0x89, 0x5C, 0x24, 0x48, 0x48, 0xC7, 0x44, 0x24,
      0x50, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x74, 0x24, 0x38, 0x31, 0xD2,
      0xB8, 0x3B, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xBF, 0x7F, 0x00, 0x00, 0x00,
      0xB8, 0x3C, 0x00, 0x00, 0x00, 0x0F, 0x05, 0x31, 0xC0, 0x48, 0x81, 0xC4,
      0x90, 0x00, 0x00, 0x00, 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C,
      0x5B, 0x5D, 0xC3
    };
    size_t proc_out_start = o;
    emit_bytes(buf, &o, proc_out_code, sizeof(proc_out_code));
    {
      size_t call_imm = proc_out_start + 101u;
      int32_t disp = (int32_t)(out->off_valloc - (call_imm + 4u));
      memcpy(buf + call_imm, &disp, 4);
    }
  }

  // Linux x86_64 network runtime bridge. The first production lane supports
  // IPv4 loopback sockets for direct-native smoke and local services.
  out->off_net_af_inet = o;
  emit1(buf, &o, 0xB8); emit4(buf, &o, 2); emit1(buf, &o, 0xC3);

  out->off_net_af_inet6 = o;
  emit1(buf, &o, 0xB8); emit4(buf, &o, 10); emit1(buf, &o, 0xC3);

  out->off_net_sock_stream = o;
  emit1(buf, &o, 0xB8); emit4(buf, &o, 1); emit1(buf, &o, 0xC3);

  out->off_net_sock_dgram = o;
  emit1(buf, &o, 0xB8); emit4(buf, &o, 2); emit1(buf, &o, 0xC3);

  out->off_net_ipproto_tcp = o;
  emit1(buf, &o, 0xB8); emit4(buf, &o, 6); emit1(buf, &o, 0xC3);

  out->off_net_ipproto_udp = o;
  emit1(buf, &o, 0xB8); emit4(buf, &o, 17); emit1(buf, &o, 0xC3);

  out->off_net_init = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);

  out->off_net_cleanup = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);

  out->off_net_last_error = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xB8", 2); emit8(buf, &o, err_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x00\xC3", 4);

  // net_socket(fam:rcx, type:rdx, proto:r8) -> fd | -1
  out->off_net_socket = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x89\xCF", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x89\xD6", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x44\x89\xC2", 3);
  emit1(buf, &o, 0xB8); emit4(buf, &o, 41);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_linux_syscall_return(buf, &o, err_addr, 0);

  // net_close(sock:rcx) -> 0 | -1
  out->off_net_close = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x89\xCF", 2);
  emit1(buf, &o, 0xB8); emit4(buf, &o, 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_linux_syscall_return(buf, &o, err_addr, 1);

  // parse_ipv4(host:rcx, default_addr:edx) -> sockaddr-ready IPv4 dword | -1
  // The returned dword is safe to store directly into sockaddr_in.sin_addr.
  out->off_net_parse_ipv4 = o;
  emit_bytes(buf, &o, (const unsigned char*)
    "\x89\xD0"                         // mov eax, edx
    "\x48\x85\xC9"                     // test rcx, rcx
    "\x74\x6C"                         // je done
    "\x80\x39\x00"                     // cmp byte [rcx],0
    "\x74\x67"                         // je done
    "\x45\x31\xC0"                     // xor r8d,r8d
    "\x45\x31\xC9"                     // xor r9d,r9d
    "\x45\x31\xD2"                     // part: xor r10d,r10d
    "\x45\x31\xDB"                     // xor r11d,r11d
    "\x0F\xB6\x01"                     // digit_loop: movzx eax, byte [rcx]
    "\x3C\x30"                         // cmp al,'0'
    "\x72\x25"                         // jb after_digits
    "\x3C\x39"                         // cmp al,'9'
    "\x77\x21"                         // ja after_digits
    "\x45\x6B\xD2\x0A"                 // imul r10d,r10d,10
    "\x83\xE8\x30"                     // sub eax,'0'
    "\x41\x01\xC2"                     // add r10d,eax
    "\x41\x81\xFA\xFF\x00\x00\x00"     // cmp r10d,255
    "\x77\x38"                         // ja invalid
    "\x48\xFF\xC1"                     // inc rcx
    "\x41\xFF\xC3"                     // inc r11d
    "\x41\x83\xFB\x03"                 // cmp r11d,3
    "\x77\x2C"                         // ja invalid
    "\xEB\xD4"                         // jmp digit_loop
    "\x45\x85\xDB"                     // after_digits: test r11d,r11d
    "\x74\x25"                         // jz invalid
    "\x41\xC1\xE0\x08"                 // shl r8d,8
    "\x45\x09\xD0"                     // or r8d,r10d
    "\x41\xFF\xC1"                     // inc r9d
    "\x41\x83\xF9\x04"                 // cmp r9d,4
    "\x74\x0A"                         // je final
    "\x80\x39\x2E"                     // cmp byte [rcx],'.'
    "\x75\x10"                         // jne invalid
    "\x48\xFF\xC1"                     // inc rcx
    "\xEB\xAF"                         // jmp part
    "\x80\x39\x00"                     // final: cmp byte [rcx],0
    "\x75\x06"                         // jne invalid
    "\x44\x89\xC0"                     // mov eax,r8d
    "\x0F\xC8"                         // bswap eax
    "\xC3"                             // ret
    "\xB8\xFF\xFF\xFF\xFF"             // invalid: mov eax,-1
    "\xC3",                            // done: ret
    116);

  // net_connect(sock:rcx, host:rdx, port:r8) -> 0 | -1
  // Direct-native supports IPv4 literal hosts; null/empty host defaults to loopback.
  out->off_net_connect = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x20", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x4C\x24\x18", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\x44\x24\x10", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xD1", 3);
  emit1(buf, &o, 0xBA); emit4(buf, &o, 0x0100007Fu);
  emit1(buf, &o, 0xE8); { size_t pos = o; emit4(buf, &o, 0); patch_rel32(buf, pos, out->off_net_parse_ipv4); }
  emit_bytes(buf, &o, (const unsigned char*)"\x83\xF8\xFF", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2); size_t j_connect_bad_host = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x89\x44\x24\x04", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x66\xC7\x04\x24\x02\x00", 6);
  emit_bytes(buf, &o, (const unsigned char*)"\x8B\x44\x24\x10", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x86\xE0", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x66\x89\x44\x24\x02", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\x44\x24\x08\x00\x00\x00\x00", 9);
  emit_bytes(buf, &o, (const unsigned char*)"\x8B\x7C\x24\x18", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xE6", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\xBA\x10\x00\x00\x00", 5);
  emit1(buf, &o, 0xB8); emit4(buf, &o, 42);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x20", 4);
  emit_linux_syscall_return(buf, &o, err_addr, 1);
  { size_t invalid = o;
    emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x20", 4);
    emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
    patch_rel32(buf, j_connect_bad_host, invalid);
  }

  // net_bind(sock:rcx, host:rdx, port:r8) -> 0 | -1
  // Applies SO_REUSEADDR and supports IPv4 literal hosts.
  out->off_net_bind = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x30", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x4C\x24\x28", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\x44\x24\x20", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\x54\x24\x18", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\xC7\x44\x24\x10\x01\x00\x00\x00", 8);
  emit_bytes(buf, &o, (const unsigned char*)"\x89\xCF", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\xBE\x01\x00\x00\x00", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\xBA\x02\x00\x00\x00", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x8D\x54\x24\x10", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x41\xB8\x04\x00\x00\x00", 6);
  emit1(buf, &o, 0xB8); emit4(buf, &o, 54);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x4C\x24\x18", 5);
  emit1(buf, &o, 0xBA); emit4(buf, &o, 0x0100007Fu);
  emit1(buf, &o, 0xE8); { size_t pos = o; emit4(buf, &o, 0); patch_rel32(buf, pos, out->off_net_parse_ipv4); }
  emit_bytes(buf, &o, (const unsigned char*)"\x83\xF8\xFF", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x84", 2); size_t j_bind_bad_host = o; emit4(buf, &o, 0);
  emit_bytes(buf, &o, (const unsigned char*)"\x89\x44\x24\x04", 4);
  emit_bytes(buf, &o, (const unsigned char*)"\x66\xC7\x04\x24\x02\x00", 6);
  emit_bytes(buf, &o, (const unsigned char*)"\x44\x8B\x44\x24\x20", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x44\x89\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x86\xE0", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x66\x89\x44\x24\x02", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xC7\x44\x24\x08\x00\x00\x00\x00", 9);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x8B\x7C\x24\x28", 5);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xE6", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\xBA\x10\x00\x00\x00", 5);
  emit1(buf, &o, 0xB8); emit4(buf, &o, 49);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x30", 4);
  emit_linux_syscall_return(buf, &o, err_addr, 1);
  { size_t invalid = o;
    emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x30", 4);
    emit_bytes(buf, &o, (const unsigned char*)"\xB8\xFF\xFF\xFF\xFF\xC3", 6);
    patch_rel32(buf, j_bind_bad_host, invalid);
  }

  // net_listen(sock:rcx, backlog:rdx) -> 0 | -1
  out->off_net_listen = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x89\xCF", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x89\xD6", 2);
  emit1(buf, &o, 0xB8); emit4(buf, &o, 50);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_linux_syscall_return(buf, &o, err_addr, 1);

  // net_accept(sock:rcx) -> fd | -1
  out->off_net_accept = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x89\xCF", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xF6", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xD2", 2);
  emit1(buf, &o, 0xB8); emit4(buf, &o, 43);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_linux_syscall_return(buf, &o, err_addr, 0);

  // net_send(sock:rcx, buf:rdx, len:r8) -> bytes | -1
  out->off_net_send = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x89\xCF", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xD6", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xC2", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x45\x31\xD2", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x45\x31\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x45\x31\xC9", 3);
  emit1(buf, &o, 0xB8); emit4(buf, &o, 44);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_linux_syscall_return(buf, &o, err_addr, 0);

  // net_recv(sock:rcx, buf:rdx, len:r8) -> bytes | -1
  out->off_net_recv = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x89\xCF", 2);
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x89\xD6", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x4C\x89\xC2", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x45\x31\xD2", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x45\x31\xC0", 3);
  emit_bytes(buf, &o, (const unsigned char*)"\x45\x31\xC9", 3);
  emit1(buf, &o, 0xB8); emit4(buf, &o, 45);
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2);
  emit_linux_syscall_return(buf, &o, err_addr, 0);

  out->off_net_set_blocking = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);

  out->off_net_set_timeout = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);

  out->off_net_resolve = o;
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC0\xC3", 3);

  out->size = o;
  return (o <= cap) ? 0 : -1;
}

static int build_linux_entry_stub(unsigned char* buf, size_t cap, uint64_t main_addr, size_t* out_size){
  if(!buf || cap < 32) return -1;
  size_t o = 0;
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xEC\x20", 4); // sub rsp, 32 (shadow space)
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xC9", 2); // xor ecx, ecx
  emit_bytes(buf, &o, (const unsigned char*)"\x31\xD2", 2); // xor edx, edx
  emit_bytes(buf, &o, (const unsigned char*)"\x45\x31\xC0", 3); // xor r8d, r8d
  emit_bytes(buf, &o, (const unsigned char*)"\x45\x31\xC9", 3); // xor r9d, r9d
  emit_bytes(buf, &o, (const unsigned char*)"\x48\xB8", 2); // mov rax, imm64
  emit8(buf, &o, main_addr);
  emit_bytes(buf, &o, (const unsigned char*)"\xFF\xD0", 2); // call rax
  emit_bytes(buf, &o, (const unsigned char*)"\x48\x83\xC4\x20", 4); // add rsp, 32
  emit_bytes(buf, &o, (const unsigned char*)"\x89\xC7", 2); // mov edi, eax
  emit_bytes(buf, &o, (const unsigned char*)"\xB8\x3C\x00\x00\x00", 5); // mov eax, 60
  emit_bytes(buf, &o, (const unsigned char*)"\x0F\x05", 2); // syscall
  if(out_size) *out_size = o;
  return (o <= cap) ? 0 : -1;
}

int ir_compile_to_elf_exe(IRModule* m, const char* out_exe){
  if(!m || !out_exe) return -1;

  const uint64_t base = 0x400000;
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
    uint32_t off = (uint32_t)(IMP_COUNT * ptr_size); // import ptr table

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
    uint32_t data_rva = align_up_u32(rdata_rva + rdata_size, 16);
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

    // bump heap in data
    const uint32_t heap_size = 1u << 20; // 1MB
    uint32_t heap_base_off = align_up_u32(data_size, 16);
    uint32_t heap_cur_off = heap_base_off + heap_size;
    uint32_t heap_end_off = heap_cur_off + 8;
    uint32_t heap_free_off = heap_end_off + 8;
    uint32_t total_data = heap_free_off + 8;
    uint32_t file_base_off = align_up_u32(total_data, 8);
    uint32_t stdin_off = file_base_off;
    const uint32_t file_struct_size = 32;
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

    // import table addresses
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
    imp.iat_sleep   = rdata_rva + (uint32_t)(IMP_SLEEP * ptr_size);
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
    imp.iat_getfileattr = rdata_rva + (uint32_t)(IMP_GETFILEATTR * ptr_size);
    imp.iat_createdir   = rdata_rva + (uint32_t)(IMP_CREATEDIR * ptr_size);
    imp.iat_removedir   = rdata_rva + (uint32_t)(IMP_REMOVEDIR * ptr_size);
    imp.iat_deletefile  = rdata_rva + (uint32_t)(IMP_DELETEFILE * ptr_size);
    imp.iat_movefile    = rdata_rva + (uint32_t)(IMP_MOVEFILE * ptr_size);
    imp.iat_remove      = rdata_rva + (uint32_t)(IMP_DELETEFILE * ptr_size);
    imp.iat_rename      = rdata_rva + (uint32_t)(IMP_MOVEFILE * ptr_size);
    imp.iat_list_dir    = rdata_rva + (uint32_t)(IMP_LISTDIR * ptr_size);
    imp.iat_proc_run    = rdata_rva + (uint32_t)(IMP_PROCRUN * ptr_size);
    imp.iat_proc_out    = rdata_rva + (uint32_t)(IMP_PROCOUT * ptr_size);
    imp.iat_net_af_inet       = rdata_rva + (uint32_t)(IMP_NET_AF_INET * ptr_size);
    imp.iat_net_af_inet6      = rdata_rva + (uint32_t)(IMP_NET_AF_INET6 * ptr_size);
    imp.iat_net_sock_stream   = rdata_rva + (uint32_t)(IMP_NET_SOCK_STREAM * ptr_size);
    imp.iat_net_sock_dgram    = rdata_rva + (uint32_t)(IMP_NET_SOCK_DGRAM * ptr_size);
    imp.iat_net_ipproto_tcp   = rdata_rva + (uint32_t)(IMP_NET_IPPROTO_TCP * ptr_size);
    imp.iat_net_ipproto_udp   = rdata_rva + (uint32_t)(IMP_NET_IPPROTO_UDP * ptr_size);
    imp.iat_net_init          = rdata_rva + (uint32_t)(IMP_NET_INIT * ptr_size);
    imp.iat_net_cleanup       = rdata_rva + (uint32_t)(IMP_NET_CLEANUP * ptr_size);
    imp.iat_net_last_error    = rdata_rva + (uint32_t)(IMP_NET_LAST_ERROR * ptr_size);
    imp.iat_net_socket        = rdata_rva + (uint32_t)(IMP_NET_SOCKET * ptr_size);
    imp.iat_net_close         = rdata_rva + (uint32_t)(IMP_NET_CLOSE * ptr_size);
    imp.iat_net_connect       = rdata_rva + (uint32_t)(IMP_NET_CONNECT * ptr_size);
    imp.iat_net_bind          = rdata_rva + (uint32_t)(IMP_NET_BIND * ptr_size);
    imp.iat_net_listen        = rdata_rva + (uint32_t)(IMP_NET_LISTEN * ptr_size);
    imp.iat_net_accept        = rdata_rva + (uint32_t)(IMP_NET_ACCEPT * ptr_size);
    imp.iat_net_send          = rdata_rva + (uint32_t)(IMP_NET_SEND * ptr_size);
    imp.iat_net_recv          = rdata_rva + (uint32_t)(IMP_NET_RECV * ptr_size);
    imp.iat_net_set_blocking  = rdata_rva + (uint32_t)(IMP_NET_SET_BLOCKING * ptr_size);
    imp.iat_net_set_timeout   = rdata_rva + (uint32_t)(IMP_NET_SET_TIMEOUT * ptr_size);
    imp.iat_net_resolve       = rdata_rva + (uint32_t)(IMP_NET_RESOLVE * ptr_size);

    int ok = ir_x64_codegen(m, text_rva, str_rva, str_n, fmt_rva, fmtf_rva, nl_rva, sp_rva,
                            &imp, ginfo, gcount, &text, &text_size, &entry_off);
    if(ok != 0) return -1;

    // append syscall stubs
    unsigned char stubs[16384];
    ElfStubInfo si;
    memset(&si, 0, sizeof(si));
    if(build_linux_stubs(stubs, sizeof(stubs), 1u, 60u, heap_cur_addr, heap_end_addr, heap_free_addr,
                         err_addr, stdin_addr, stdout_addr, stderr_addr, &si) != 0){
      return -1;
    }
    size_t text_base = text_size;
    unsigned char* new_text = (unsigned char*)realloc(text, text_size + (uint32_t)si.size);
    if(!new_text) return -1;
    text = new_text;
    memcpy(text + text_size, stubs, si.size);
    text_size += (uint32_t)si.size;

    // append entry stub that calls main then sys_exit
    {
      unsigned char entry_stub[128];
      size_t entry_sz = 0;
      uint64_t main_addr = base + (uint64_t)text_rva + (uint64_t)entry_off;
      if(build_linux_entry_stub(entry_stub, sizeof(entry_stub), main_addr, &entry_sz) != 0){
        return -1;
      }
      size_t entry_off_new = text_size;
      unsigned char* new_text2 = (unsigned char*)realloc(text, text_size + (uint32_t)entry_sz);
      if(!new_text2) return -1;
      text = new_text2;
      memcpy(text + text_size, entry_stub, entry_sz);
      text_size += (uint32_t)entry_sz;
      entry_off = entry_off_new;
    }

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

    // fill import table with absolute addresses
    uint64_t text_base_addr = base + (uint64_t)text_rva;
    put_u64_le(rdata + IMP_EXIT * ptr_size, text_base_addr + text_base + si.off_exit);
    put_u64_le(rdata + IMP_GETSTD * ptr_size, text_base_addr + text_base + si.off_getstd);
    put_u64_le(rdata + IMP_WRITE * ptr_size, text_base_addr + text_base + si.off_write);
    put_u64_le(rdata + IMP_LSTRLEN * ptr_size, text_base_addr + text_base + si.off_strlen);
    put_u64_le(rdata + IMP_LSTRCMP * ptr_size, text_base_addr + text_base + si.off_strcmp);
    put_u64_le(rdata + IMP_GETTIME * ptr_size, text_base_addr + text_base + si.off_gettime);
    put_u64_le(rdata + IMP_SLEEP * ptr_size, text_base_addr + text_base + si.off_sleep);
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
    put_u64_le(rdata + IMP_GETFILEATTR * ptr_size, text_base_addr + text_base + si.off_getfileattr);
    put_u64_le(rdata + IMP_CREATEDIR * ptr_size, text_base_addr + text_base + si.off_createdir);
    put_u64_le(rdata + IMP_REMOVEDIR * ptr_size, text_base_addr + text_base + si.off_removedir);
    put_u64_le(rdata + IMP_DELETEFILE * ptr_size, text_base_addr + text_base + si.off_deletefile);
    put_u64_le(rdata + IMP_MOVEFILE * ptr_size, text_base_addr + text_base + si.off_movefile);
    put_u64_le(rdata + IMP_LISTDIR * ptr_size, text_base_addr + text_base + si.off_list_dir);
    put_u64_le(rdata + IMP_PROCRUN * ptr_size, text_base_addr + text_base + si.off_proc_run);
    put_u64_le(rdata + IMP_PROCOUT * ptr_size, text_base_addr + text_base + si.off_proc_out);
    put_u64_le(rdata + IMP_NET_AF_INET * ptr_size, text_base_addr + text_base + si.off_net_af_inet);
    put_u64_le(rdata + IMP_NET_AF_INET6 * ptr_size, text_base_addr + text_base + si.off_net_af_inet6);
    put_u64_le(rdata + IMP_NET_SOCK_STREAM * ptr_size, text_base_addr + text_base + si.off_net_sock_stream);
    put_u64_le(rdata + IMP_NET_SOCK_DGRAM * ptr_size, text_base_addr + text_base + si.off_net_sock_dgram);
    put_u64_le(rdata + IMP_NET_IPPROTO_TCP * ptr_size, text_base_addr + text_base + si.off_net_ipproto_tcp);
    put_u64_le(rdata + IMP_NET_IPPROTO_UDP * ptr_size, text_base_addr + text_base + si.off_net_ipproto_udp);
    put_u64_le(rdata + IMP_NET_INIT * ptr_size, text_base_addr + text_base + si.off_net_init);
    put_u64_le(rdata + IMP_NET_CLEANUP * ptr_size, text_base_addr + text_base + si.off_net_cleanup);
    put_u64_le(rdata + IMP_NET_LAST_ERROR * ptr_size, text_base_addr + text_base + si.off_net_last_error);
    put_u64_le(rdata + IMP_NET_SOCKET * ptr_size, text_base_addr + text_base + si.off_net_socket);
    put_u64_le(rdata + IMP_NET_CLOSE * ptr_size, text_base_addr + text_base + si.off_net_close);
    put_u64_le(rdata + IMP_NET_CONNECT * ptr_size, text_base_addr + text_base + si.off_net_connect);
    put_u64_le(rdata + IMP_NET_BIND * ptr_size, text_base_addr + text_base + si.off_net_bind);
    put_u64_le(rdata + IMP_NET_LISTEN * ptr_size, text_base_addr + text_base + si.off_net_listen);
    put_u64_le(rdata + IMP_NET_ACCEPT * ptr_size, text_base_addr + text_base + si.off_net_accept);
    put_u64_le(rdata + IMP_NET_SEND * ptr_size, text_base_addr + text_base + si.off_net_send);
    put_u64_le(rdata + IMP_NET_RECV * ptr_size, text_base_addr + text_base + si.off_net_recv);
    put_u64_le(rdata + IMP_NET_SET_BLOCKING * ptr_size, text_base_addr + text_base + si.off_net_set_blocking);
    put_u64_le(rdata + IMP_NET_SET_TIMEOUT * ptr_size, text_base_addr + text_base + si.off_net_set_timeout);
    put_u64_le(rdata + IMP_NET_RESOLVE * ptr_size, text_base_addr + text_base + si.off_net_resolve);

    // ELF layout
    const uint32_t phoff = 64;
    const uint32_t ehsize = 64;
    const uint32_t phentsize = 56;
    const uint32_t phnum = 2;
    uint32_t text_off = page;
    uint32_t rdata_off = align_up_u32(text_off + text_size, page);
    uint32_t data_pad = data_rva - (rdata_rva + rdata_size);
    uint32_t data_off = rdata_off + rdata_size + data_pad;
    uint64_t entry = base + (uint64_t)text_rva + (uint64_t)entry_off;

    FILE* f = fopen(out_exe, "wb");
    if(!f) return -1;

    // ELF header
    unsigned char ident[16];
    memset(ident, 0, sizeof(ident));
    ident[0] = 0x7F; ident[1] = 'E'; ident[2] = 'L'; ident[3] = 'F';
    ident[4] = 2; // 64-bit
    ident[5] = 1; // little endian
    ident[6] = 1; // version
    fwrite(ident, 1, sizeof(ident), f);
    write_u16(f, 2);       // ET_EXEC
    write_u16(f, 0x3E);    // EM_X86_64
    write_u32(f, 1);       // version
    write_u64(f, entry);
    write_u64(f, phoff);
    write_u64(f, 0);
    write_u32(f, 0);
    write_u16(f, ehsize);
    write_u16(f, phentsize);
    write_u16(f, phnum);
    write_u16(f, 0);
    write_u16(f, 0);
    write_u16(f, 0);

    // Program header: TEXT (R|X)
    write_u32(f, 1);             // PT_LOAD
    write_u32(f, 5);             // R | X
    write_u64(f, text_off);
    write_u64(f, base + text_rva);
    write_u64(f, base + text_rva);
    write_u64(f, text_size);
    write_u64(f, text_size);
    write_u64(f, page);

    // Program header: DATA (R|W)
    write_u32(f, 1);             // PT_LOAD
    write_u32(f, 6);             // R | W
    write_u64(f, rdata_off);
    write_u64(f, base + rdata_rva);
    write_u64(f, base + rdata_rva);
    uint64_t rw_span = (uint64_t)rdata_size + (uint64_t)data_pad + (uint64_t)data_size;
    write_u64(f, rw_span);
    write_u64(f, rw_span);
    write_u64(f, page);

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
    // pad to data_off for aligned data_rva
    cur = ftell(f);
    while(cur < (long)data_off){
      fputc(0, f);
      cur++;
    }
    fwrite(data, 1, data_size, f);
    fclose(f);
#ifndef _WIN32
    // Ensure the output is executable on Unix hosts.
    chmod(out_exe, 0755);
#endif
    return 0;
  }

  return -1;
}

int ir_verify_elf_exe(const char* path){
  if(!path) return -1;
  FILE* f = fopen(path, "rb");
  if(!f) return -1;
  fseek(f, 0, SEEK_END);
  long fsz = ftell(f);
  if(fsz <= 0){ fclose(f); return -1; }
  fseek(f, 0, SEEK_SET);
  unsigned char hdr[64];
  size_t n = fread(hdr, 1, sizeof(hdr), f);
  if(n < 64){ fclose(f); return -1; }
  if(hdr[0] != 0x7F || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F'){ fclose(f); return -1; }
  if(hdr[4] != 2 || hdr[5] != 1){ fclose(f); return -1; }
  uint16_t mach = (uint16_t)hdr[18] | ((uint16_t)hdr[19] << 8);
  if(mach != 0x3E && mach != 0xB7){ fclose(f); return -1; }
  uint64_t entry = 0;
  memcpy(&entry, hdr + 24, 8);
  uint64_t phoff = 0;
  memcpy(&phoff, hdr + 32, 8);
  uint16_t phentsz = (uint16_t)hdr[54] | ((uint16_t)hdr[55] << 8);
  uint16_t phnum = (uint16_t)hdr[56] | ((uint16_t)hdr[57] << 8);
  if(phentsz < 56 || phnum == 0){ fclose(f); return -1; }
  if(phoff + (uint64_t)phentsz * (uint64_t)phnum > (uint64_t)fsz){ fclose(f); return -1; }
  if(fseek(f, (long)phoff, SEEK_SET) != 0){ fclose(f); return -1; }
  int entry_ok = 0;
  for(uint16_t i=0;i<phnum;i++){
    unsigned char ph[56];
    if(fread(ph, 1, 56, f) != 56){ fclose(f); return -1; }
    uint32_t p_type = (uint32_t)ph[0] | ((uint32_t)ph[1] << 8) | ((uint32_t)ph[2] << 16) | ((uint32_t)ph[3] << 24);
    if(p_type != 1) continue; // PT_LOAD
    uint32_t p_flags = (uint32_t)ph[4] | ((uint32_t)ph[5] << 8) | ((uint32_t)ph[6] << 16) | ((uint32_t)ph[7] << 24);
    uint64_t p_offset = 0, p_vaddr = 0, p_filesz = 0, p_memsz = 0;
    memcpy(&p_offset, ph + 8, 8);
    memcpy(&p_vaddr, ph + 16, 8);
    memcpy(&p_filesz, ph + 32, 8);
    memcpy(&p_memsz, ph + 40, 8);
    uint64_t p_align = 0;
    memcpy(&p_align, ph + 48, 8);
    if(p_offset + p_filesz > (uint64_t)fsz){ fclose(f); return -1; }
    if(p_memsz < p_filesz){ fclose(f); return -1; }
    if(p_align != 0){
      if((p_align & (p_align - 1)) != 0){ fclose(f); return -1; }
      if((p_offset % p_align) != (p_vaddr % p_align)){ fclose(f); return -1; }
    }
    if(entry >= p_vaddr && entry < (p_vaddr + p_memsz)){
      if(p_flags & 1) entry_ok = 1; // executable
    }
  }
  fclose(f);
  return entry_ok ? 0 : -1;
}
