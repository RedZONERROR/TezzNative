// src/ir_codegen_pe.c
#include "ir_codegen_pe.h"
#include "ir_codegen_arm64.h"
#include "util.h"
#include "host_gui_backend.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

extern long long tn_os_system(unsigned char*);
extern void* tn_mmap_file(unsigned char*, long long*);
extern void tn_munmap_file(void*, long long);
extern long long tn_io_pipe_create(long long*, long long*);
extern long long tn_io_fd_read(long long, unsigned char*, long long);
extern long long tn_io_fd_write(long long, unsigned char*, long long);
extern long long tn_io_fd_close(long long);
extern unsigned char* tn_getenv_str(const unsigned char*);
extern long long tn_color_is_tty_stdout(void);
extern long long tn_tts_play_pcm(long long* pcm, long long len, long long rate);
extern long long tn_stt_capture_mic(long long* pcm, long long n_samples, long long rate);

static uint32_t align_up_u32(uint32_t v, uint32_t a){
  return (v + (a - 1)) & ~(a - 1);
}

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

typedef struct {
  uint32_t vaddr;
  uint32_t vsize;
  uint32_t raw_ptr;
  uint32_t raw_size;
  uint32_t ch;
} PeSec;

static int pe_rva_to_off(const PeSec* secs, uint16_t nsect, uint32_t rva, uint32_t fsz, uint32_t* out_off){
  if(!secs || !out_off) return -1;
  for(uint16_t i=0; i<nsect; i++){
    uint32_t span = secs[i].vsize > secs[i].raw_size ? secs[i].vsize : secs[i].raw_size;
    if(rva >= secs[i].vaddr && rva < (secs[i].vaddr + span)){
      uint32_t off_in = secs[i].raw_ptr + (rva - secs[i].vaddr);
      if(off_in < fsz){
        *out_off = off_in;
        return 0;
      }
      return -1;
    }
  }
  return -1;
}

int ir_verify_pe_exe(const char* path){
  if(!path) return -1;
  FILE* f = fopen(path, "rb");
  if(!f) return -1;
  fseek(f, 0, SEEK_END);
  long fsz = ftell(f);
  if(fsz <= 0){ fclose(f); return -1; }
  fseek(f, 0, SEEK_SET);
  unsigned char* buf = (unsigned char*)malloc((size_t)fsz);
  if(!buf){ fclose(f); return -1; }
  size_t n = fread(buf, 1, (size_t)fsz, f);
  fclose(f);
  if(n < 0x200){ free(buf); return -1; }
  if(buf[0] != 'M' || buf[1] != 'Z'){ free(buf); return -1; }
  uint32_t pe_off = (uint32_t)buf[0x3C] |
                    ((uint32_t)buf[0x3D] << 8) |
                    ((uint32_t)buf[0x3E] << 16) |
                    ((uint32_t)buf[0x3F] << 24);
  if(pe_off + 4 + 20 >= n){ free(buf); return -1; }
  if(memcmp(buf + pe_off, "PE\0\0", 4) != 0){ free(buf); return -1; }
  uint16_t machine = (uint16_t)buf[pe_off + 4] | ((uint16_t)buf[pe_off + 5] << 8);
  if(machine != 0x8664 && machine != 0x014C && machine != 0xAA64){ free(buf); return -1; }
  uint16_t nsect = (uint16_t)buf[pe_off + 6] | ((uint16_t)buf[pe_off + 7] << 8);
  uint16_t opt_size = (uint16_t)buf[pe_off + 20] | ((uint16_t)buf[pe_off + 21] << 8);
  uint32_t opt_off = pe_off + 4 + 20;
  if(opt_off + opt_size > n){ free(buf); return -1; }
  uint16_t opt_magic = (uint16_t)buf[opt_off] | ((uint16_t)buf[opt_off + 1] << 8);
  if(opt_magic != 0x20B && opt_magic != 0x10B){ free(buf); return -1; }
  uint32_t ptr_size = (opt_magic == 0x20B) ? 8u : 4u;
  uint32_t entry_rva = (uint32_t)buf[opt_off + 0x10] |
                       ((uint32_t)buf[opt_off + 0x11] << 8) |
                       ((uint32_t)buf[opt_off + 0x12] << 16) |
                       ((uint32_t)buf[opt_off + 0x13] << 24);
  uint32_t dd_off = opt_off + ((opt_magic == 0x20B) ? 0x70 : 0x60);
  if(dd_off + 8*2 > n){ free(buf); return -1; }
  uint32_t imp_rva = (uint32_t)buf[dd_off + 8] |
                     ((uint32_t)buf[dd_off + 9] << 8) |
                     ((uint32_t)buf[dd_off +10] << 16) |
                     ((uint32_t)buf[dd_off +11] << 24);
  uint32_t imp_sz = (uint32_t)buf[dd_off + 12] |
                    ((uint32_t)buf[dd_off +13] << 8) |
                    ((uint32_t)buf[dd_off +14] << 16) |
                    ((uint32_t)buf[dd_off +15] << 24);
  uint32_t rel_rva = (uint32_t)buf[dd_off + 8*5 + 0] |
                     ((uint32_t)buf[dd_off + 8*5 + 1] << 8) |
                     ((uint32_t)buf[dd_off + 8*5 + 2] << 16) |
                     ((uint32_t)buf[dd_off + 8*5 + 3] << 24);
  uint32_t rel_sz = (uint32_t)buf[dd_off + 8*5 + 4] |
                    ((uint32_t)buf[dd_off + 8*5 + 5] << 8) |
                    ((uint32_t)buf[dd_off + 8*5 + 6] << 16) |
                    ((uint32_t)buf[dd_off + 8*5 + 7] << 24);
  uint32_t sect_off = opt_off + opt_size;
  if(sect_off + (uint32_t)nsect * 40u > n){ free(buf); return -1; }

  int entry_ok = 0;
  int imp_ok = (imp_rva == 0 || imp_sz == 0) ? 0 : -1;
  int rel_ok = (rel_rva == 0 || rel_sz == 0) ? 0 : -1;

  PeSec* secs = (PeSec*)calloc((size_t)nsect, sizeof(PeSec));
  if(!secs){ free(buf); return -1; }
  for(uint16_t i=0; i<nsect; i++){
    uint32_t off = sect_off + (uint32_t)i * 40u;
    uint32_t vsize = (uint32_t)buf[off + 8] |
                     ((uint32_t)buf[off + 9] << 8) |
                     ((uint32_t)buf[off +10] << 16) |
                     ((uint32_t)buf[off +11] << 24);
    uint32_t vaddr = (uint32_t)buf[off +12] |
                     ((uint32_t)buf[off +13] << 8) |
                     ((uint32_t)buf[off +14] << 16) |
                     ((uint32_t)buf[off +15] << 24);
    uint32_t raw_size = (uint32_t)buf[off +16] |
                        ((uint32_t)buf[off +17] << 8) |
                        ((uint32_t)buf[off +18] << 16) |
                        ((uint32_t)buf[off +19] << 24);
    uint32_t raw_ptr = (uint32_t)buf[off +20] |
                       ((uint32_t)buf[off +21] << 8) |
                       ((uint32_t)buf[off +22] << 16) |
                       ((uint32_t)buf[off +23] << 24);
    uint32_t ch = (uint32_t)buf[off +36] |
                  ((uint32_t)buf[off +37] << 8) |
                  ((uint32_t)buf[off +38] << 16) |
                  ((uint32_t)buf[off +39] << 24);
    if(raw_size && (uint64_t)raw_ptr + (uint64_t)raw_size > (uint64_t)n){ free(buf); return -1; }

    secs[i].vaddr = vaddr;
    secs[i].vsize = vsize;
    secs[i].raw_ptr = raw_ptr;
    secs[i].raw_size = raw_size;
    secs[i].ch = ch;

    uint32_t span = vsize > raw_size ? vsize : raw_size;
    if(entry_rva >= vaddr && entry_rva < (vaddr + span)){
      if(ch & 0x20000000u) entry_ok = 1; // executable
    }
    if(imp_rva >= vaddr && imp_rva < (vaddr + span)){
      uint32_t off_in = raw_ptr + (imp_rva - vaddr);
      if((uint64_t)off_in + (uint64_t)imp_sz <= (uint64_t)n) imp_ok = 0;
    }
    if(rel_rva >= vaddr && rel_rva < (vaddr + span)){
      uint32_t off_in = raw_ptr + (rel_rva - vaddr);
      if((uint64_t)off_in + (uint64_t)rel_sz <= (uint64_t)n) rel_ok = 0;
    }
  }

  if(imp_rva != 0 && imp_sz != 0){
    uint32_t imp_off = 0;
    if(pe_rva_to_off(secs, nsect, imp_rva, (uint32_t)n, &imp_off) != 0){ free(secs); free(buf); return -1; }
    if((uint64_t)imp_off + (uint64_t)imp_sz > (uint64_t)n){ free(secs); free(buf); return -1; }
    int have_desc = 0;
    uint32_t cur = imp_off;
    uint32_t max = imp_off + imp_sz;
    for(int d=0; cur + 20 <= max && d < 64; d++){
      uint32_t orig = (uint32_t)buf[cur] | ((uint32_t)buf[cur+1] << 8) |
                      ((uint32_t)buf[cur+2] << 16) | ((uint32_t)buf[cur+3] << 24);
      uint32_t name = (uint32_t)buf[cur+12] | ((uint32_t)buf[cur+13] << 8) |
                      ((uint32_t)buf[cur+14] << 16) | ((uint32_t)buf[cur+15] << 24);
      uint32_t first = (uint32_t)buf[cur+16] | ((uint32_t)buf[cur+17] << 8) |
                       ((uint32_t)buf[cur+18] << 16) | ((uint32_t)buf[cur+19] << 24);
      if(orig == 0 && name == 0 && first == 0){
        break;
      }
      have_desc = 1;
      uint32_t name_off = 0;
      if(pe_rva_to_off(secs, nsect, name, (uint32_t)n, &name_off) != 0){ free(secs); free(buf); return -1; }
      // ensure null-terminated import name
      uint32_t t = name_off;
      while(t < n && buf[t] != 0) t++;
      if(t >= n){ free(secs); free(buf); return -1; }

      uint32_t thunk_rva = orig ? orig : first;
      uint32_t thunk_off = 0;
      if(pe_rva_to_off(secs, nsect, thunk_rva, (uint32_t)n, &thunk_off) != 0){ free(secs); free(buf); return -1; }
      // validate a small number of thunks until 0
      for(int ti=0; ti<256; ti++){
        if(thunk_off + ptr_size > n){ free(secs); free(buf); return -1; }
        uint64_t thunk = 0;
        if(ptr_size == 8){
          thunk = (uint64_t)buf[thunk_off] |
                  ((uint64_t)buf[thunk_off+1] << 8) |
                  ((uint64_t)buf[thunk_off+2] << 16) |
                  ((uint64_t)buf[thunk_off+3] << 24) |
                  ((uint64_t)buf[thunk_off+4] << 32) |
                  ((uint64_t)buf[thunk_off+5] << 40) |
                  ((uint64_t)buf[thunk_off+6] << 48) |
                  ((uint64_t)buf[thunk_off+7] << 56);
        } else {
          thunk = (uint64_t)buf[thunk_off] |
                  ((uint64_t)buf[thunk_off+1] << 8) |
                  ((uint64_t)buf[thunk_off+2] << 16) |
                  ((uint64_t)buf[thunk_off+3] << 24);
        }
        if(thunk == 0) break;
        if(ptr_size == 8){
          if(thunk & (1ULL << 63)){
            // ordinal import, skip
          } else {
            uint32_t hn_rva = (uint32_t)thunk;
            uint32_t hn_off = 0;
            if(pe_rva_to_off(secs, nsect, hn_rva, (uint32_t)n, &hn_off) != 0){ free(secs); free(buf); return -1; }
            if(hn_off + 2 >= n){ free(secs); free(buf); return -1; }
            uint32_t s = hn_off + 2;
            while(s < n && buf[s] != 0) s++;
            if(s >= n){ free(secs); free(buf); return -1; }
          }
        } else {
          if(thunk & 0x80000000u){
            // ordinal import, skip
          } else {
            uint32_t hn_rva = (uint32_t)thunk;
            uint32_t hn_off = 0;
            if(pe_rva_to_off(secs, nsect, hn_rva, (uint32_t)n, &hn_off) != 0){ free(secs); free(buf); return -1; }
            if(hn_off + 2 >= n){ free(secs); free(buf); return -1; }
            uint32_t s = hn_off + 2;
            while(s < n && buf[s] != 0) s++;
            if(s >= n){ free(secs); free(buf); return -1; }
          }
        }
        thunk_off += ptr_size;
      }
      cur += 20;
    }
    if(!have_desc){ free(secs); free(buf); return -1; }
  }

  free(secs);
  free(buf);
  if(!entry_ok) return -1;
  if(imp_ok != 0) return -1;
  if(rel_ok != 0) return -1;
  return 0;
}

static int build_pe_imports(unsigned char* rdata, uint32_t rdata_rva, uint32_t ptr_size,
                            PeImportInfo* out){
  if(!rdata || !out) return -1;
  if(ptr_size != 4 && ptr_size != 8) return -1;

  const char* k_dll = "kernel32.dll";
  const char* k_funcs[] = {
    "ExitProcess",                          // 0
    "GetStdHandle",                         // 1
    "WriteFile",                            // 2
    "lstrlenA",                             // 3
    "lstrcmpA",                             // 4
    "GetSystemTimePreciseAsFileTime",        // 5
    "VirtualAlloc",                         // 6
    "VirtualFree",                          // 7
    "CreateFileA",                          // 8
    "CloseHandle",                          // 9
    "DeleteFileA",                          // 10
    "MoveFileA",                            // 11
    "GetFileSizeEx",                        // 12
    "ReadFile",                             // 13
    "Sleep",                                // 14
    "GetFileAttributesA",                   // 15
    "CreateDirectoryA",                     // 16
    "RemoveDirectoryA",                     // 17
    "GetCurrentDirectoryA",                 // 18
    "CreateFileMappingA",                   // 19
    "MapViewOfFile",                        // 20
    "UnmapViewOfFile",                      // 21
    "CreateThread",                         // 22
    "WaitForSingleObject",                  // 23
    "FindFirstFileA",                       // 24
    "FindNextFileA",                        // 25
    "FindClose"                             // 26
  };
  const int k_n = 27;
  const char* u_dll = "user32.dll";
  const char* u_funcs[] = {"wsprintfA"};
  const int u_n = 1;
  const char* m_dll = "msvcrt.dll";
  const char* m_funcs[] = {
    "sprintf",
    "memcpy",
    "memmove",
    "memcmp",
    "system",
    "putchar",
    "getchar",
    "realloc"
  };
  const int m_n = 8;
  // UCRT: file I/O needed by io module
  const char* c_dll = "ucrtbase.dll";
  const char* c_funcs[] = {
    "fopen",        // 0
    "fclose",       // 1
    "fread",        // 2
    "fwrite",       // 3
    "fseek",        // 4
    "ftell",        // 5
    "fflush",       // 6
    "remove",       // 7
    "rename",       // 8
    "fgets",        // 9
    "ferror",       // 10
    "feof",         // 11
    "getenv",       // 12
    "_popen",       // 13
    "_pclose"       // 14
  };
  const int c_n = 15;
  const char* w_dll = "WS2_32.dll";
  const char* w_funcs[] = {
    "WSAStartup",       // 0
    "WSACleanup",       // 1
    "WSAGetLastError",  // 2
    "socket",           // 3
    "closesocket",      // 4
    "connect",          // 5
    "bind",             // 6
    "listen",           // 7
    "accept",           // 8
    "send",             // 9
    "recv",             // 10
    "ioctlsocket",      // 11
    "setsockopt"        // 12
  };
  const int w_n = 13;

  const int ndll = 5;
  const uint32_t desc_size = 20u * (uint32_t)(ndll + 1);
  uint32_t cur = desc_size;

  uint32_t ilt_off[5];
  uint32_t iat_off[5];
  uint32_t dll_off[5];
  uint32_t hint_off_k[27];
  uint32_t hint_off_u[1];
  uint32_t hint_off_m[8];
  uint32_t hint_off_c[15];
  uint32_t hint_off_w[13];

  ilt_off[0] = cur; cur += (uint32_t)ptr_size * (uint32_t)(k_n + 1);
  ilt_off[1] = cur; cur += (uint32_t)ptr_size * (uint32_t)(u_n + 1);
  ilt_off[2] = cur; cur += (uint32_t)ptr_size * (uint32_t)(m_n + 1);
  ilt_off[3] = cur; cur += (uint32_t)ptr_size * (uint32_t)(c_n + 1);
  ilt_off[4] = cur; cur += (uint32_t)ptr_size * (uint32_t)(w_n + 1);

  iat_off[0] = cur; cur += (uint32_t)ptr_size * (uint32_t)(k_n + 1);
  iat_off[1] = cur; cur += (uint32_t)ptr_size * (uint32_t)(u_n + 1);
  iat_off[2] = cur; cur += (uint32_t)ptr_size * (uint32_t)(m_n + 1);
  iat_off[3] = cur; cur += (uint32_t)ptr_size * (uint32_t)(c_n + 1);
  iat_off[4] = cur; cur += (uint32_t)ptr_size * (uint32_t)(w_n + 1);

  for(int i=0;i<k_n;i++){
    if(cur & 1u) cur++;
    hint_off_k[i] = cur;
    cur += 2 + (uint32_t)strlen(k_funcs[i]) + 1;
  }
  for(int i=0;i<u_n;i++){
    if(cur & 1u) cur++;
    hint_off_u[i] = cur;
    cur += 2 + (uint32_t)strlen(u_funcs[i]) + 1;
  }
  for(int i=0;i<m_n;i++){
    if(cur & 1u) cur++;
    hint_off_m[i] = cur;
    cur += 2 + (uint32_t)strlen(m_funcs[i]) + 1;
  }
  for(int i=0;i<c_n;i++){
    if(cur & 1u) cur++;
    hint_off_c[i] = cur;
    cur += 2 + (uint32_t)strlen(c_funcs[i]) + 1;
  }
  for(int i=0;i<w_n;i++){
    if(cur & 1u) cur++;
    hint_off_w[i] = cur;
    cur += 2 + (uint32_t)strlen(w_funcs[i]) + 1;
  }

  if(cur & 1u) cur++;
  dll_off[0] = cur; cur += (uint32_t)strlen(k_dll) + 1;
  if(cur & 1u) cur++;
  dll_off[1] = cur; cur += (uint32_t)strlen(u_dll) + 1;
  if(cur & 1u) cur++;
  dll_off[2] = cur; cur += (uint32_t)strlen(m_dll) + 1;
  if(cur & 1u) cur++;
  dll_off[3] = cur; cur += (uint32_t)strlen(c_dll) + 1;
  if(cur & 1u) cur++;
  dll_off[4] = cur; cur += (uint32_t)strlen(w_dll) + 1;

  memset(rdata, 0, cur);

  // Descriptor 0: kernel32
  put_u32_le(rdata + 0 + 0, rdata_rva + ilt_off[0]);
  put_u32_le(rdata + 0 + 12, rdata_rva + dll_off[0]);
  put_u32_le(rdata + 0 + 16, rdata_rva + iat_off[0]);
  // Descriptor 1: user32
  put_u32_le(rdata + 20 + 0, rdata_rva + ilt_off[1]);
  put_u32_le(rdata + 20 + 12, rdata_rva + dll_off[1]);
  put_u32_le(rdata + 20 + 16, rdata_rva + iat_off[1]);
  // Descriptor 2: msvcrt
  put_u32_le(rdata + 40 + 0, rdata_rva + ilt_off[2]);
  put_u32_le(rdata + 40 + 12, rdata_rva + dll_off[2]);
  put_u32_le(rdata + 40 + 16, rdata_rva + iat_off[2]);
  // Descriptor 3: ucrtbase
  put_u32_le(rdata + 60 + 0, rdata_rva + ilt_off[3]);
  put_u32_le(rdata + 60 + 12, rdata_rva + dll_off[3]);
  put_u32_le(rdata + 60 + 16, rdata_rva + iat_off[3]);
  // Descriptor 4: ws2_32
  put_u32_le(rdata + 80 + 0, rdata_rva + ilt_off[4]);
  put_u32_le(rdata + 80 + 12, rdata_rva + dll_off[4]);
  put_u32_le(rdata + 80 + 16, rdata_rva + iat_off[4]);

  // ILT/IAT kernel32
  for(int i=0;i<k_n;i++){
    uint32_t hrva = rdata_rva + hint_off_k[i];
    if(ptr_size == 8){
      put_u64_le(rdata + ilt_off[0] + (uint32_t)ptr_size * (uint32_t)i, (uint64_t)hrva);
      put_u64_le(rdata + iat_off[0] + (uint32_t)ptr_size * (uint32_t)i, (uint64_t)hrva);
    } else {
      put_u32_le(rdata + ilt_off[0] + (uint32_t)ptr_size * (uint32_t)i, hrva);
      put_u32_le(rdata + iat_off[0] + (uint32_t)ptr_size * (uint32_t)i, hrva);
    }
  }
  // ILT/IAT user32
  for(int i=0;i<u_n;i++){
    uint32_t hrva = rdata_rva + hint_off_u[i];
    if(ptr_size == 8){
      put_u64_le(rdata + ilt_off[1] + (uint32_t)ptr_size * (uint32_t)i, (uint64_t)hrva);
      put_u64_le(rdata + iat_off[1] + (uint32_t)ptr_size * (uint32_t)i, (uint64_t)hrva);
    } else {
      put_u32_le(rdata + ilt_off[1] + (uint32_t)ptr_size * (uint32_t)i, hrva);
      put_u32_le(rdata + iat_off[1] + (uint32_t)ptr_size * (uint32_t)i, hrva);
    }
  }
  // ILT/IAT msvcrt
  for(int i=0;i<m_n;i++){
    uint32_t hrva = rdata_rva + hint_off_m[i];
    if(ptr_size == 8){
      put_u64_le(rdata + ilt_off[2] + (uint32_t)ptr_size * (uint32_t)i, (uint64_t)hrva);
      put_u64_le(rdata + iat_off[2] + (uint32_t)ptr_size * (uint32_t)i, (uint64_t)hrva);
    } else {
      put_u32_le(rdata + ilt_off[2] + (uint32_t)ptr_size * (uint32_t)i, hrva);
      put_u32_le(rdata + iat_off[2] + (uint32_t)ptr_size * (uint32_t)i, hrva);
    }
  }
  // ILT/IAT ucrtbase
  for(int i=0;i<c_n;i++){
    uint32_t hrva = rdata_rva + hint_off_c[i];
    if(ptr_size == 8){
      put_u64_le(rdata + ilt_off[3] + (uint32_t)ptr_size * (uint32_t)i, (uint64_t)hrva);
      put_u64_le(rdata + iat_off[3] + (uint32_t)ptr_size * (uint32_t)i, (uint64_t)hrva);
    } else {
      put_u32_le(rdata + ilt_off[3] + (uint32_t)ptr_size * (uint32_t)i, hrva);
      put_u32_le(rdata + iat_off[3] + (uint32_t)ptr_size * (uint32_t)i, hrva);
    }
  }
  // ILT/IAT ws2_32
  for(int i=0;i<w_n;i++){
    uint32_t hrva = rdata_rva + hint_off_w[i];
    if(ptr_size == 8){
      put_u64_le(rdata + ilt_off[4] + (uint32_t)ptr_size * (uint32_t)i, (uint64_t)hrva);
      put_u64_le(rdata + iat_off[4] + (uint32_t)ptr_size * (uint32_t)i, (uint64_t)hrva);
    } else {
      put_u32_le(rdata + ilt_off[4] + (uint32_t)ptr_size * (uint32_t)i, hrva);
      put_u32_le(rdata + iat_off[4] + (uint32_t)ptr_size * (uint32_t)i, hrva);
    }
  }

  // Hint/name strings
  for(int i=0;i<k_n;i++){
    rdata[hint_off_k[i] + 0] = 0;
    rdata[hint_off_k[i] + 1] = 0;
    memcpy(rdata + hint_off_k[i] + 2, k_funcs[i], strlen(k_funcs[i]) + 1);
  }
  for(int i=0;i<u_n;i++){
    rdata[hint_off_u[i] + 0] = 0;
    rdata[hint_off_u[i] + 1] = 0;
    memcpy(rdata + hint_off_u[i] + 2, u_funcs[i], strlen(u_funcs[i]) + 1);
  }
  for(int i=0;i<m_n;i++){
    rdata[hint_off_m[i] + 0] = 0;
    rdata[hint_off_m[i] + 1] = 0;
    memcpy(rdata + hint_off_m[i] + 2, m_funcs[i], strlen(m_funcs[i]) + 1);
  }
  for(int i=0;i<c_n;i++){
    rdata[hint_off_c[i] + 0] = 0;
    rdata[hint_off_c[i] + 1] = 0;
    memcpy(rdata + hint_off_c[i] + 2, c_funcs[i], strlen(c_funcs[i]) + 1);
  }
  for(int i=0;i<w_n;i++){
    rdata[hint_off_w[i] + 0] = 0;
    rdata[hint_off_w[i] + 1] = 0;
    memcpy(rdata + hint_off_w[i] + 2, w_funcs[i], strlen(w_funcs[i]) + 1);
  }
  memcpy(rdata + dll_off[0], k_dll, strlen(k_dll) + 1);
  memcpy(rdata + dll_off[1], u_dll, strlen(u_dll) + 1);
  memcpy(rdata + dll_off[2], m_dll, strlen(m_dll) + 1);
  memcpy(rdata + dll_off[3], c_dll, strlen(c_dll) + 1);
  memcpy(rdata + dll_off[4], w_dll, strlen(w_dll) + 1);
  out->import_size = cur;
  out->iat_exit    = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 0u;
  out->iat_getstd  = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 1u;
  out->iat_write   = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 2u;
  out->iat_lstrlen = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 3u;
  out->iat_lstrcmp = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 4u;
  out->iat_gettime = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 5u;
  out->iat_valloc  = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 6u;
  out->iat_vfree   = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 7u;
  out->iat_createfile = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 8u;
  out->iat_closehandle= rdata_rva + iat_off[0] + (uint32_t)ptr_size * 9u;
  out->iat_deletefile = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 10u;
  out->iat_movefile   = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 11u;
  out->iat_getfilesize= rdata_rva + iat_off[0] + (uint32_t)ptr_size * 12u;
  out->iat_readfile   = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 13u;
  out->iat_sleep      = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 14u;
  out->iat_getfileattr= rdata_rva + iat_off[0] + (uint32_t)ptr_size * 15u;
  out->iat_createdir  = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 16u;
  out->iat_removedir  = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 17u;
  out->iat_getcurdir  = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 18u;
  out->iat_createfilemap = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 19u;
  out->iat_mapview       = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 20u;
  out->iat_unmapview     = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 21u;
  out->iat_createthread   = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 22u;
  out->iat_waitforsingleobject = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 23u;
  out->iat_findfirstfile = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 24u;
  out->iat_findnextfile  = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 25u;
  out->iat_findclose     = rdata_rva + iat_off[0] + (uint32_t)ptr_size * 26u;
  out->iat_wsprintf   = rdata_rva + iat_off[1] + (uint32_t)ptr_size * 0u;
  out->iat_sprintf = rdata_rva + iat_off[2] + (uint32_t)ptr_size * 0u;
  out->iat_memcpy  = rdata_rva + iat_off[2] + (uint32_t)ptr_size * 1u;
  out->iat_memmove = rdata_rva + iat_off[2] + (uint32_t)ptr_size * 2u;
  out->iat_memcmp  = rdata_rva + iat_off[2] + (uint32_t)ptr_size * 3u;
  out->iat_system  = rdata_rva + iat_off[2] + (uint32_t)ptr_size * 4u;
  out->iat_putchar = rdata_rva + iat_off[2] + (uint32_t)ptr_size * 5u;
  out->iat_getchar = rdata_rva + iat_off[2] + (uint32_t)ptr_size * 6u;
  out->iat_realloc = rdata_rva + iat_off[2] + (uint32_t)ptr_size * 7u;
  out->iat_getenv  = rdata_rva + iat_off[3] + (uint32_t)ptr_size * 12u;
  out->iat_popen   = rdata_rva + iat_off[3] + (uint32_t)ptr_size * 13u;
  out->iat_pclose  = rdata_rva + iat_off[3] + (uint32_t)ptr_size * 14u;
  // ucrtbase.dll: fopen=0,fclose=1,fread=2,fwrite=3,fseek=4,ftell=5,fflush=6,remove=7,rename=8,fgets=9,ferror=10,feof=11,getenv=12,_popen=13,_pclose=14
  out->iat_fopen  = rdata_rva + iat_off[3] + (uint32_t)ptr_size * 0u;
  out->iat_fclose = rdata_rva + iat_off[3] + (uint32_t)ptr_size * 1u;
  out->iat_fread  = rdata_rva + iat_off[3] + (uint32_t)ptr_size * 2u;
  out->iat_fwrite = rdata_rva + iat_off[3] + (uint32_t)ptr_size * 3u;
  out->iat_fseek  = rdata_rva + iat_off[3] + (uint32_t)ptr_size * 4u;
  out->iat_ftell  = rdata_rva + iat_off[3] + (uint32_t)ptr_size * 5u;
  out->iat_fflush = rdata_rva + iat_off[3] + (uint32_t)ptr_size * 6u;
  out->iat_remove = rdata_rva + iat_off[3] + (uint32_t)ptr_size * 7u;
  out->iat_rename = rdata_rva + iat_off[3] + (uint32_t)ptr_size * 8u;
  out->iat_io_err  = rdata_rva + iat_off[3] + (uint32_t)ptr_size * 10u; // ferror
  out->iat_io_eof  = rdata_rva + iat_off[3] + (uint32_t)ptr_size * 11u; // feof
  // read_line/write_line/stdin/stdout/stderr not directly in IAT (handled via runtime)
  out->iat_read_line  = 0;
  out->iat_read_bytes = 0;
  out->iat_write_line = 0;
  out->iat_stdin  = 0;
  out->iat_stdout = 0;
  out->iat_stderr = 0;
  out->iat_wsa_startup     = rdata_rva + iat_off[4] + (uint32_t)ptr_size * 0u;
  out->iat_wsa_cleanup     = rdata_rva + iat_off[4] + (uint32_t)ptr_size * 1u;
  out->iat_wsa_last_error  = rdata_rva + iat_off[4] + (uint32_t)ptr_size * 2u;
  out->iat_wsa_socket      = rdata_rva + iat_off[4] + (uint32_t)ptr_size * 3u;
  out->iat_wsa_closesocket = rdata_rva + iat_off[4] + (uint32_t)ptr_size * 4u;
  out->iat_wsa_connect     = rdata_rva + iat_off[4] + (uint32_t)ptr_size * 5u;
  out->iat_wsa_bind        = rdata_rva + iat_off[4] + (uint32_t)ptr_size * 6u;
  out->iat_wsa_listen      = rdata_rva + iat_off[4] + (uint32_t)ptr_size * 7u;
  out->iat_wsa_accept      = rdata_rva + iat_off[4] + (uint32_t)ptr_size * 8u;
  out->iat_wsa_send        = rdata_rva + iat_off[4] + (uint32_t)ptr_size * 9u;
  out->iat_wsa_recv        = rdata_rva + iat_off[4] + (uint32_t)ptr_size * 10u;
  out->iat_wsa_ioctlsocket = rdata_rva + iat_off[4] + (uint32_t)ptr_size * 11u;
  out->iat_wsa_setsockopt  = rdata_rva + iat_off[4] + (uint32_t)ptr_size * 12u;
  return (int)cur;
}

typedef struct {
  unsigned char* data;
  size_t len;
  size_t cap;
} CodeBuf;

static void cb_init(CodeBuf* cb, size_t cap){
  cb->len = 0;
  cb->cap = cap ? cap : 256;
  cb->data = (unsigned char*)malloc(cb->cap);
}
static void cb_free(CodeBuf* cb){
  free(cb->data);
  cb->data = NULL;
  cb->len = cb->cap = 0;
}
static int ensure_iat(uint32_t iat, const char* name){
  if(iat != 0) return 1;
  fprintf(stderr, "buildexe: '%s' not supported for this target\n", name ? name : "?");
  return 0;
}
static void cb_reserve(CodeBuf* cb, size_t extra){
  if(cb->len + extra <= cb->cap) return;
  size_t ncap = cb->cap ? cb->cap * 2 : 256;
  while(ncap < cb->len + extra) ncap *= 2;
  cb->data = (unsigned char*)realloc(cb->data, ncap);
  cb->cap = ncap;
}
static void cb_emit1(CodeBuf* cb, unsigned char v){
  cb_reserve(cb, 1);
  cb->data[cb->len++] = v;
}
static void cb_emit4(CodeBuf* cb, uint32_t v){
  cb_reserve(cb, 4);
  cb->data[cb->len++] = (unsigned char)(v & 0xFF);
  cb->data[cb->len++] = (unsigned char)((v >> 8) & 0xFF);
  cb->data[cb->len++] = (unsigned char)((v >> 16) & 0xFF);
  cb->data[cb->len++] = (unsigned char)((v >> 24) & 0xFF);
}
static void cb_emit8(CodeBuf* cb, uint64_t v){
  cb_reserve(cb, 8);
  cb->data[cb->len++] = (unsigned char)(v & 0xFF);
  cb->data[cb->len++] = (unsigned char)((v >> 8) & 0xFF);
  cb->data[cb->len++] = (unsigned char)((v >> 16) & 0xFF);
  cb->data[cb->len++] = (unsigned char)((v >> 24) & 0xFF);
  cb->data[cb->len++] = (unsigned char)((v >> 32) & 0xFF);
  cb->data[cb->len++] = (unsigned char)((v >> 40) & 0xFF);
  cb->data[cb->len++] = (unsigned char)((v >> 48) & 0xFF);
  cb->data[cb->len++] = (unsigned char)((v >> 56) & 0xFF);
}
static void cb_emit(CodeBuf* cb, const void* data, size_t n){
  cb_reserve(cb, n);
  memcpy(cb->data + cb->len, data, n);
  cb->len += n;
}
static void cb_patch4(CodeBuf* cb, size_t pos, uint32_t v){
  if(pos + 4 > cb->len) return;
  cb->data[pos + 0] = (unsigned char)(v & 0xFF);
  cb->data[pos + 1] = (unsigned char)((v >> 8) & 0xFF);
  cb->data[pos + 2] = (unsigned char)((v >> 16) & 0xFF);
  cb->data[pos + 3] = (unsigned char)((v >> 24) & 0xFF);
}

typedef struct {
  uint32_t* items;
  int n, cap;
} RelocList;

static void reloc_init(RelocList* r){
  r->items = NULL;
  r->n = r->cap = 0;
}
static void reloc_free(RelocList* r){
  free(r->items);
  r->items = NULL;
  r->n = r->cap = 0;
}
static void reloc_add(RelocList* r, uint32_t rva){
  if(!r) return;
  if(r->n == r->cap){
    r->cap = r->cap ? r->cap * 2 : 64;
    r->items = (uint32_t*)realloc(r->items, sizeof(uint32_t)*(size_t)r->cap);
    if(!r->items) die("out of memory");
  }
  r->items[r->n++] = rva;
}
static int cmp_u32(const void* a, const void* b){
  uint32_t x = *(const uint32_t*)a;
  uint32_t y = *(const uint32_t*)b;
  return (x < y) ? -1 : (x > y ? 1 : 0);
}
static int build_reloc_section(RelocList* r, unsigned char** out, uint32_t* out_size){
  if(!out || !out_size) return -1;
  *out = NULL;
  *out_size = 0;
  if(!r || r->n == 0) return 0;
  qsort(r->items, (size_t)r->n, sizeof(uint32_t), cmp_u32);
  // conservative size: 8 bytes per page + 2 bytes per entry + padding
  size_t cap = (size_t)r->n * 2 + (size_t)r->n * 2 + 64;
  unsigned char* buf = (unsigned char*)malloc(cap);
  if(!buf) return -1;
  size_t off = 0;
  int i = 0;
  while(i < r->n){
    uint32_t page = r->items[i] & 0xFFFFF000u;
    size_t block_start = off;
    if(off + 8 > cap){ free(buf); return -1; }
    put_u32_le(buf + off, page); off += 4;
    size_t size_pos = off; off += 4;
    int count = 0;
    while(i < r->n && (r->items[i] & 0xFFFFF000u) == page){
      uint16_t entry = (uint16_t)((3u << 12) | (r->items[i] & 0xFFFu)); // HIGHLOW
      if(off + 2 > cap){ free(buf); return -1; }
      buf[off++] = (unsigned char)(entry & 0xFF);
      buf[off++] = (unsigned char)((entry >> 8) & 0xFF);
      count++;
      i++;
    }
    if(count & 1){
      if(off + 2 > cap){ free(buf); return -1; }
      buf[off++] = 0;
      buf[off++] = 0;
    }
    uint32_t block_size = (uint32_t)(off - block_start);
    put_u32_le(buf + size_pos, block_size);
  }
  *out = buf;
  *out_size = (uint32_t)off;
  return 0;
}

static void emit_rex(CodeBuf* cb, int w, int r, int x, int b){
  unsigned char rex = 0x40;
  if(w) rex |= 0x08;
  if(r) rex |= 0x04;
  if(x) rex |= 0x02;
  if(b) rex |= 0x01;
  cb_emit1(cb, rex);
}

static void emit_modrm(CodeBuf* cb, int mod, int reg, int rm){
  cb_emit1(cb, (unsigned char)((mod<<6) | ((reg&7)<<3) | (rm&7)));
}

static void emit_mov_reg_imm64(CodeBuf* cb, int reg, uint64_t imm){
  emit_rex(cb, 1, 0, 0, (reg>>3)&1);
  cb_emit1(cb, (unsigned char)(0xB8 + (reg & 7)));
  cb_emit8(cb, imm);
}

static void emit_mov_rr(CodeBuf* cb, int dst, int src){
  emit_rex(cb, 1, (src>>3)&1, 0, (dst>>3)&1);
  cb_emit1(cb, 0x89);
  emit_modrm(cb, 3, src, dst);
}

static void emit_mov_reg_membp(CodeBuf* cb, int reg, int disp){
  int disp8 = (-disp >= -128 && -disp <= 127);
  emit_rex(cb, 1, (reg>>3)&1, 0, 0);
  cb_emit1(cb, 0x8B);
  emit_modrm(cb, disp8 ? 1 : 2, reg, 5);
  if(disp8) cb_emit1(cb, (unsigned char)(-disp));
  else cb_emit4(cb, (uint32_t)(-(int32_t)disp));
}

static void emit_mov_membp_reg(CodeBuf* cb, int disp, int reg){
  int disp8 = (-disp >= -128 && -disp <= 127);
  emit_rex(cb, 1, (reg>>3)&1, 0, 0);
  cb_emit1(cb, 0x89);
  emit_modrm(cb, disp8 ? 1 : 2, reg, 5);
  if(disp8) cb_emit1(cb, (unsigned char)(-disp));
  else cb_emit4(cb, (uint32_t)(-(int32_t)disp));
}

static void emit_movsd_xmm_membp(CodeBuf* cb, int xmm, int disp){
  int disp8 = (-disp >= -128 && -disp <= 127);
  cb_emit(cb, "\xF2", 1);
  emit_rex(cb, 0, (xmm>>3)&1, 0, 0);
  cb_emit1(cb, 0x0F);
  cb_emit1(cb, 0x10);
  emit_modrm(cb, disp8 ? 1 : 2, xmm, 5);
  if(disp8) cb_emit1(cb, (unsigned char)(-disp));
  else cb_emit4(cb, (uint32_t)(-(int32_t)disp));
}

static void emit_movsd_membp_xmm(CodeBuf* cb, int disp, int xmm){
  int disp8 = (-disp >= -128 && -disp <= 127);
  cb_emit(cb, "\xF2", 1);
  emit_rex(cb, 0, (xmm>>3)&1, 0, 0);
  cb_emit1(cb, 0x0F);
  cb_emit1(cb, 0x11);
  emit_modrm(cb, disp8 ? 1 : 2, xmm, 5);
  if(disp8) cb_emit1(cb, (unsigned char)(-disp));
  else cb_emit4(cb, (uint32_t)(-(int32_t)disp));
}

static void emit_movq_xmm_reg(CodeBuf* cb, int xmm, int reg){
  cb_emit(cb, "\x66", 1);
  emit_rex(cb, 1, (xmm>>3)&1, 0, (reg>>3)&1);
  cb_emit(cb, "\x0F\x6E", 2);
  emit_modrm(cb, 3, xmm, reg);
}

static void emit_movq_reg_xmm(CodeBuf* cb, int reg, int xmm){
  cb_emit(cb, "\x66", 1);
  emit_rex(cb, 1, (xmm>>3)&1, 0, (reg>>3)&1);
  cb_emit(cb, "\x0F\x7E", 2);
  emit_modrm(cb, 3, xmm, reg);
}

static void emit_movsd_rsp_xmm(CodeBuf* cb, int disp, int xmm){
  // movsd [rsp+disp], xmm
  cb_emit(cb, "\xF2", 1);
  emit_rex(cb, 0, (xmm>>3)&1, 0, 0);
  cb_emit1(cb, 0x0F);
  cb_emit1(cb, 0x11);
  emit_modrm(cb, 2, xmm, 4);
  cb_emit1(cb, 0x24);
  cb_emit4(cb, (uint32_t)disp);
}

static void emit_lea_reg_membp(CodeBuf* cb, int reg, int disp){
  int disp8 = (-disp >= -128 && -disp <= 127);
  emit_rex(cb, 1, (reg>>3)&1, 0, 0);
  cb_emit1(cb, 0x8D);
  emit_modrm(cb, disp8 ? 1 : 2, reg, 5);
  if(disp8) cb_emit1(cb, (unsigned char)(-disp));
  else cb_emit4(cb, (uint32_t)(-(int32_t)disp));
}

static void emit_mov_reg_rip(CodeBuf* cb, int reg, uint32_t text_rva, uint32_t target_rva){
  emit_rex(cb, 1, (reg>>3)&1, 0, 0);
  cb_emit1(cb, 0x8B);
  emit_modrm(cb, 0, reg, 5);
  size_t pos = cb->len;
  cb_emit4(cb, 0);
  uint32_t rip = text_rva + (uint32_t)pos + 4;
  int32_t disp = (int32_t)(target_rva - rip);
  cb_patch4(cb, pos, (uint32_t)disp);
}

static void emit_lea_reg_rip(CodeBuf* cb, int reg, uint32_t text_rva, uint32_t target_rva){
  emit_rex(cb, 1, (reg>>3)&1, 0, 0);
  cb_emit1(cb, 0x8D);
  emit_modrm(cb, 0, reg, 5);
  size_t pos = cb->len;
  cb_emit4(cb, 0);
  uint32_t rip = text_rva + (uint32_t)pos + 4;
  int32_t disp = (int32_t)(target_rva - rip);
  cb_patch4(cb, pos, (uint32_t)disp);
}

static void emit_call_reg(CodeBuf* cb, int reg){
  emit_rex(cb, 0, 0, 0, (reg>>3)&1);
  cb_emit1(cb, 0xFF);
  emit_modrm(cb, 3, 2, reg);
}

static void emit_store_rsp_disp_reg(CodeBuf* cb, int disp, int reg){
  // mov [rsp+disp], reg
  emit_rex(cb, 1, (reg>>3)&1, 0, 0);
  cb_emit1(cb, 0x89);
  emit_modrm(cb, 2, reg, 4);
  cb_emit1(cb, 0x24);
  cb_emit4(cb, (uint32_t)disp);
}

static void emit_store_rsp_disp_imm32(CodeBuf* cb, int disp, uint32_t imm){
  // mov dword [rsp+disp], imm32
  emit_rex(cb, 0, 0, 0, 0);
  cb_emit1(cb, 0xC7);
  emit_modrm(cb, 2, 0, 4);
  cb_emit1(cb, 0x24);
  cb_emit4(cb, (uint32_t)disp);
  cb_emit4(cb, imm);
}

static void emit_store_rsp_disp_imm16(CodeBuf* cb, int disp, uint16_t imm){
  cb_emit1(cb, 0x66);
  cb_emit1(cb, 0xC7);
  if(disp == 0){
    emit_modrm(cb, 0, 0, 4);
    cb_emit1(cb, 0x24);
  } else if(disp >= -128 && disp <= 127){
    emit_modrm(cb, 1, 0, 4);
    cb_emit1(cb, 0x24);
    cb_emit1(cb, (unsigned char)disp);
  } else {
    emit_modrm(cb, 2, 0, 4);
    cb_emit1(cb, 0x24);
    cb_emit4(cb, (uint32_t)disp);
  }
  cb_emit1(cb, (unsigned char)(imm & 0xFF));
  cb_emit1(cb, (unsigned char)((imm >> 8) & 0xFF));
}

static void emit_store_rsp_disp_ax16(CodeBuf* cb, int disp){
  cb_emit1(cb, 0x66);
  cb_emit1(cb, 0x89);
  if(disp >= -128 && disp <= 127){
    emit_modrm(cb, 1, 0, 4);
    cb_emit1(cb, 0x24);
    cb_emit1(cb, (unsigned char)disp);
  } else {
    emit_modrm(cb, 2, 0, 4);
    cb_emit1(cb, 0x24);
    cb_emit4(cb, (uint32_t)disp);
  }
}

static void emit_mov_reg_imm32(CodeBuf* cb, int reg, uint32_t imm){
  emit_rex(cb, 0, 0, 0, (reg>>3)&1);
  cb_emit1(cb, (unsigned char)(0xB8 + (reg & 7)));
  cb_emit4(cb, imm);
}

static void emit_mov_reg_rsp_disp(CodeBuf* cb, int reg, int disp){
  emit_rex(cb, 1, (reg>>3)&1, 0, 0);
  cb_emit1(cb, 0x8B);
  if(disp == 0){
    emit_modrm(cb, 0, reg, 4);
    cb_emit1(cb, 0x24);
  } else if(disp >= -128 && disp <= 127){
    emit_modrm(cb, 1, reg, 4);
    cb_emit1(cb, 0x24);
    cb_emit1(cb, (unsigned char)disp);
  } else {
    emit_modrm(cb, 2, reg, 4);
    cb_emit1(cb, 0x24);
    cb_emit4(cb, (uint32_t)disp);
  }
}

static void emit_mov_eax_rsp_disp(CodeBuf* cb, int disp){
  emit_rex(cb, 0, 0, 0, 0);
  cb_emit1(cb, 0x8B);
  if(disp == 0){
    emit_modrm(cb, 0, 0, 4);
    cb_emit1(cb, 0x24);
  } else if(disp >= -128 && disp <= 127){
    emit_modrm(cb, 1, 0, 4);
    cb_emit1(cb, 0x24);
    cb_emit1(cb, (unsigned char)disp);
  } else {
    emit_modrm(cb, 2, 0, 4);
    cb_emit1(cb, 0x24);
    cb_emit4(cb, (uint32_t)disp);
  }
}

static void emit_lea_reg_rsp_disp(CodeBuf* cb, int reg, int disp){
  emit_rex(cb, 1, (reg>>3)&1, 0, 0);
  cb_emit1(cb, 0x8D);
  if(disp == 0){
    emit_modrm(cb, 0, reg, 4);
    cb_emit1(cb, 0x24);
  } else if(disp >= -128 && disp <= 127){
    emit_modrm(cb, 1, reg, 4);
    cb_emit1(cb, 0x24);
    cb_emit1(cb, (unsigned char)disp);
  } else {
    emit_modrm(cb, 2, reg, 4);
    cb_emit1(cb, 0x24);
    cb_emit4(cb, (uint32_t)disp);
  }
}

static void emit_sub_rsp_imm32(CodeBuf* cb, uint32_t imm){
  cb_emit(cb, "\x48\x81\xEC", 3);
  cb_emit4(cb, imm);
}

static void emit_add_rsp_imm32(CodeBuf* cb, uint32_t imm){
  cb_emit(cb, "\x48\x81\xC4", 3);
  cb_emit4(cb, imm);
}

static uint32_t net_iat_for_name(const PeImportInfo* imp, const char* name, int nlen, const char** label){
  if(label) *label = NULL;
  if(!imp || !name) return 0;
#define TN_NET_MATCH(field, lit) \
  do { \
    if(nlen == (int)(sizeof(lit) - 1) && strncmp(name, lit, sizeof(lit) - 1) == 0){ \
      if(label) *label = lit; \
      return imp->field; \
    } \
  } while(0)
  TN_NET_MATCH(iat_net_af_inet, "net_af_inet");
  TN_NET_MATCH(iat_net_af_inet6, "net_af_inet6");
  TN_NET_MATCH(iat_net_sock_stream, "net_sock_stream");
  TN_NET_MATCH(iat_net_sock_dgram, "net_sock_dgram");
  TN_NET_MATCH(iat_net_ipproto_tcp, "net_ipproto_tcp");
  TN_NET_MATCH(iat_net_ipproto_udp, "net_ipproto_udp");
  TN_NET_MATCH(iat_net_init, "net_init");
  TN_NET_MATCH(iat_net_cleanup, "net_cleanup");
  TN_NET_MATCH(iat_net_last_error, "net_last_error");
  TN_NET_MATCH(iat_net_socket, "net_socket");
  TN_NET_MATCH(iat_net_close, "net_close");
  TN_NET_MATCH(iat_net_connect, "net_connect");
  TN_NET_MATCH(iat_net_bind, "net_bind");
  TN_NET_MATCH(iat_net_listen, "net_listen");
  TN_NET_MATCH(iat_net_accept, "net_accept");
  TN_NET_MATCH(iat_net_send, "net_send");
  TN_NET_MATCH(iat_net_recv, "net_recv");
  TN_NET_MATCH(iat_net_set_blocking, "net_set_blocking");
  TN_NET_MATCH(iat_net_set_timeout, "net_set_timeout");
  TN_NET_MATCH(iat_net_resolve, "net_resolve");
#undef TN_NET_MATCH
  return 0;
}

static int net_label_eq(const char* label, const char* lit){
  return label && strcmp(label, lit) == 0;
}

static void emit_win_iat_call(CodeBuf* cb, uint32_t text_rva, uint32_t iat){
  emit_mov_reg_rip(cb, 0, text_rva, iat);
  emit_call_reg(cb, 0);
}

static void patch_rel32_to(CodeBuf* cb, size_t pos, size_t target);

static size_t emit_jmp32_placeholder_cb(CodeBuf* cb){
  cb_emit1(cb, 0xE9);
  size_t pos = cb->len;
  cb_emit4(cb, 0);
  return pos;
}

static size_t emit_call32_placeholder_cb(CodeBuf* cb){
  cb_emit1(cb, 0xE8);
  size_t pos = cb->len;
  cb_emit4(cb, 0);
  return pos;
}

static void emit_win_ipv4_parse_helper(CodeBuf* cb){
  cb_emit(cb,
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
}

static void emit_win_call_ipv4_parse(CodeBuf* cb, int host_disp, uint32_t default_addr){
  size_t j_skip = emit_jmp32_placeholder_cb(cb);
  size_t helper = cb->len;
  emit_win_ipv4_parse_helper(cb);
  size_t after_helper = cb->len;
  patch_rel32_to(cb, j_skip, after_helper);
  emit_mov_reg_rsp_disp(cb, 1, host_disp);     // rcx = host
  emit_mov_reg_imm32(cb, 2, default_addr);     // edx = default sockaddr dword
  size_t call_pos = emit_call32_placeholder_cb(cb);
  patch_rel32_to(cb, call_pos, helper);
}

static int emit_win_net_runtime_call(CodeBuf* cb, uint32_t text_rva, const PeImportInfo* imp,
                                     const char* label){
  if(!cb || !imp || !label) return -1;

  if(net_label_eq(label, "net_af_inet")){ emit_mov_reg_imm64(cb, 0, 2); return 1; }
  if(net_label_eq(label, "net_af_inet6")){ emit_mov_reg_imm64(cb, 0, 23); return 1; }
  if(net_label_eq(label, "net_sock_stream")){ emit_mov_reg_imm64(cb, 0, 1); return 1; }
  if(net_label_eq(label, "net_sock_dgram")){ emit_mov_reg_imm64(cb, 0, 2); return 1; }
  if(net_label_eq(label, "net_ipproto_tcp")){ emit_mov_reg_imm64(cb, 0, 6); return 1; }
  if(net_label_eq(label, "net_ipproto_udp")){ emit_mov_reg_imm64(cb, 0, 17); return 1; }

  if(net_label_eq(label, "net_init")){
    if(!ensure_iat(imp->iat_wsa_startup, "net_init->WSAStartup")) return -1;
    emit_sub_rsp_imm32(cb, 0x1D0);
    emit_mov_reg_imm32(cb, 1, 0x0202u);
    emit_lea_reg_rsp_disp(cb, 2, 0x20);
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_startup);
    emit_add_rsp_imm32(cb, 0x1D0);
    return 1;
  }
  if(net_label_eq(label, "net_cleanup")){
    if(!ensure_iat(imp->iat_wsa_cleanup, "net_cleanup->WSACleanup")) return -1;
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_cleanup);
    return 1;
  }
  if(net_label_eq(label, "net_last_error")){
    if(!ensure_iat(imp->iat_wsa_last_error, "net_last_error->WSAGetLastError")) return -1;
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_last_error);
    return 1;
  }
  if(net_label_eq(label, "net_socket")){
    if(!ensure_iat(imp->iat_wsa_socket, "net_socket->socket")) return -1;
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_socket);
    return 1;
  }
  if(net_label_eq(label, "net_close")){
    if(!ensure_iat(imp->iat_wsa_closesocket, "net_close->closesocket")) return -1;
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_closesocket);
    return 1;
  }
  if(net_label_eq(label, "net_connect")){
    if(!ensure_iat(imp->iat_wsa_connect, "net_connect->connect")) return -1;
    emit_sub_rsp_imm32(cb, 0x60);
    emit_store_rsp_disp_reg(cb, 0x40, 1); // socket
    emit_store_rsp_disp_reg(cb, 0x48, 8); // port
    emit_store_rsp_disp_reg(cb, 0x50, 2); // host
    emit_win_call_ipv4_parse(cb, 0x50, 0x0100007Fu);
    cb_emit(cb, "\x83\xF8\xFF", 3); // cmp eax,-1
    cb_emit(cb, "\x0F\x84", 2); size_t j_bad_host = cb->len; cb_emit4(cb, 0);
    emit_store_rsp_disp_reg(cb, 0x24, 0); // sin_addr
    emit_store_rsp_disp_imm16(cb, 0x20, 2);
    emit_mov_eax_rsp_disp(cb, 0x48);
    cb_emit(cb, "\x86\xE0", 2);
    emit_store_rsp_disp_ax16(cb, 0x22);
    emit_mov_reg_imm64(cb, 0, 0);
    emit_store_rsp_disp_reg(cb, 0x28, 0);
    emit_mov_reg_rsp_disp(cb, 1, 0x40);
    emit_lea_reg_rsp_disp(cb, 2, 0x20);
    emit_mov_reg_imm32(cb, 8, 16);
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_connect);
    emit_add_rsp_imm32(cb, 0x60);
    cb_emit1(cb, 0xE9); size_t j_done = cb->len; cb_emit4(cb, 0);
    size_t fail = cb->len;
    emit_mov_reg_imm32(cb, 0, 0xFFFFFFFFu);
    emit_add_rsp_imm32(cb, 0x60);
    size_t done = cb->len;
    patch_rel32_to(cb, j_bad_host, fail);
    patch_rel32_to(cb, j_done, done);
    return 1;
  }
  if(net_label_eq(label, "net_bind")){
    if(!ensure_iat(imp->iat_wsa_bind, "net_bind->bind")) return -1;
    if(!ensure_iat(imp->iat_wsa_setsockopt, "net_bind->setsockopt")) return -1;
    emit_sub_rsp_imm32(cb, 0x60);
    emit_store_rsp_disp_reg(cb, 0x40, 1); // socket
    emit_store_rsp_disp_reg(cb, 0x48, 8); // port
    emit_store_rsp_disp_reg(cb, 0x50, 2); // host
    emit_store_rsp_disp_imm32(cb, 0x30, 1);
    emit_store_rsp_disp_imm32(cb, 0x20, 4);
    emit_mov_reg_rsp_disp(cb, 1, 0x40);
    emit_mov_reg_imm32(cb, 2, 0xFFFFu);   // SOL_SOCKET
    emit_mov_reg_imm32(cb, 8, 0x0004u);   // SO_REUSEADDR
    emit_lea_reg_rsp_disp(cb, 9, 0x30);
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_setsockopt);
    emit_win_call_ipv4_parse(cb, 0x50, 0x0100007Fu);
    cb_emit(cb, "\x83\xF8\xFF", 3); // cmp eax,-1
    cb_emit(cb, "\x0F\x84", 2); size_t j_bad_host = cb->len; cb_emit4(cb, 0);
    emit_store_rsp_disp_reg(cb, 0x24, 0); // sin_addr
    emit_store_rsp_disp_imm16(cb, 0x20, 2);
    emit_mov_eax_rsp_disp(cb, 0x48);
    cb_emit(cb, "\x86\xE0", 2);
    emit_store_rsp_disp_ax16(cb, 0x22);
    emit_mov_reg_imm64(cb, 0, 0);
    emit_store_rsp_disp_reg(cb, 0x28, 0);
    emit_mov_reg_rsp_disp(cb, 1, 0x40);
    emit_lea_reg_rsp_disp(cb, 2, 0x20);
    emit_mov_reg_imm32(cb, 8, 16);
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_bind);
    emit_add_rsp_imm32(cb, 0x60);
    cb_emit1(cb, 0xE9); size_t j_done = cb->len; cb_emit4(cb, 0);
    size_t fail = cb->len;
    emit_mov_reg_imm32(cb, 0, 0xFFFFFFFFu);
    emit_add_rsp_imm32(cb, 0x60);
    size_t done = cb->len;
    patch_rel32_to(cb, j_bad_host, fail);
    patch_rel32_to(cb, j_done, done);
    return 1;
  }
  if(net_label_eq(label, "net_listen")){
    if(!ensure_iat(imp->iat_wsa_listen, "net_listen->listen")) return -1;
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_listen);
    return 1;
  }
  if(net_label_eq(label, "net_accept")){
    if(!ensure_iat(imp->iat_wsa_accept, "net_accept->accept")) return -1;
    emit_mov_reg_imm64(cb, 2, 0);
    emit_mov_reg_imm64(cb, 8, 0);
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_accept);
    return 1;
  }
  if(net_label_eq(label, "net_send")){
    if(!ensure_iat(imp->iat_wsa_send, "net_send->send")) return -1;
    emit_mov_reg_imm64(cb, 9, 0);
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_send);
    return 1;
  }
  if(net_label_eq(label, "net_recv")){
    if(!ensure_iat(imp->iat_wsa_recv, "net_recv->recv")) return -1;
    emit_mov_reg_imm64(cb, 9, 0);
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_recv);
    return 1;
  }
  if(net_label_eq(label, "net_set_blocking")){
    if(!ensure_iat(imp->iat_wsa_ioctlsocket, "net_set_blocking->ioctlsocket")) return -1;
    emit_sub_rsp_imm32(cb, 0x40);
    emit_store_rsp_disp_imm32(cb, 0x20, 0);
    cb_emit(cb, "\x48\x85\xD2", 3);       // test rdx, rdx
    cb_emit(cb, "\x75\x0C", 2);           // jne +12
    emit_store_rsp_disp_imm32(cb, 0x20, 1);
    emit_mov_reg_imm32(cb, 2, 0x8004667Eu); // FIONBIO
    emit_lea_reg_rsp_disp(cb, 8, 0x20);
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_ioctlsocket);
    emit_add_rsp_imm32(cb, 0x40);
    return 1;
  }
  if(net_label_eq(label, "net_set_timeout")){
    if(!ensure_iat(imp->iat_wsa_setsockopt, "net_set_timeout->setsockopt")) return -1;
    emit_sub_rsp_imm32(cb, 0x50);
    emit_store_rsp_disp_reg(cb, 0x40, 1); // socket
    emit_store_rsp_disp_reg(cb, 0x30, 2); // milliseconds
    emit_store_rsp_disp_imm32(cb, 0x20, 4);
    emit_mov_reg_rsp_disp(cb, 1, 0x40);
    emit_mov_reg_imm32(cb, 2, 0xFFFFu);   // SOL_SOCKET
    emit_mov_reg_imm32(cb, 8, 0x1006u);   // SO_RCVTIMEO
    emit_lea_reg_rsp_disp(cb, 9, 0x30);
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_setsockopt);
    emit_store_rsp_disp_imm32(cb, 0x20, 4);
    emit_mov_reg_rsp_disp(cb, 1, 0x40);
    emit_mov_reg_imm32(cb, 2, 0xFFFFu);   // SOL_SOCKET
    emit_mov_reg_imm32(cb, 8, 0x1005u);   // SO_SNDTIMEO
    emit_lea_reg_rsp_disp(cb, 9, 0x30);
    emit_win_iat_call(cb, text_rva, imp->iat_wsa_setsockopt);
    emit_add_rsp_imm32(cb, 0x50);
    return 1;
  }
  if(net_label_eq(label, "net_resolve")){
    emit_mov_reg_imm64(cb, 0, 0);
    return 1;
  }
  return -1;
}

static int emit_net_runtime_call(CodeBuf* cb, uint32_t text_rva, const PeImportInfo* imp,
                                 const char* name, int nlen){
  const char* label = NULL;
  uint32_t iat = net_iat_for_name(imp, name, nlen, &label);
  if(!label) return 0;
  if(iat != 0){
    emit_win_iat_call(cb, text_rva, iat);
    return 1;
  }
  return emit_win_net_runtime_call(cb, text_rva, imp, label);
}

static int is_net_runtime_name(const PeImportInfo* imp, const char* name, int nlen){
  const char* label = NULL;
  (void)net_iat_for_name(imp, name, nlen, &label);
  return label != NULL;
}

static void emit_call_rel32(CodeBuf* cb, int32_t disp){
  cb_emit1(cb, 0xE8);
  cb_emit4(cb, (uint32_t)disp);
}

static void emit_prologue(CodeBuf* cb, int frame_size){
  cb_emit1(cb, 0x55);               // push rbp
  cb_emit(cb, "\x48\x89\xE5", 3);   // mov rbp, rsp
  if(frame_size > 0){
    cb_emit(cb, "\x48\x81\xEC", 3); // sub rsp, imm32
    cb_emit4(cb, (uint32_t)frame_size);
  }
}

static void emit_epilogue(CodeBuf* cb, int frame_size){
  if(frame_size > 0){
    cb_emit(cb, "\x48\x81\xC4", 3); // add rsp, imm32
    cb_emit4(cb, (uint32_t)frame_size);
  }
  cb_emit1(cb, 0x5D); // pop rbp
  cb_emit1(cb, 0xC3); // ret
}

static void emit_add_rax_rcx(CodeBuf* cb){ cb_emit(cb, "\x48\x01\xC8", 3); }
static void emit_sub_rax_rcx(CodeBuf* cb){ cb_emit(cb, "\x48\x29\xC8", 3); }
static void emit_and_rax_rcx(CodeBuf* cb){ cb_emit(cb, "\x48\x21\xC8", 3); }
static void emit_or_rax_rcx(CodeBuf* cb){ cb_emit(cb, "\x48\x09\xC8", 3); }
static void emit_xor_rax_rcx(CodeBuf* cb){ cb_emit(cb, "\x48\x31\xC8", 3); }
static void emit_imul_rax_rcx(CodeBuf* cb){ cb_emit(cb, "\x48\x0F\xAF\xC1", 4); }
static void emit_imul_rax_imm32(CodeBuf* cb, int32_t imm){
  cb_emit(cb, "\x48\x69\xC0", 3); // imul rax, rax, imm32
  cb_emit4(cb, (uint32_t)imm);
}
static void emit_shl_rax_cl(CodeBuf* cb){ cb_emit(cb, "\x48\xD3\xE0", 3); }
static void emit_sar_rax_cl(CodeBuf* cb){ cb_emit(cb, "\x48\xD3\xF8", 3); }
static void emit_cqo(CodeBuf* cb){ cb_emit(cb, "\x48\x99", 2); }
static void emit_idiv_rcx(CodeBuf* cb){ cb_emit(cb, "\x48\xF7\xF9", 3); }
static void emit_cmp_rax_rcx(CodeBuf* cb){ cb_emit(cb, "\x48\x39\xC8", 3); }
static void emit_setcc(CodeBuf* cb, int cc){
  cb_emit1(cb, 0x0F);
  cb_emit1(cb, (unsigned char)(0x90 | (cc & 0xF)));
  // setcc r/m8 (use AL)
  emit_modrm(cb, 3, 0, 0);
}
static void emit_movzx_eax_al(CodeBuf* cb){ cb_emit(cb, "\x0F\xB6\xC0", 3); }

static void emit_fbin(CodeBuf* cb, int op){
  // xmm0 op xmm1 -> xmm0
  cb_emit(cb, "\xF2", 1);
  cb_emit1(cb, 0x0F);
  switch(op){
    case 0: cb_emit1(cb, 0x58); break; // addsd
    case 1: cb_emit1(cb, 0x5C); break; // subsd
    case 2: cb_emit1(cb, 0x59); break; // mulsd
    case 3: cb_emit1(cb, 0x5E); break; // divsd
    default: cb_emit1(cb, 0x58); break;
  }
  emit_modrm(cb, 3, 0, 1);
}

static void emit_ucomisd(CodeBuf* cb){
  cb_emit(cb, "\x66\x0F\x2E\xC1", 4); // ucomisd xmm0,xmm1
}

static void emit_cvtsi2sd(CodeBuf* cb){
  cb_emit(cb, "\xF2\x0F\x2A\xC0", 4); // cvtsi2sd xmm0, rax
}

static void emit_cvttsd2si(CodeBuf* cb){
  cb_emit(cb, "\xF2\x0F\x2C\xC0", 4); // cvttsd2si rax, xmm0
}

static void emit_mov_rcx_imm32(CodeBuf* cb, int32_t imm){
  cb_emit(cb, "\x48\xC7\xC1", 3);
  cb_emit4(cb, (uint32_t)imm);
}

static void emit_mov_reg_reg(CodeBuf* cb, int dst, int src){
  emit_mov_rr(cb, dst, src);
}

static int reg_slot_off(int r){ return 8 * (r + 1); }

typedef struct {
  int* dst_reg;
  int* off_bytes;
  int n, cap;
} AllocaMap;

static void amap_add(AllocaMap* A, int r, int off){
  if(A->n==A->cap){
    A->cap = A->cap ? A->cap * 2 : 16;
    A->dst_reg  = (int*)realloc(A->dst_reg,  sizeof(int) * (size_t)A->cap);
    A->off_bytes= (int*)realloc(A->off_bytes,sizeof(int) * (size_t)A->cap);
    if(!A->dst_reg || !A->off_bytes) die("out of memory");
  }
  A->dst_reg[A->n] = r;
  A->off_bytes[A->n] = off;
  A->n++;
}

static int amap_find(AllocaMap* A, int r){
  for(int i=0;i<A->n;i++){
    if(A->dst_reg[i]==r) return A->off_bytes[i];
  }
  return -1;
}

typedef struct {
  size_t pos;
  int target;
} CallPatch;

enum { T_MAIN=1, T_SAY=2, T_SAYSTR=3 };

typedef struct {
  size_t pos;
  int func_idx;
} FuncPatch;

typedef struct {
  size_t pos;
  int label;
} LabelPatch;

typedef struct {
  const char* name;
  int len;
  int is_extern;
  size_t off;
  int param_count;
} FuncInfo;

static int func_info_find(const FuncInfo* finfo, int fcount, const char* name, int nlen){
  if(!finfo || !name || nlen <= 0) return -1;
  for(int i=0; i<fcount; i++){
    if(finfo[i].len == nlen && strncmp(finfo[i].name, name, (size_t)nlen)==0){
      return i;
    }
  }
  return -1;
}

static int raw_io_bridge_target(const FuncInfo* finfo, int fcount, const char* name, int nlen){
  if(!name) return -1;
  if(nlen==18 && strncmp(name, "list_dir_recursive", 18)==0){
    int idx = func_info_find(finfo, fcount, "io.dir_list_rec", 15);
    if(idx >= 0) return idx;
    return func_info_find(finfo, fcount, "dir_list_rec", 12);
  }
  if(nlen==4 && strncmp(name, "glob", 4)==0){
    int idx = func_info_find(finfo, fcount, "io.glob_list", 12);
    if(idx >= 0) return idx;
    return func_info_find(finfo, fcount, "glob_list", 9);
  }
  return -1;
}

static int call_target_index(const FuncInfo* finfo, int fcount, const char* name, int nlen){
  int idx = func_info_find(finfo, fcount, name, nlen);
  if(idx >= 0) return idx;
  return raw_io_bridge_target(finfo, fcount, name, nlen);
}

static int emit_func_patch_call(CodeBuf* cb, FuncPatch* fpatches, int* fpatch_n,
                                int fpatch_cap, int tidx){
  if(!cb || !fpatches || !fpatch_n || tidx < 0 || *fpatch_n >= fpatch_cap) return -1;
  cb_emit1(cb, 0xE8);
  size_t pos = cb->len;
  cb_emit4(cb, 0);
  fpatches[*fpatch_n].pos = pos;
  fpatches[*fpatch_n].func_idx = tidx;
  (*fpatch_n)++;
  return 0;
}

typedef struct {
  int live_start;   // instruction index of first definition
  int live_end;     // instruction index of last use
  int vreg;         // which virtual register
  int mreg;         // assigned machine reg, or -1 (spill)
} LsraInterval;

static int emit_call_prepare(CodeBuf* cb, int argc, int* args, const int* vreg_to_mreg, int nreg, LsraInterval* iv, int k, int* out_pushed){
  int pushed = 0;
  if(iv) {
    for(int r=0; r<nreg; r++){
      int mreg = (vreg_to_mreg) ? vreg_to_mreg[r] : -1;
      if(mreg == 10 || mreg == 11) {
        if(iv[r].live_start <= k && iv[r].live_end > k) {
          pushed |= (1<<mreg);
        }
      }
    }
    // Push in fixed order (0 to 15) so popping (15 down to 0) restores correctly
    for(int m=0; m<16; m++) {
      if(pushed & (1<<m)) {
        emit_rex(cb, 1, 0, 0, (m>>3)&1);
        cb_emit1(cb, (unsigned char)(0x50 + (m & 7))); // push
      }
    }
  }
  if(out_pushed) *out_pushed = pushed;

  int num_pushes = 0;
  for(int m=0; m<16; m++) if(pushed & (1<<m)) num_pushes++;

  int extra = (argc > 4) ? (argc - 4) : 0;
  int total = (num_pushes * 8) + 32 + extra * 8;
  int aligned_total = (int)align_up_u32((uint32_t)total, 16);
  int total_al = aligned_total - (num_pushes * 8);

  if(total_al > 0){
    cb_emit(cb, "\x48\x81\xEC", 3);
    cb_emit4(cb, (uint32_t)total_al);
  }
  for(int i=4;i<argc;i++){
    int mreg = (args[i] >= 0 && args[i] < nreg && vreg_to_mreg) ? vreg_to_mreg[args[i]] : -1;
    if(mreg >= 0) {
      emit_mov_rr(cb, 0, mreg);
    } else {
      emit_mov_reg_membp(cb, 0, reg_slot_off(args[i]));
    }
    emit_store_rsp_disp_reg(cb, 32 + 8*(i-4), 0);
  }
  
  if(argc > 0) {
    int mreg = (args[0] < nreg && vreg_to_mreg) ? vreg_to_mreg[args[0]] : -1;
    if(mreg >= 0) emit_mov_rr(cb, 1, mreg);
    else emit_mov_reg_membp(cb, 1, reg_slot_off(args[0]));
    emit_movq_xmm_reg(cb, 0, 1); // Arg1 also in XMM0
  }
  if(argc > 1) {
    int mreg = (args[1] < nreg && vreg_to_mreg) ? vreg_to_mreg[args[1]] : -1;
    if(mreg >= 0) emit_mov_rr(cb, 2, mreg);
    else emit_mov_reg_membp(cb, 2, reg_slot_off(args[1]));
    emit_movq_xmm_reg(cb, 1, 2); // Arg2 also in XMM1
  }
  if(argc > 2) {
    int mreg = (args[2] < nreg && vreg_to_mreg) ? vreg_to_mreg[args[2]] : -1;
    if(mreg >= 0) emit_mov_rr(cb, 8, mreg);
    else emit_mov_reg_membp(cb, 8, reg_slot_off(args[2]));
    emit_movq_xmm_reg(cb, 2, 8); // Arg3 also in XMM2
  }
  if(argc > 3) {
    int mreg = (args[3] < nreg && vreg_to_mreg) ? vreg_to_mreg[args[3]] : -1;
    if(mreg >= 0) emit_mov_rr(cb, 9, mreg);
    else emit_mov_reg_membp(cb, 9, reg_slot_off(args[3]));
    emit_movq_xmm_reg(cb, 3, 9); // Arg4 also in XMM3
  }
  return total_al;
}

static void emit_call_finish(CodeBuf* cb, int total_al, int pushed){
  if(total_al > 0){
    cb_emit(cb, "\x48\x81\xC4", 3);
    cb_emit4(cb, (uint32_t)total_al);
  }
  for(int m=15; m>=0; m--){
    if(pushed & (1<<m)){
      emit_rex(cb, 1, 0, 0, (m>>3)&1);
      cb_emit1(cb, (unsigned char)(0x58 + (m & 7))); // pop
    }
  }
}

static void patch_rel32_to(CodeBuf* cb, size_t pos, size_t target){
  int32_t disp = (int32_t)((int64_t)target - (int64_t)(pos + 4));
  cb_patch4(cb, pos, (uint32_t)disp);
}

static int emit_win_list_dir_inline(CodeBuf* cb, uint32_t text_rva,
                                    const PeImportInfo* imp,
                                    size_t malloc_off, size_t free_off){
  if(!cb || !imp) return -1;
  if(!ensure_iat(imp->iat_findfirstfile, "list_dir->FindFirstFileA")) return -1;
  if(!ensure_iat(imp->iat_findnextfile, "list_dir->FindNextFileA")) return -1;
  if(!ensure_iat(imp->iat_findclose, "list_dir->FindClose")) return -1;

  const int frame = 0x680;
  const int out_off = 0x40;
  const int handle_off = 0x48;
  const int len_off = 0x50;
  const int path_off = 0x58;
  const int pattern_off = 0x80;
  const int find_off = 0x300;
  const int name_off = find_off + 44;
  const int out_cap_minus_two = 8190;

  cb_emit(cb, "\x48\x85\xC9", 3); // test rcx, rcx
  cb_emit(cb, "\x0F\x85", 2); size_t j_have_path = cb->len; cb_emit4(cb, 0);
  emit_mov_reg_imm64(cb, 0, 0);
  cb_emit1(cb, 0xE9); size_t j_null_done = cb->len; cb_emit4(cb, 0);

  size_t have_path = cb->len;
  patch_rel32_to(cb, j_have_path, have_path);
  cb_emit(cb, "\x48\x81\xEC", 3); cb_emit4(cb, (uint32_t)frame);
  cb_emit(cb, "\x48\x89\x4C\x24", 4); cb_emit1(cb, (unsigned char)path_off); // save path

  // Build a FindFirstFileA search pattern: path + ("\\*" or "*").
  cb_emit(cb, "\x4C\x8B\xC1", 3); // mov r8, rcx
  cb_emit(cb, "\x4C\x8D\x8C\x24", 4); cb_emit4(cb, (uint32_t)pattern_off); // lea r9,[rsp+pattern]
  cb_emit(cb, "\x45\x31\xD2", 3); // xor r10d,r10d
  size_t copy_loop = cb->len;
  cb_emit(cb, "\x41\x8A\x00", 3); // mov al,[r8]
  cb_emit(cb, "\x84\xC0", 2); // test al,al
  cb_emit(cb, "\x0F\x84", 2); size_t j_copy_done = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x41\x88\x01", 3); // mov [r9],al
  cb_emit(cb, "\x49\xFF\xC0", 3); // inc r8
  cb_emit(cb, "\x49\xFF\xC1", 3); // inc r9
  cb_emit(cb, "\x49\xFF\xC2", 3); // inc r10
  cb_emit(cb, "\x49\x81\xFA", 3); cb_emit4(cb, 496); // cmp r10,496
  cb_emit(cb, "\x0F\x82", 2); size_t j_copy_loop = cb->len; cb_emit4(cb, 0);
  size_t copy_done = cb->len;
  patch_rel32_to(cb, j_copy_done, copy_done);
  patch_rel32_to(cb, j_copy_loop, copy_loop);

  cb_emit(cb, "\x4D\x85\xD2", 3); // test r10,r10
  cb_emit(cb, "\x0F\x84", 2); size_t j_append_star = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x41\x8A\x41\xFF", 4); // mov al,[r9-1]
  cb_emit(cb, "\x3C\x5C", 2); // cmp al,'\\'
  cb_emit(cb, "\x0F\x84", 2); size_t j_sep_ok1 = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x3C\x2F", 2); // cmp al,'/'
  cb_emit(cb, "\x0F\x84", 2); size_t j_sep_ok2 = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x41\xC6\x01\x5C", 4); // mov byte [r9],'\\'
  cb_emit(cb, "\x49\xFF\xC1", 3); // inc r9
  size_t append_star = cb->len;
  patch_rel32_to(cb, j_append_star, append_star);
  patch_rel32_to(cb, j_sep_ok1, append_star);
  patch_rel32_to(cb, j_sep_ok2, append_star);
  cb_emit(cb, "\x41\xC6\x01\x2A", 4); // mov byte [r9],'*'
  cb_emit(cb, "\x41\xC6\x41\x01\x00", 5); // mov byte [r9+1],0

  cb_emit(cb, "\x48\xC7\xC1\x00\x20\x00\x00", 7); // mov rcx,8192
  { int32_t disp = (int32_t)((int64_t)(text_rva + malloc_off) - (int64_t)(text_rva + (uint32_t)(cb->len + 5))); emit_call_rel32(cb, disp); }
  cb_emit(cb, "\x48\x85\xC0", 3); // test rax,rax
  cb_emit(cb, "\x0F\x85", 2); size_t j_out_ok = cb->len; cb_emit4(cb, 0);
  cb_emit1(cb, 0xE9); size_t j_fail_stack = cb->len; cb_emit4(cb, 0);
  size_t out_ok = cb->len;
  patch_rel32_to(cb, j_out_ok, out_ok);
  cb_emit(cb, "\x48\x89\x44\x24", 4); cb_emit1(cb, (unsigned char)out_off); // save out
  cb_emit(cb, "\xC6\x00\x00", 3); // out[0]=0

  cb_emit(cb, "\x48\x8D\x8C\x24", 4); cb_emit4(cb, (uint32_t)pattern_off); // rcx=&pattern
  cb_emit(cb, "\x48\x8D\x94\x24", 4); cb_emit4(cb, (uint32_t)find_off); // rdx=&finddata
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_findfirstfile);
  emit_call_reg(cb, 0);
  cb_emit(cb, "\x48\x83\xF8\xFF", 4); // cmp rax,-1
  cb_emit(cb, "\x0F\x85", 2); size_t j_find_ok = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x48\x8B\x4C\x24", 4); cb_emit1(cb, (unsigned char)out_off); // rcx=out
  { int32_t disp = (int32_t)((int64_t)(text_rva + free_off) - (int64_t)(text_rva + (uint32_t)(cb->len + 5))); emit_call_rel32(cb, disp); }
  cb_emit1(cb, 0xE9); size_t j_fail_stack2 = cb->len; cb_emit4(cb, 0);
  size_t find_ok = cb->len;
  patch_rel32_to(cb, j_find_ok, find_ok);
  cb_emit(cb, "\x48\x89\x44\x24", 4); cb_emit1(cb, (unsigned char)handle_off); // save handle
  cb_emit(cb, "\x48\xC7\x44\x24", 4); cb_emit1(cb, (unsigned char)len_off); cb_emit4(cb, 0); // len=0

  size_t entry_loop = cb->len;
  cb_emit(cb, "\x48\x8D\x94\x24", 4); cb_emit4(cb, (uint32_t)name_off); // rdx=&cFileName
  cb_emit(cb, "\x8A\x02", 2); // mov al,[rdx]
  cb_emit(cb, "\x3C\x2E", 2); // cmp al,'.'
  cb_emit(cb, "\x0F\x85", 2); size_t j_copy_name = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x8A\x42\x01", 3); // mov al,[rdx+1]
  cb_emit(cb, "\x84\xC0", 2); // test al,al
  cb_emit(cb, "\x0F\x84", 2); size_t j_next_entry0 = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x3C\x2E", 2); // cmp al,'.'
  cb_emit(cb, "\x0F\x85", 2); size_t j_copy_name2 = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x8A\x42\x02", 3); // mov al,[rdx+2]
  cb_emit(cb, "\x84\xC0", 2); // test al,al
  cb_emit(cb, "\x0F\x84", 2); size_t j_next_entry1 = cb->len; cb_emit4(cb, 0);

  size_t copy_name = cb->len;
  patch_rel32_to(cb, j_copy_name, copy_name);
  patch_rel32_to(cb, j_copy_name2, copy_name);
  cb_emit(cb, "\x4C\x8B\x54\x24", 4); cb_emit1(cb, (unsigned char)out_off); // r10=out
  cb_emit(cb, "\x4C\x8B\x5C\x24", 4); cb_emit1(cb, (unsigned char)len_off); // r11=len
  size_t copy_char = cb->len;
  cb_emit(cb, "\x49\x81\xFB", 3); cb_emit4(cb, (uint32_t)out_cap_minus_two); // cmp r11,8190
  cb_emit(cb, "\x0F\x83", 2); size_t j_finish_overflow0 = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x8A\x02", 2); // mov al,[rdx]
  cb_emit(cb, "\x84\xC0", 2); // test al,al
  cb_emit(cb, "\x0F\x84", 2); size_t j_append_nl = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x43\x88\x04\x1A", 4); // mov [r10+r11],al
  cb_emit(cb, "\x49\xFF\xC3", 3); // inc r11
  cb_emit(cb, "\x48\xFF\xC2", 3); // inc rdx
  cb_emit1(cb, 0xE9); size_t j_copy_char = cb->len; cb_emit4(cb, 0);
  size_t append_nl = cb->len;
  patch_rel32_to(cb, j_append_nl, append_nl);
  cb_emit(cb, "\x49\x81\xFB", 3); cb_emit4(cb, (uint32_t)out_cap_minus_two); // cmp r11,8190
  cb_emit(cb, "\x0F\x83", 2); size_t j_finish_overflow1 = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x43\xC6\x04\x1A\x0A", 5); // out[len]='\n'
  cb_emit(cb, "\x49\xFF\xC3", 3); // inc r11
  cb_emit(cb, "\x4C\x89\x5C\x24", 4); cb_emit1(cb, (unsigned char)len_off); // save len
  patch_rel32_to(cb, j_copy_char, copy_char);

  size_t next_entry = cb->len;
  patch_rel32_to(cb, j_next_entry0, next_entry);
  patch_rel32_to(cb, j_next_entry1, next_entry);
  cb_emit(cb, "\x48\x8B\x4C\x24", 4); cb_emit1(cb, (unsigned char)handle_off); // rcx=handle
  cb_emit(cb, "\x48\x8D\x94\x24", 4); cb_emit4(cb, (uint32_t)find_off); // rdx=&finddata
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_findnextfile);
  emit_call_reg(cb, 0);
  cb_emit(cb, "\x48\x85\xC0", 3); // test rax,rax
  cb_emit(cb, "\x0F\x85", 2); size_t j_entry_loop = cb->len; cb_emit4(cb, 0);

  size_t finish = cb->len;
  patch_rel32_to(cb, j_finish_overflow0, finish);
  patch_rel32_to(cb, j_finish_overflow1, finish);
  patch_rel32_to(cb, j_entry_loop, entry_loop);
  cb_emit(cb, "\x48\x8B\x4C\x24", 4); cb_emit1(cb, (unsigned char)handle_off); // rcx=handle
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_findclose);
  emit_call_reg(cb, 0);
  cb_emit(cb, "\x48\x8B\x44\x24", 4); cb_emit1(cb, (unsigned char)out_off); // rax=out
  cb_emit(cb, "\x48\x8B\x54\x24", 4); cb_emit1(cb, (unsigned char)len_off); // rdx=len
  cb_emit(cb, "\x48\x85\xD2", 3); // test rdx,rdx
  cb_emit(cb, "\x0F\x84", 2); size_t j_empty = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x48\xFF\xCA", 3); // dec rdx
  cb_emit(cb, "\xC6\x04\x10\x00", 4); // out[len-1]=0
  cb_emit1(cb, 0xE9); size_t j_restore = cb->len; cb_emit4(cb, 0);
  size_t empty = cb->len;
  patch_rel32_to(cb, j_empty, empty);
  cb_emit(cb, "\xC6\x00\x00", 3); // out[0]=0
  cb_emit1(cb, 0xE9); size_t j_restore2 = cb->len; cb_emit4(cb, 0);

  size_t fail_stack = cb->len;
  patch_rel32_to(cb, j_fail_stack, fail_stack);
  patch_rel32_to(cb, j_fail_stack2, fail_stack);
  emit_mov_reg_imm64(cb, 0, 0);

  size_t restore_stack = cb->len;
  patch_rel32_to(cb, j_restore, restore_stack);
  patch_rel32_to(cb, j_restore2, restore_stack);
  cb_emit(cb, "\x48\x81\xC4", 3); cb_emit4(cb, (uint32_t)frame);

  size_t done = cb->len;
  patch_rel32_to(cb, j_null_done, done);
  return 0;
}

static int emit_win_proc_out_inline(CodeBuf* cb, uint32_t text_rva,
                                    const PeImportInfo* imp,
                                    size_t malloc_off){
  if(!cb || !imp) return -1;
  if(!ensure_iat(imp->iat_popen, "proc_out->_popen")) return -1;
  if(!ensure_iat(imp->iat_pclose, "proc_out->_pclose")) return -1;
  if(!ensure_iat(imp->iat_fread, "proc_out->fread")) return -1;

  const int frame = 0x70;
  const int pipe_off = 0x40;
  const int out_off = 0x48;
  const int len_off = 0x50;
  const int mode_off = 0x60;
  const int cap_minus_one = 32767;

  cb_emit(cb, "\x48\x85\xC9", 3); // test rcx, rcx
  cb_emit(cb, "\x0F\x85", 2); size_t j_have_cmd = cb->len; cb_emit4(cb, 0);
  emit_mov_reg_imm64(cb, 0, 0);
  cb_emit1(cb, 0xE9); size_t j_null_done = cb->len; cb_emit4(cb, 0);

  size_t have_cmd = cb->len;
  patch_rel32_to(cb, j_have_cmd, have_cmd);
  cb_emit(cb, "\x48\x83\xEC", 3); cb_emit1(cb, (unsigned char)frame);

  cb_emit(cb, "\xC6\x44\x24", 3); cb_emit1(cb, (unsigned char)mode_off); cb_emit1(cb, 'r');
  cb_emit(cb, "\xC6\x44\x24", 3); cb_emit1(cb, (unsigned char)(mode_off + 1)); cb_emit1(cb, 'b');
  cb_emit(cb, "\xC6\x44\x24", 3); cb_emit1(cb, (unsigned char)(mode_off + 2)); cb_emit1(cb, 0);

  cb_emit(cb, "\x48\x8D\x54\x24", 4); cb_emit1(cb, (unsigned char)mode_off); // rdx=&mode
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_popen);
  emit_call_reg(cb, 0);
  cb_emit(cb, "\x48\x85\xC0", 3); // test rax,rax
  cb_emit(cb, "\x0F\x85", 2); size_t j_pipe_ok = cb->len; cb_emit4(cb, 0);
  cb_emit1(cb, 0xE9); size_t j_fail_stack = cb->len; cb_emit4(cb, 0);

  size_t pipe_ok = cb->len;
  patch_rel32_to(cb, j_pipe_ok, pipe_ok);
  cb_emit(cb, "\x48\x89\x44\x24", 4); cb_emit1(cb, (unsigned char)pipe_off); // pipe

  cb_emit(cb, "\x48\xC7\xC1\x00\x80\x00\x00", 7); // mov rcx,32768
  { int32_t disp = (int32_t)((int64_t)(text_rva + malloc_off) - (int64_t)(text_rva + (uint32_t)(cb->len + 5))); emit_call_rel32(cb, disp); }
  cb_emit(cb, "\x48\x85\xC0", 3);
  cb_emit(cb, "\x0F\x85", 2); size_t j_out_ok = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x48\x8B\x4C\x24", 4); cb_emit1(cb, (unsigned char)pipe_off);
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_pclose);
  emit_call_reg(cb, 0);
  cb_emit1(cb, 0xE9); size_t j_fail_after_close = cb->len; cb_emit4(cb, 0);

  size_t out_ok = cb->len;
  patch_rel32_to(cb, j_out_ok, out_ok);
  cb_emit(cb, "\x48\x89\x44\x24", 4); cb_emit1(cb, (unsigned char)out_off); // out
  cb_emit(cb, "\x48\xC7\x44\x24", 4); cb_emit1(cb, (unsigned char)len_off); cb_emit4(cb, 0); // len=0

  size_t read_loop = cb->len;
  cb_emit(cb, "\x48\x8B\x54\x24", 4); cb_emit1(cb, (unsigned char)len_off); // rdx=len
  cb_emit(cb, "\x48\x81\xFA", 3); cb_emit4(cb, (uint32_t)cap_minus_one);
  cb_emit(cb, "\x0F\x83", 2); size_t j_finish_full = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x4C\x8B\x54\x24", 4); cb_emit1(cb, (unsigned char)out_off); // r10=out
  cb_emit(cb, "\x49\x8D\x0C\x12", 4); // rcx=out+len
  cb_emit(cb, "\x48\xC7\xC2\x01\x00\x00\x00", 7); // rdx=1
  cb_emit(cb, "\x49\xC7\xC0\xFF\x7F\x00\x00", 7); // r8=32767
  cb_emit(cb, "\x4C\x8B\x5C\x24", 4); cb_emit1(cb, (unsigned char)len_off); // r11=len
  cb_emit(cb, "\x4D\x29\xD8", 3); // r8-=r11
  cb_emit(cb, "\x4C\x8B\x4C\x24", 4); cb_emit1(cb, (unsigned char)pipe_off); // r9=pipe
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fread);
  emit_call_reg(cb, 0);
  cb_emit(cb, "\x48\x85\xC0", 3);
  cb_emit(cb, "\x0F\x84", 2); size_t j_finish_eof = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x48\x01\x44\x24", 4); cb_emit1(cb, (unsigned char)len_off); // len+=rax
  cb_emit1(cb, 0xE9); size_t j_read_loop = cb->len; cb_emit4(cb, 0);

  size_t finish = cb->len;
  patch_rel32_to(cb, j_finish_full, finish);
  patch_rel32_to(cb, j_finish_eof, finish);
  patch_rel32_to(cb, j_read_loop, read_loop);
  cb_emit(cb, "\x48\x8B\x4C\x24", 4); cb_emit1(cb, (unsigned char)pipe_off);
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_pclose);
  emit_call_reg(cb, 0);
  cb_emit(cb, "\x48\x8B\x44\x24", 4); cb_emit1(cb, (unsigned char)out_off); // rax=out
  cb_emit(cb, "\x48\x8B\x54\x24", 4); cb_emit1(cb, (unsigned char)len_off); // rdx=len
  cb_emit(cb, "\xC6\x04\x10\x00", 4); // out[len]=0
  cb_emit1(cb, 0xE9); size_t j_restore = cb->len; cb_emit4(cb, 0);

  size_t fail_stack = cb->len;
  patch_rel32_to(cb, j_fail_stack, fail_stack);
  patch_rel32_to(cb, j_fail_after_close, fail_stack);
  emit_mov_reg_imm64(cb, 0, 0);

  size_t restore_stack = cb->len;
  patch_rel32_to(cb, j_restore, restore_stack);
  cb_emit(cb, "\x48\x83\xC4", 3); cb_emit1(cb, (unsigned char)frame);

  size_t done = cb->len;
  patch_rel32_to(cb, j_null_done, done);
  return 0;
}

// ----------------- x86 helpers (32-bit) -----------------

static void emit_x86_prologue(CodeBuf* cb, int frame_size){
  cb_emit1(cb, 0x55);             // push ebp
  cb_emit(cb, "\x89\xE5", 2);     // mov ebp, esp
  if(frame_size > 0){
    cb_emit1(cb, 0x81); cb_emit1(cb, 0xEC); // sub esp, imm32
    cb_emit4(cb, (uint32_t)frame_size);
  }
}

static void emit_x86_epilogue(CodeBuf* cb, int frame_size){
  if(frame_size > 0){
    cb_emit1(cb, 0x81); cb_emit1(cb, 0xC4); // add esp, imm32
    cb_emit4(cb, (uint32_t)frame_size);
  }
  cb_emit1(cb, 0x5D); // pop ebp
  cb_emit1(cb, 0xC3); // ret
}

static void emit_x86_mov_eax_imm32(CodeBuf* cb, uint32_t imm){
  cb_emit1(cb, 0xB8);
  cb_emit4(cb, imm);
}
static void emit_x86_mov_edx_imm32(CodeBuf* cb, uint32_t imm){
  cb_emit1(cb, 0xBA);
  cb_emit4(cb, imm);
}

static void emit_x86_mov_eax_membp(CodeBuf* cb, int disp){
  int disp8 = (-disp >= -128 && -disp <= 127);
  cb_emit1(cb, 0x8B);
  emit_modrm(cb, disp8 ? 1 : 2, 0, 5);
  if(disp8) cb_emit1(cb, (unsigned char)(-disp));
  else cb_emit4(cb, (uint32_t)(-(int32_t)disp));
}
static void emit_x86_mov_edx_membp(CodeBuf* cb, int disp){
  int disp8 = (-disp >= -128 && -disp <= 127);
  cb_emit1(cb, 0x8B);
  emit_modrm(cb, disp8 ? 1 : 2, 2, 5);
  if(disp8) cb_emit1(cb, (unsigned char)(-disp));
  else cb_emit4(cb, (uint32_t)(-(int32_t)disp));
}
static void emit_x86_mov_membp_eax(CodeBuf* cb, int disp){
  int disp8 = (-disp >= -128 && -disp <= 127);
  cb_emit1(cb, 0x89);
  emit_modrm(cb, disp8 ? 1 : 2, 0, 5);
  if(disp8) cb_emit1(cb, (unsigned char)(-disp));
  else cb_emit4(cb, (uint32_t)(-(int32_t)disp));
}
static void emit_x86_mov_membp_edx(CodeBuf* cb, int disp){
  int disp8 = (-disp >= -128 && -disp <= 127);
  cb_emit1(cb, 0x89);
  emit_modrm(cb, disp8 ? 1 : 2, 2, 5);
  if(disp8) cb_emit1(cb, (unsigned char)(-disp));
  else cb_emit4(cb, (uint32_t)(-(int32_t)disp));
}
static void emit_x86_lea_eax_membp(CodeBuf* cb, int disp){
  int disp8 = (-disp >= -128 && -disp <= 127);
  cb_emit1(cb, 0x8D);
  emit_modrm(cb, disp8 ? 1 : 2, 0, 5);
  if(disp8) cb_emit1(cb, (unsigned char)(-disp));
  else cb_emit4(cb, (uint32_t)(-(int32_t)disp));
}

static void emit_x86_push_imm32(CodeBuf* cb, uint32_t imm){
  cb_emit1(cb, 0x68);
  cb_emit4(cb, imm);
}
static void emit_x86_push_reg(CodeBuf* cb, int reg){
  cb_emit1(cb, (unsigned char)(0x50 + (reg & 7)));
}
static void emit_x86_add_esp(CodeBuf* cb, uint32_t imm){
  cb_emit1(cb, 0x81); cb_emit1(cb, 0xC4);
  cb_emit4(cb, imm);
}

static void emit_x86_mov_eax_abs(CodeBuf* cb, uint32_t text_rva, uint32_t target_rva, RelocList* rel){
  emit_x86_mov_eax_imm32(cb, 0x400000u + target_rva);
  reloc_add(rel, text_rva + (uint32_t)cb->len - 4);
}
static void emit_x86_call_moffs(CodeBuf* cb, uint32_t text_rva, uint32_t target_rva, RelocList* rel){
  cb_emit(cb, "\xFF\x15", 2); // call dword ptr [moffs32]
  cb_emit4(cb, 0x400000u + target_rva);
  reloc_add(rel, text_rva + (uint32_t)cb->len - 4);
}
static void emit_x86_fld_abs(CodeBuf* cb, uint32_t text_rva, uint32_t target_rva, RelocList* rel){
  cb_emit(cb, "\xDD\x05", 2); // fld qword [moffs32]
  cb_emit4(cb, 0x400000u + target_rva);
  reloc_add(rel, text_rva + (uint32_t)cb->len - 4);
}

static void emit_x86_movsd_xmm_membp(CodeBuf* cb, int xmm, int disp){
  int disp8 = (-disp >= -128 && -disp <= 127);
  cb_emit(cb, "\xF2\x0F\x10", 3);
  emit_modrm(cb, disp8 ? 1 : 2, xmm, 5);
  if(disp8) cb_emit1(cb, (unsigned char)(-disp));
  else cb_emit4(cb, (uint32_t)(-(int32_t)disp));
}
static void emit_x86_movsd_membp_xmm(CodeBuf* cb, int disp, int xmm){
  int disp8 = (-disp >= -128 && -disp <= 127);
  cb_emit(cb, "\xF2\x0F\x11", 3);
  emit_modrm(cb, disp8 ? 1 : 2, xmm, 5);
  if(disp8) cb_emit1(cb, (unsigned char)(-disp));
  else cb_emit4(cb, (uint32_t)(-(int32_t)disp));
}
static void emit_x86_ucomisd(CodeBuf* cb){
  cb_emit(cb, "\x66\x0F\x2E", 3);
  emit_modrm(cb, 3, 0, 1);
}
static void emit_x86_cvtsi2sd(CodeBuf* cb){
  cb_emit(cb, "\xF2\x0F\x2A", 3);
  emit_modrm(cb, 3, 0, 0);
}
static void emit_x86_cvttsd2si(CodeBuf* cb){
  cb_emit(cb, "\xF2\x0F\x2C", 3);
  emit_modrm(cb, 3, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────
// Linear-Scan Register Allocator (LSRA) for x86-64 PE codegen
//
// Available machine registers (callee-saved, safe to use as temporaries
// across entire function body — we save/restore in prologue/epilogue):
//   R10=10, R11=11, R12=12, R13=13, R14=14, R15=15, RBX=3, RDI=7
//
// Algorithm:
//  1. Compute live ranges: [first_def .. last_use] for each virtual reg.
//  2. Sort virtual regs by live_start.
//  3. Assign machine regs greedily; on conflict, spill the one with latest
//     live_end (furthest future use) — this is the classic LSRA heuristic.
//  4. Produce vreg_to_mreg[]: -1 = spill to stack.  >=0 = machine reg ID.
//
// Call sites should use ra_load() / ra_store() wrappers instead of the
// raw reg_slot_off() emit helpers for the matched virtual registers.
// ─────────────────────────────────────────────────────────────────────────

#define LSRA_NPHY  5

// Physical register IDs usable for allocation. Keep this list to the x64
// callee-saved intersection used by both Win64 and SysV/ELF backends.
static const int lsra_phy[LSRA_NPHY] = {
  12,  // R12
  13,  // R13
  14,  // R14
  15,  // R15
  3,   // RBX
};

// Compute live ranges for all virtual registers in F.
// Returns heap-allocated array of LsraInterval[nreg]; caller must free.
static LsraInterval* lsra_compute_ranges(IRFunc* F, int nreg) {
  if(nreg <= 0) return NULL;
  LsraInterval* iv = (LsraInterval*)malloc(sizeof(LsraInterval) * (size_t)nreg);
  if(!iv) { fprintf(stderr, "lsra: OOM\n"); exit(1); }
  for(int r = 0; r < nreg; r++) {
    iv[r].vreg       = r;
    iv[r].live_start = 0x7fffffff;
    iv[r].live_end   = -1;
    iv[r].mreg       = -1;
  }
  for(int k = 0; k < F->n; k++) {
    IRIns* in = &F->ins[k];
    // definition
    int def = -1;
    if(in->op==I_ICONST || in->op==I_FCONST || in->op==I_MOV ||
       in->op==I_BIN   || in->op==I_FBIN   || in->op==I_CMP ||
       in->op==I_FCMP  || in->op==I_I2F    || in->op==I_F2I ||
       in->op==I_CALL  || in->op==I_CALLPTR|| in->op==I_GEP ||
       in->op==I_LOAD  || in->op==I_SCONST || in->op==I_ADDRSYM){
      def = in->a;
    }
    if(def >= 0 && def < nreg) {
      if(k < iv[def].live_start) iv[def].live_start = k;
      if(k > iv[def].live_end)   iv[def].live_end   = k;
    }
    // uses — mark last use for all source operands
    #define MARK_USE(r) do { if((r)>=0 && (r)<nreg) { if(k>iv[r].live_end) iv[r].live_end=k; } } while(0)
    if(in->op==I_BIN || in->op==I_FBIN || in->op==I_CMP || in->op==I_FCMP){
      MARK_USE(in->b); MARK_USE(in->c);
    }
    if(in->op==I_MOV || in->op==I_I2F || in->op==I_F2I){
      MARK_USE(in->b);
    }
    if(in->op==I_STORE){ MARK_USE(in->a); MARK_USE(in->b); }
    if(in->op==I_LOAD) { MARK_USE(in->b); }
    if(in->op==I_GEP)  { MARK_USE(in->b); }
    if(in->op==I_JZ)   { MARK_USE(in->a); }
    if(in->op==I_RET)  { MARK_USE(in->a); }
    if((in->op==I_CALL || in->op==I_CALLPTR) && in->args) {
      if(in->op==I_CALLPTR) { MARK_USE(in->b); }
      for(int ai=0; ai<in->argc; ai++) MARK_USE(in->args[ai]);
     if(in->a >= 0 && in->a < nreg){
      if(iv[in->a].live_start == 0x7fffffff) iv[in->a].live_start = k;
      iv[in->a].live_end = k + 1;
     }
    }
    #undef MARK_USE
  }

  for(int i=0; i<F->n; i++){
    IRIns* pi = &F->ins[i];
    if(pi->op == I_JMP || pi->op == I_JZ){
      int target_idx = -1;
      int target_label = (pi->op == I_JMP) ? pi->a : pi->b;
      for(int j=0; j<F->n; j++){
        if(F->ins[j].op == I_LABEL && F->ins[j].a == target_label){
          target_idx = j;
          break;
        }
      }
      if(target_idx >= 0){
        for(int r=0; r<nreg; r++){
          if(iv[r].live_start != 0x7fffffff && iv[r].live_start <= target_idx && iv[r].live_end > target_idx){
            if(iv[r].live_end < i + 1){
              iv[r].live_end = i + 1;
            }
          }
        }
      }
    }
  }

  // Any register that was never defined gets a live range covering the whole fn
  // (conservative: don't allocate it to a machine reg)
  for(int r = 0; r < nreg; r++) {
    if(iv[r].live_start == 0x7fffffff) {
      iv[r].live_start = 0;
      iv[r].live_end   = F->n;
    }
  }
  return iv;
}

// Sort intervals by live_start (insertion sort — nreg typically <200)
static void lsra_sort(LsraInterval* iv, int n) {
  for(int i=1; i<n; i++){
    LsraInterval tmp = iv[i];
    int j = i-1;
    while(j>=0 && iv[j].live_start > tmp.live_start) { iv[j+1]=iv[j]; j--; }
    iv[j+1] = tmp;
  }
}

// Main LSRA pass: assign machine registers to virtual registers.
// Returns vreg_to_mreg[nreg]: entry = machine reg (e.g. 10=R10), or -1=spill.
static int* lsra_assign(IRFunc* F, int nreg, int* phy_used_mask_out, LsraInterval** iv_out) {
  if(nreg <= 0) { if(phy_used_mask_out) *phy_used_mask_out=0; if(iv_out) *iv_out=NULL; return NULL; }
  int* vreg_to_mreg = (int*)malloc(sizeof(int) * (size_t)nreg);
  if(!vreg_to_mreg) { fprintf(stderr, "lsra: OOM\n"); exit(1); }
  for(int r=0; r<nreg; r++) vreg_to_mreg[r] = -1;

  LsraInterval* iv = lsra_compute_ranges(F, nreg);
  if(!iv) return vreg_to_mreg;
  lsra_sort(iv, nreg);

  // Active set: intervals currently occupying a physical register.
  // active[i].mreg is the physical register it holds.
  LsraInterval* active = (LsraInterval*)malloc(sizeof(LsraInterval) * LSRA_NPHY);
  if(!active) { fprintf(stderr, "lsra: OOM\n"); exit(1); }
  int n_active = 0;

  // Free list of physical registers
  int free_phy[LSRA_NPHY];
  int n_free = LSRA_NPHY;
  for(int i=0; i<LSRA_NPHY; i++) free_phy[i] = lsra_phy[i];

  int phy_used_mask = 0;

  for(int i=0; i<nreg; i++){
    LsraInterval* cur = &iv[i];
    // Expire old intervals
    for(int j=0; j<n_active; ){
      if(active[j].live_end < cur->live_start){
        // Free the machine reg
        free_phy[n_free++] = active[j].mreg;
        active[j] = active[--n_active];
      } else {
        j++;
      }
    }
    if(n_free == 0){
      // Spill: evict the active interval with the furthest live_end
      int spill_idx = 0;
      for(int j=1; j<n_active; j++){
        if(active[j].live_end > active[spill_idx].live_end) spill_idx = j;
      }
      if(active[spill_idx].live_end > cur->live_end){
        // Spill the active one, give its machine reg to current
        cur->mreg = active[spill_idx].mreg;
        vreg_to_mreg[cur->vreg] = cur->mreg;
        vreg_to_mreg[active[spill_idx].vreg] = -1; // spill old
        active[spill_idx] = *cur;
      }
      // else: spill current (mreg stays -1)
    } else {
      cur->mreg = free_phy[--n_free];
      phy_used_mask |= (1 << cur->mreg);
      vreg_to_mreg[cur->vreg] = cur->mreg;
      // Add to active
      active[n_active++] = *cur;
    }
  }

  free(active);
  if(iv_out) *iv_out = iv;
  else free(iv);
  if(phy_used_mask_out) *phy_used_mask_out = phy_used_mask;
  return vreg_to_mreg;
}

// Emit: load virtual reg into RAX using LSRA result
//   If vreg has a machine reg assigned → emit mov rax, mreg
//   Else → emit mov rax, [rbp - slot]
static void ra_load_rax(CodeBuf* cb, int vreg, const int* vreg_to_mreg, int nreg) {
  if(vreg < 0) return;
  int mreg = (vreg < nreg && vreg_to_mreg) ? vreg_to_mreg[vreg] : -1;
  if(mreg >= 0) {
    emit_mov_rr(cb, 0 /*RAX*/, mreg);
  } else {
    emit_mov_reg_membp(cb, 0, reg_slot_off(vreg));
  }
}

// Emit: store RAX → virtual reg using LSRA result
static void ra_store_rax(CodeBuf* cb, int vreg, const int* vreg_to_mreg, int nreg) {
  if(vreg < 0) return;
  int mreg = (vreg < nreg && vreg_to_mreg) ? vreg_to_mreg[vreg] : -1;
  if(mreg >= 0) {
    emit_mov_rr(cb, mreg, 0 /*RAX*/);
  } else {
    emit_mov_membp_reg(cb, reg_slot_off(vreg), 0);
  }
}

static void ra_load_xmm(CodeBuf* cb, int xmm, int vreg, const int* vreg_to_mreg, int nreg) {
  if(vreg < 0) return;
  int mreg = (vreg < nreg && vreg_to_mreg) ? vreg_to_mreg[vreg] : -1;
  if(mreg >= 0) {
    emit_movq_xmm_reg(cb, xmm, mreg);
  } else {
    emit_movsd_xmm_membp(cb, xmm, reg_slot_off(vreg));
  }
}

static void ra_store_xmm(CodeBuf* cb, int vreg, int xmm, const int* vreg_to_mreg, int nreg) {
  if(vreg < 0) return;
  int mreg = (vreg < nreg && vreg_to_mreg) ? vreg_to_mreg[vreg] : -1;
  if(mreg >= 0) {
    emit_movq_reg_xmm(cb, mreg, xmm);
  } else {
    emit_movsd_membp_xmm(cb, reg_slot_off(vreg), xmm);
  }
}

static void emit_lsra_saves(CodeBuf* cb, int phy_used_mask) {
  static const int callee_saved[] = {12, 13, 14, 15, 3, 7, 6};
  int n_saved = 0;
  for(int i=0; i<7; i++){
    if(phy_used_mask & (1 << callee_saved[i])){
      emit_rex(cb, 1, 0, 0, (callee_saved[i]>>3)&1);
      cb_emit1(cb, (unsigned char)(0x50 + (callee_saved[i] & 7)));
      n_saved++;
    }
  }
  if (n_saved % 2 != 0) {
    cb_emit(cb, "\x48\x83\xEC\x08", 4); // sub rsp, 8
  }
}

static void emit_lsra_restores(CodeBuf* cb, int phy_used_mask) {
  static const int callee_saved[] = {12, 13, 14, 15, 3, 7, 6};
  int n_saved = 0;
  for(int i=0; i<7; i++) if(phy_used_mask & (1 << callee_saved[i])) n_saved++;
  if (n_saved % 2 != 0) {
    cb_emit(cb, "\x48\x83\xC4\x08", 4); // add rsp, 8
  }
  for(int i=6; i>=0; i--){   // pop in reverse order
    if(phy_used_mask & (1 << callee_saved[i])){
      emit_rex(cb, 1, 0, 0, (callee_saved[i]>>3)&1);
      cb_emit1(cb, (unsigned char)(0x58 + (callee_saved[i] & 7)));
    }
  }
}

static int lower_func_x64(IRFunc* fn, CodeBuf* cb, uint32_t text_rva,
                          const uint32_t* str_rva, int str_n,
                          size_t say_off, size_t sayf_off, size_t saystr_off, size_t saymulti_off,
                          size_t sayf_inline_off, size_t saystr_inline_off, uint32_t nl_rva,
                          size_t memcpy_off, size_t memmove_off, size_t memcmp_off,
                          size_t time_off, size_t time_ms_off, size_t time_ns_off,
                          size_t malloc_off, size_t free_off,
                          FuncInfo* finfo, int fcount,
                          GlobalInfo* ginfo, int gcount,
                          const PeImportInfo* imp,
  FuncPatch* fpatches, int* fpatch_n, int fpatch_cap,
  LabelPatch* lpatches, int* lpatch_n, int lpatch_cap){
  (void)sayf_inline_off;
  (void)saystr_inline_off;
  int nreg = (fn->max_reg >= 0) ? (fn->max_reg + 1) : fn->next_reg;
  if(nreg <= 0) nreg = 1;

  // Run linear-scan register allocator: assigns hot virtual regs to machine regs
  int lsra_phy_mask = 0;
  LsraInterval* iv = NULL;
  int* vreg_to_mreg = lsra_assign(fn, nreg, &lsra_phy_mask, &iv);

  int reg_bytes = nreg * 8;
  int off = reg_bytes + 8;
  AllocaMap amap = {0};

  int max_extra = 0;
  for(int k=0;k<fn->n;k++){
    IRIns* in = &fn->ins[k];
    if(in->op == I_ALLOCA){
      int align = in->align ? in->align : 8;
      int base = (int)align_up_u32((uint32_t)off, (uint32_t)align);
      amap_add(&amap, in->a, base + in->size);
      off = base + in->size;
    }
    if(in->op == I_CALL){
      int extra = (in->argc > 4) ? (in->argc - 4) : 0;
      if(extra > max_extra) max_extra = extra;
    }
  }
  int frame = reg_bytes + (off - reg_bytes) + 32 + max_extra*8;
  frame = (int)align_up_u32((uint32_t)frame, 16);

  size_t func_start = cb->len;
  emit_prologue(cb, frame);
  // Save callee-saved machine regs used by LSRA
  emit_lsra_saves(cb, lsra_phy_mask);
  int preg[32];
  int pn = 0;
  if(fn->param_regs && fn->param_regs_n > 0){
    for(int i=0;i<fn->param_regs_n && i<32;i++){
      preg[pn++] = fn->param_regs[i];
    }
  } else {
    for(int i=0;i<fn->n && pn<fn->param_count;i++){
      IRIns* pi = &fn->ins[i];
      if(pi->op == I_LABEL) break;
      if(pi->op == I_STORE){
        preg[pn++] = pi->b;
      }
    }
  }
  if(pn == 0){
    for(int i=0;i<fn->param_count && i<4;i++){
      preg[pn++] = i;
    }
  }
  for(int i=0; i<pn && i<4; i++){
    int reg = (i==0)?1: (i==1)?2: (i==2)?8:9; // RCX, RDX, R8, R9
    int mreg = (preg[i] < nreg && vreg_to_mreg) ? vreg_to_mreg[preg[i]] : -1;
    if(mreg >= 0) {
      emit_mov_rr(cb, mreg, reg);
    } else {
      emit_mov_membp_reg(cb, reg_slot_off(preg[i]), reg);
    }
  }
  for(int i=4; i<pn; i++){
    int stack_off = -(48 + (i - 4) * 8); // e.g. 5th arg is [rbp+48]
    int mreg = (preg[i] < nreg && vreg_to_mreg) ? vreg_to_mreg[preg[i]] : -1;
    if(mreg >= 0) {
      emit_mov_reg_membp(cb, mreg, stack_off);
    } else {
      emit_mov_reg_membp(cb, 0, stack_off); // rax
      emit_mov_membp_reg(cb, reg_slot_off(preg[i]), 0);
    }
  }

  int lbl_n = fn->next_label > 0 ? fn->next_label : 0;
  size_t* label_off = NULL;
  if(lbl_n > 0){
    label_off = (size_t*)malloc(sizeof(size_t) * (size_t)lbl_n);
    for(int i=0;i<lbl_n;i++) label_off[i] = (size_t)-1;
  }

  size_t ret_patches[512];
  int ret_n = 0;

  for(int k=0;k<fn->n;k++){
    IRIns* in = &fn->ins[k];
    switch(in->op){
      case I_LABEL:
        if(label_off && in->a >=0 && in->a < lbl_n){
          label_off[in->a] = cb->len;
        }
        break;
      case I_JMP: {
        cb_emit1(cb, 0xE9);
        size_t pos = cb->len;
        cb_emit4(cb, 0);
        if(*lpatch_n < lpatch_cap){
          lpatches[*lpatch_n].pos = pos;
          lpatches[*lpatch_n].label = in->a;
          (*lpatch_n)++;
        }
        break;
      }
      case I_JZ: {
        ra_load_rax(cb, in->a, vreg_to_mreg, nreg);
        cb_emit(cb, "\x48\x85\xC0", 3);
        cb_emit(cb, "\x0F\x84", 2);
        size_t pos = cb->len;
        cb_emit4(cb, 0);
        if(*lpatch_n < lpatch_cap){
          lpatches[*lpatch_n].pos = pos;
          lpatches[*lpatch_n].label = in->b;
          (*lpatch_n)++;
        }
        break;
      }
      case I_ICONST:
        emit_mov_reg_imm64(cb, 0, (uint64_t)in->imm);
        ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
        break;
      case I_ADDRSYM: {
        const char* nm = in->name;
        int nlen = in->nlen;
        int done = 0;
        if(nlen==3 && strncmp(nm, "say", 3)==0){
          emit_lea_reg_rip(cb, 0, text_rva, text_rva + (uint32_t)say_off);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==5 && strncmp(nm, "say_f", 5)==0){
          emit_lea_reg_rip(cb, 0, text_rva, text_rva + (uint32_t)sayf_off);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==7 && strncmp(nm, "say_str", 7)==0){
          emit_lea_reg_rip(cb, 0, text_rva, text_rva + (uint32_t)saystr_off);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==9 && strncmp(nm, "say_multi", 9)==0){
          emit_lea_reg_rip(cb, 0, text_rva, text_rva + (uint32_t)saymulti_off);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==6 && strncmp(nm, "memcpy", 6)==0){
          emit_lea_reg_rip(cb, 0, text_rva, text_rva + (uint32_t)memcpy_off);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==7 && strncmp(nm, "memmove", 7)==0){
          emit_lea_reg_rip(cb, 0, text_rva, text_rva + (uint32_t)memmove_off);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==6 && strncmp(nm, "memcmp", 6)==0){
          emit_lea_reg_rip(cb, 0, text_rva, text_rva + (uint32_t)memcmp_off);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==3 && strncmp(nm, "len", 3)==0){
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrlen);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==6 && strncmp(nm, "strcmp", 6)==0){
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrcmp);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==8 && strncmp(nm, "tn_strcmp", 8)==0){
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrcmp);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==8 && strncmp(nm, "time_now", 8)==0){
          emit_lea_reg_rip(cb, 0, text_rva, text_rva + (uint32_t)time_off);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==11 && strncmp(nm, "time_now_ms", 11)==0){
          emit_lea_reg_rip(cb, 0, text_rva, text_rva + (uint32_t)time_ms_off);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==11 && strncmp(nm, "time_now_ns", 11)==0){
          emit_lea_reg_rip(cb, 0, text_rva, text_rva + (uint32_t)time_ns_off);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==6 && strncmp(nm, "malloc", 6)==0){
          emit_lea_reg_rip(cb, 0, text_rva, text_rva + (uint32_t)malloc_off);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==4 && strncmp(nm, "free", 4)==0){
          emit_lea_reg_rip(cb, 0, text_rva, text_rva + (uint32_t)free_off);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==5 && strncmp(nm, "fopen", 5)==0){
          if(!ensure_iat(imp->iat_fopen, "fopen")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fopen);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==6 && strncmp(nm, "fclose", 6)==0){
          if(!ensure_iat(imp->iat_fclose, "fclose")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fclose);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==5 && strncmp(nm, "fread", 5)==0){
          if(!ensure_iat(imp->iat_fread, "fread")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fread);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==6 && strncmp(nm, "fwrite", 6)==0){
          if(!ensure_iat(imp->iat_fwrite, "fwrite")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fwrite);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==5 && strncmp(nm, "fseek", 5)==0){
          if(!ensure_iat(imp->iat_fseek, "fseek")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fseek);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==5 && strncmp(nm, "ftell", 5)==0){
          if(!ensure_iat(imp->iat_ftell, "ftell")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_ftell);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==6 && strncmp(nm, "fflush", 6)==0){
          if(!ensure_iat(imp->iat_fflush, "fflush")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fflush);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==6 && strncmp(nm, "io_err", 6)==0){
          if(!ensure_iat(imp->iat_io_err, "io_err")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_io_err);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==18 && strncmp(nm, "CreateFileMappingA", 18)==0){
          if(!ensure_iat(imp->iat_createfilemap, "CreateFileMappingA")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_createfilemap);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==13 && strncmp(nm, "MapViewOfFile", 13)==0){
          if(!ensure_iat(imp->iat_mapview, "MapViewOfFile")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_mapview);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==15 && strncmp(nm, "UnmapViewOfFile", 15)==0){
          if(!ensure_iat(imp->iat_unmapview, "UnmapViewOfFile")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_unmapview);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==12 && strncmp(nm, "CreateThread", 12)==0){
          if(!ensure_iat(imp->iat_createthread, "CreateThread")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_createthread);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==19 && strncmp(nm, "WaitForSingleObject", 19)==0){
          if(!ensure_iat(imp->iat_waitforsingleobject, "WaitForSingleObject")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_waitforsingleobject);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==5 && strncmp(nm, "Sleep", 5)==0){
          if(!ensure_iat(imp->iat_sleep, "Sleep")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_sleep);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==11 && strncmp(nm, "CreateFileA", 11)==0){
          if(!ensure_iat(imp->iat_createfile, "CreateFileA")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_createfile);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==11 && strncmp(nm, "CloseHandle", 11)==0){
          if(!ensure_iat(imp->iat_closehandle, "CloseHandle")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_closehandle);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==6 && strncmp(nm, "io_eof", 6)==0){
          if(!ensure_iat(imp->iat_io_eof, "io_eof")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_io_eof);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==9 && strncmp(nm, "read_line", 9)==0){
          if(!ensure_iat(imp->iat_read_line, "read_line")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_read_line);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==10 && strncmp(nm, "read_bytes", 10)==0){
          if(!ensure_iat(imp->iat_read_bytes, "read_bytes")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_read_bytes);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==10 && strncmp(nm, "write_line", 10)==0){
          if(!ensure_iat(imp->iat_write_line, "write_line")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_write_line);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==5 && strncmp(nm, "stdin", 5)==0){
          if(!ensure_iat(imp->iat_stdin, "stdin")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_stdin);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==6 && strncmp(nm, "stdout", 6)==0){
          if(!ensure_iat(imp->iat_stdout, "stdout")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_stdout);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        } else if(nlen==6 && strncmp(nm, "stderr", 6)==0){
          if(!ensure_iat(imp->iat_stderr, "stderr")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_stderr);
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
          done = 1;
        }
        for(int gi=0; gi<gcount && !done; gi++){
          if(!ginfo[gi].is_extern && ginfo[gi].len==nlen &&
             strncmp(ginfo[gi].name, nm, (size_t)nlen)==0){
            emit_lea_reg_rip(cb, 0, text_rva, ginfo[gi].rva);
            ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
            done = 1;
            break;
          }
        }
        if(!done){
          for(int fi=0; fi<fcount; fi++){
            if(finfo[fi].len==nlen && strncmp(finfo[fi].name, nm, (size_t)nlen)==0){
              if(!finfo[fi].is_extern){
                emit_lea_reg_rip(cb, 0, text_rva, text_rva + (uint32_t)finfo[fi].off);
                ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
                done = 1;
                break;
              }
              // extern function pointers: allow known mappings
              if(nlen==3 && strncmp(nm, "len", 3)==0){
                emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrlen);
                ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
                done = 1;
                break;
              }
              if(nlen==6 && strncmp(nm, "strcmp", 6)==0){
                emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrcmp);
                ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
                done = 1;
                break;
              }
              if(nlen==8 && strncmp(nm, "tn_strcmp", 8)==0){
                emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrcmp);
                ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
                done = 1;
                break;
              }
            }
          }
        }
        if(!done){
          fprintf(stderr, "buildexe: addrsym unresolved '%.*s'\n", nlen, nm);
          free(amap.dst_reg);
          free(amap.off_bytes);
          free(label_off);
          return -1;
        }
        break;
      }
      case I_SCONST:
        if(in->sid < 0 || in->sid >= str_n){
          fprintf(stderr, "buildexe: sconst out of range (%d)\n", in->sid);
          free(amap.dst_reg);
          free(amap.off_bytes);
          free(label_off);
          return -1;
        }
        emit_lea_reg_rip(cb, 0, text_rva, str_rva[in->sid]);
        ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
        break;
      case I_MOV:
        ra_load_rax(cb, in->b, vreg_to_mreg, nreg);
        ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
        break;
      case I_BIN: {
        ra_load_rax(cb, in->b, vreg_to_mreg, nreg);
        int mreg_c = (in->c < nreg && vreg_to_mreg) ? vreg_to_mreg[in->c] : -1; if(mreg_c >= 0) emit_mov_rr(cb, 1 /*RCX*/, mreg_c); else emit_mov_reg_membp(cb, 1, reg_slot_off(in->c));
        switch(in->binop){
          case B_ADD: emit_add_rax_rcx(cb); break;
          case B_SUB: emit_sub_rax_rcx(cb); break;
          case B_MUL: emit_imul_rax_rcx(cb); break;
          case B_AND: emit_and_rax_rcx(cb); break;
          case B_OR:  emit_or_rax_rcx(cb); break;
          case B_XOR: emit_xor_rax_rcx(cb); break;
          case B_SHL: emit_shl_rax_cl(cb); break;
          case B_SHR: emit_sar_rax_cl(cb); break;
          case B_DIV:
          case B_MOD:
            emit_cqo(cb);
            emit_idiv_rcx(cb);
            if(in->binop == B_MOD){
              emit_mov_reg_reg(cb, 0, 2); // rax = rdx
            }
            break;
          default:
            fprintf(stderr, "buildexe: unsupported binop %d\n", in->binop);
            free(amap.dst_reg);
            free(amap.off_bytes);
            free(label_off);
            return -1;
        }
        ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
        break;
      }
      case I_CMP: {
        ra_load_rax(cb, in->b, vreg_to_mreg, nreg);
        int mreg_c = (in->c < nreg && vreg_to_mreg) ? vreg_to_mreg[in->c] : -1; if(mreg_c >= 0) emit_mov_rr(cb, 1 /*RCX*/, mreg_c); else emit_mov_reg_membp(cb, 1, reg_slot_off(in->c));
        emit_cmp_rax_rcx(cb);
        switch(in->cmpop){
          case C_EQ:  emit_setcc(cb, 4); break;  // sete
          case C_NEQ: emit_setcc(cb, 5); break;  // setne
          case C_LT:  emit_setcc(cb, 12); break; // setl
          case C_LTE: emit_setcc(cb, 14); break; // setle
          case C_GT:  emit_setcc(cb, 15); break; // setg
          case C_GTE: emit_setcc(cb, 13); break; // setge
          default: emit_setcc(cb, 4); break;
        }
        emit_movzx_eax_al(cb);
        ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
        break;
      }
      case I_FCONST:
        emit_mov_reg_imm64(cb, 0, (uint64_t)in->immf);
        ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
        break;
      case I_FBIN:
        ra_load_xmm(cb, 0, in->b, vreg_to_mreg, nreg);
        ra_load_xmm(cb, 1, in->c, vreg_to_mreg, nreg);
        switch(in->fop){
          case F_ADD: emit_fbin(cb, 0); break;
          case F_SUB: emit_fbin(cb, 1); break;
          case F_MUL: emit_fbin(cb, 2); break;
          case F_DIV: emit_fbin(cb, 3); break;
          default: emit_fbin(cb, 0); break;
        }
        ra_store_xmm(cb, in->a, 0, vreg_to_mreg, nreg);
        break;
      case I_FCMP:
        ra_load_xmm(cb, 0, in->b, vreg_to_mreg, nreg);
        ra_load_xmm(cb, 1, in->c, vreg_to_mreg, nreg);
        emit_ucomisd(cb);
        switch(in->cmpop){
          case C_EQ:  emit_setcc(cb, 4); break;  // sete
          case C_NEQ: emit_setcc(cb, 5); break;  // setne
          case C_LT:  emit_setcc(cb, 2); break;  // setb
          case C_LTE: emit_setcc(cb, 6); break;  // setbe
          case C_GT:  emit_setcc(cb, 7); break;  // seta
          case C_GTE: emit_setcc(cb, 3); break;  // setae
          default: emit_setcc(cb, 4); break;
        }
        emit_movzx_eax_al(cb);
        ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
        break;
      case I_I2F:
        ra_load_rax(cb, in->b, vreg_to_mreg, nreg);
        emit_cvtsi2sd(cb);
        ra_store_xmm(cb, in->a, 0, vreg_to_mreg, nreg);
        break;
      case I_F2I:
        ra_load_xmm(cb, 0, in->b, vreg_to_mreg, nreg);
        emit_cvttsd2si(cb);
        ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
        break;
      case I_ALLOCA: {
        int aoff = amap_find(&amap, in->a);
        if(aoff < 0){
          free(amap.dst_reg);
          free(amap.off_bytes);
          free(label_off);
          return -1;
        }
        emit_lea_reg_membp(cb, 0, aoff);
        ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
        break;
      }
      case I_GEP:
        ra_load_rax(cb, in->b, vreg_to_mreg, nreg);
        if(in->imm >= -128 && in->imm <= 127){
          cb_emit(cb, "\x48\x83\xC0", 3);
          cb_emit1(cb, (unsigned char)(in->imm & 0xFF));
        } else {
          cb_emit(cb, "\x48\x05", 2);
          cb_emit4(cb, (uint32_t)in->imm);
        }
        ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
        break;
      case I_LOAD:
        ra_load_rax(cb, in->b, vreg_to_mreg, nreg);
        if(in->size == 1){
          if(in->is_unsigned) cb_emit(cb, "\x0F\xB6\x00", 3);
          else cb_emit(cb, "\x48\x0F\xBE\x00", 4);
        } else if(in->size == 2){
          if(in->is_unsigned) cb_emit(cb, "\x0F\xB7\x00", 3);
          else cb_emit(cb, "\x48\x0F\xBF\x00", 4);
        } else if(in->size == 4){
          if(in->is_unsigned) cb_emit(cb, "\x8B\x00", 2);
          else cb_emit(cb, "\x48\x63\x00", 3);
        } else {
          cb_emit(cb, "\x48\x8B\x00", 3);
        }
        ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
        break;
      case I_STORE: {
        int mreg_b = (in->b >= 0 && in->b < nreg && vreg_to_mreg) ? vreg_to_mreg[in->b] : -1;
        if(mreg_b >= 0) emit_mov_rr(cb, 1, mreg_b);
        else emit_mov_reg_membp(cb, 1, reg_slot_off(in->b));
        
        ra_load_rax(cb, in->a, vreg_to_mreg, nreg);

        if(in->size == 1){
          cb_emit(cb, "\x88\x08", 2);
        } else if(in->size == 2){
          cb_emit(cb, "\x66\x89\x08", 3);
        } else if(in->size == 4){
          cb_emit(cb, "\x89\x08", 2);
        } else {
          cb_emit(cb, "\x48\x89\x08", 3);
        }
        break;
      }
      case I_CALL: {
        const char* name = in->name;
        int nlen = in->nlen;
        if(!name || (in->argc > 0 && !in->args)){
          free(amap.dst_reg);
          free(amap.off_bytes);
          free(label_off);
          return -1;
        }
        /* Strip recognised module prefixes so bare-name dispatch works */
        if(nlen > 8 && strncmp(name, "gui_win.", 8)==0){ name += 8; nlen -= 8; }
        if(nlen > 3 && strncmp(name, "os.", 3)==0){ name += 3; nlen -= 3; }
        int pushed_mregs = 0;
        int total = emit_call_prepare(cb, in->argc, in->args, vreg_to_mreg, nreg, iv, k, &pushed_mregs);
        if(nlen==9 && strncmp(name, "say_multi", 9)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)saymulti_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
        } else if(nlen==5 && strncmp(name, "say_f", 5)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)sayf_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
        } else if(nlen==6 && strncmp(name, "memcpy", 6)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)memcpy_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
        } else if(nlen==7 && strncmp(name, "memmove", 7)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)memmove_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
        } else if(nlen==6 && strncmp(name, "memcmp", 6)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)memcmp_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
        } else if(nlen==8 && strncmp(name, "time_now", 8)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)time_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
        } else if(nlen==11 && strncmp(name, "time_now_ms", 11)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)time_ms_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
        } else if(nlen==11 && strncmp(name, "time_now_ns", 11)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)time_ns_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
        } else if(nlen==10 && strncmp(name, "args_count", 10)==0){
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(nlen==3 && strncmp(name, "arg", 3)==0){
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(nlen==13 && strncmp(name, "async_workers", 13)==0){
          emit_mov_reg_imm64(cb, 0, 1);
        } else if(nlen==7 && strncmp(name, "os_name", 7)==0){
          cb_emit(cb, "\x48\xC7\xC1\x08\x00\x00\x00", 7); // mov rcx, 8
          { int32_t disp = (int32_t)((int64_t)(text_rva + malloc_off) - (int64_t)(text_rva + (uint32_t)(cb->len + 5))); emit_call_rel32(cb, disp); }
          cb_emit(cb, "\x48\xB9\x77\x69\x6E\x64\x6F\x77\x73\x00", 10); // mov rcx, "windows\0"
          cb_emit(cb, "\x48\x89\x08", 3); // mov [rax], rcx
        } else if(nlen==7 && strncmp(name, "fb_addr", 7)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_fb_addr);
          emit_call_reg(cb, 0);
        } else if(nlen==8 && strncmp(name, "fb_width", 8)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_fb_width);
          emit_call_reg(cb, 0);
        } else if(nlen==9 && strncmp(name, "fb_height", 9)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_fb_height);
          emit_call_reg(cb, 0);
        } else if(nlen==8 && strncmp(name, "fb_pitch", 8)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_fb_pitch);
          emit_call_reg(cb, 0);
        } else if(nlen==7 && strncmp(name, "get_env", 7)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_getenv_str);
          emit_call_reg(cb, 0);
        } else if(nlen==19 && strncmp(name, "color_is_tty_stdout", 19)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_color_is_tty_stdout);
          emit_call_reg(cb, 0);
        } else if(nlen==12 && strncmp(name, "tts_play_pcm", 12)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_tts_play_pcm);
          emit_call_reg(cb, 0);
        } else if(nlen==15 && strncmp(name, "stt_capture_mic", 15)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_stt_capture_mic);
          emit_call_reg(cb, 0);
        } else if(nlen==9 && strncmp(name, "os_system", 9)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_os_system);
          emit_call_reg(cb, 0);
        } else if(nlen==12 && strncmp(name, "io_mmap_open", 12)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_mmap_file);
          emit_call_reg(cb, 0);
        } else if(nlen==13 && strncmp(name, "io_mmap_close", 13)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_munmap_file);
          emit_call_reg(cb, 0);
        } else if(nlen==14 && strncmp(name, "io_pipe_create", 14)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_io_pipe_create);
          emit_call_reg(cb, 0);
        } else if(nlen==10 && strncmp(name, "io_fd_read", 10)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_io_fd_read);
          emit_call_reg(cb, 0);
        } else if(nlen==11 && strncmp(name, "io_fd_write", 11)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_io_fd_write);
          emit_call_reg(cb, 0);
        } else if(nlen==11 && strncmp(name, "io_fd_close", 11)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_io_fd_close);
          emit_call_reg(cb, 0);
        } else if(nlen==6 && strncmp(name, "fb_bpp", 6)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_fb_bpp);
          emit_call_reg(cb, 0);
        } else if(nlen==12 && strncmp(name, "screen_width", 12)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_screen_width);
          emit_call_reg(cb, 0);
        } else if(nlen==13 && strncmp(name, "screen_height", 13)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_screen_height);
          emit_call_reg(cb, 0);
        } else if(nlen==12 && strncmp(name, "fb_fill_rect", 12)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_fb_fill_rect);
          emit_call_reg(cb, 0);
        } else if(nlen==12 && strncmp(name, "fb_put_pixel", 12)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_fb_put_pixel);
          emit_call_reg(cb, 0);
        } else if(nlen==10 && strncmp(name, "fb_init_ex", 10)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_fb_init_ex);
          emit_call_reg(cb, 0);
        } else if(nlen==10 && strncmp(name, "fb_present", 10)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_fb_present);
          emit_call_reg(cb, 0);
        } else if(nlen==7 && strncmp(name, "fb_fill", 7)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_fb_fill);
          emit_call_reg(cb, 0);
        } else if(nlen==15 && strncmp(name, "fb_fill_rounded", 15)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_fb_fill_rounded);
          emit_call_reg(cb, 0);
        } else if(nlen==14 && strncmp(name, "fb_soft_cursor", 14)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_fb_soft_cursor);
          emit_call_reg(cb, 0);
        } else if(nlen==7 && strncmp(name, "fb_text", 7)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_fb_text);
          emit_call_reg(cb, 0);
        } else if(nlen==10 && strncmp(name, "fb_text_ex", 10)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_fb_text_ex);
          emit_call_reg(cb, 0);
        } else if(nlen==13 && strncmp(name, "kbd_has_event", 13)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_kbd_has_event);
          emit_call_reg(cb, 0);
        } else if(nlen==13 && strncmp(name, "kbd_read_char", 13)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_kbd_read_char);
          emit_call_reg(cb, 0);
        } else if(nlen==16 && strncmp(name, "mouse_has_packet", 16)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_mouse_has_packet);
          emit_call_reg(cb, 0);
        } else if(nlen==8 && strncmp(name, "mouse_dx", 8)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_mouse_dx);
          emit_call_reg(cb, 0);
        } else if(nlen==8 && strncmp(name, "mouse_dy", 8)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_mouse_dy);
          emit_call_reg(cb, 0);
        } else if(nlen==13 && strncmp(name, "mouse_buttons", 13)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_mouse_buttons);
          emit_call_reg(cb, 0);
        } else if(nlen==11 && strncmp(name, "mouse_pos_x", 11)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_mouse_pos_x);
          emit_call_reg(cb, 0);
        } else if(nlen==11 && strncmp(name, "mouse_pos_y", 11)==0){
          emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_host_mouse_pos_y);
          emit_call_reg(cb, 0);
        } else if(nlen==13 && strncmp(name, "async_pending", 13)==0){
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(nlen==8 && strncmp(name, "sleep_ms", 8)==0){
          if(imp->iat_sleep){
            emit_mov_reg_rip(cb, 0, text_rva, imp->iat_sleep);
            emit_call_reg(cb, 0);
          }
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(nlen==7 && strncmp(name, "get_cwd", 7)==0){
          if(!ensure_iat(imp->iat_getcurdir, "get_cwd->GetCurrentDirectoryA")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          cb_emit(cb, "\x48\x83\xEC\x30", 4); // sub rsp, 48
          cb_emit(cb, "\x48\xC7\xC1\x04\x01\x00\x00", 7); // mov rcx, 260
          { int32_t disp = (int32_t)((int64_t)(text_rva + malloc_off) - (int64_t)(text_rva + (uint32_t)(cb->len + 5))); emit_call_rel32(cb, disp); }
          cb_emit(cb, "\x48\x89\x44\x24\x20", 5); // mov [rsp+0x20], rax (buf)
          cb_emit(cb, "\x48\x89\xC2", 3); // mov rdx, rax (buf)
          cb_emit(cb, "\x48\xC7\xC1\x04\x01\x00\x00", 7); // mov rcx, 260
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_getcurdir);
          emit_call_reg(cb, 0);
          cb_emit(cb, "\x48\x8B\x44\x24\x20", 5); // mov rax, [rsp+0x20] (buf)
          cb_emit(cb, "\x48\x83\xC4\x30", 4); // add rsp, 48
        } else if(nlen==8 && strncmp(name, "path_sep", 8)==0){
          // Windows path separator fallback.
          emit_mov_reg_imm64(cb, 0, 92);
        } else if(nlen==9 && strncmp(name, "path_join", 9)==0){
          // buildexe fallback lane: return a valid static string pointer.
          emit_lea_reg_rip(cb, 0, text_rva, nl_rva);
        } else if(nlen==13 && strncmp(name, "path_basename", 13)==0){
          emit_lea_reg_rip(cb, 0, text_rva, nl_rva);
        } else if(nlen==12 && strncmp(name, "path_dirname", 12)==0){
          emit_lea_reg_rip(cb, 0, text_rva, nl_rva);
        } else if(nlen==8 && strncmp(name, "list_dir", 8)==0){
          if(imp->iat_list_dir){
            emit_mov_reg_rip(cb, 0, text_rva, imp->iat_list_dir);
            emit_call_reg(cb, 0);
          } else if(imp->iat_findfirstfile && imp->iat_findnextfile && imp->iat_findclose){
            if(emit_win_list_dir_inline(cb, text_rva, imp, malloc_off, free_off) != 0){
              free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1;
            }
          } else {
            emit_mov_reg_imm64(cb, 0, 0);
          }
        } else if(nlen==18 && strncmp(name, "list_dir_recursive", 18)==0){
          int tidx = raw_io_bridge_target(finfo, fcount, name, nlen);
          if(tidx >= 0){
            if(emit_func_patch_call(cb, fpatches, fpatch_n, fpatch_cap, tidx) != 0){
              free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1;
            }
          } else {
            emit_mov_reg_imm64(cb, 0, 0);
          }
        } else if(nlen==4 && strncmp(name, "glob", 4)==0){
          int tidx = raw_io_bridge_target(finfo, fcount, name, nlen);
          if(tidx >= 0){
            if(emit_func_patch_call(cb, fpatches, fpatch_n, fpatch_cap, tidx) != 0){
              free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1;
            }
          } else {
            emit_mov_reg_imm64(cb, 0, 0);
          }
        } else if(nlen==14 && strncmp(name, "path_normalize", 14)==0){
          emit_lea_reg_rip(cb, 0, text_rva, nl_rva);
        } else if(nlen==19 && strncmp(name, "path_normalize_opts", 19)==0){
          emit_lea_reg_rip(cb, 0, text_rva, nl_rva);
        } else if(nlen==11 && strncmp(name, "path_exists", 11)==0){
          if(!ensure_iat(imp->iat_getfileattr, "path_exists->GetFileAttributesA")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          cb_emit(cb, "\x48\x83\xEC\x28", 4); // sub rsp, 40
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_getfileattr);
          emit_call_reg(cb, 0);
          cb_emit(cb, "\x48\x83\xC4\x28", 4); // add rsp, 40
          cb_emit(cb, "\x83\xF8\xFF", 3); // cmp eax, -1
          cb_emit(cb, "\x0F\x95\xC0", 3); // setne al
          cb_emit(cb, "\x48\x0F\xB6\xC0", 4); // movzx rax, al
        } else if(nlen==11 && strncmp(name, "path_is_dir", 11)==0){
          if(!ensure_iat(imp->iat_getfileattr, "path_is_dir->GetFileAttributesA")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          cb_emit(cb, "\x48\x83\xEC\x28", 4); // sub rsp, 40
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_getfileattr);
          emit_call_reg(cb, 0);
          cb_emit(cb, "\x48\x83\xC4\x28", 4); // add rsp, 40
          cb_emit(cb, "\x83\xF8\xFF", 3); // cmp eax, -1
          cb_emit(cb, "\x74\x0A", 2); // jz fail
          cb_emit(cb, "\x48\x83\xE0\x10", 4); // and rax, 16
          cb_emit(cb, "\x0F\x95\xC0", 3); // setne al
          cb_emit(cb, "\x48\x0F\xB6\xC0", 4); // movzx rax, al
          cb_emit(cb, "\xEB\x03", 2); // jmp done
          // fail:
          cb_emit(cb, "\x48\x31\xC0", 3); // xor rax, rax
          // done:
        } else if(nlen==5 && strncmp(name, "chdir", 5)==0){
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(nlen==5 && strncmp(name, "mkdir", 5)==0){
          if(!ensure_iat(imp->iat_createdir, "mkdir->CreateDirectoryA")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          cb_emit(cb, "\x48\x31\xD2", 3); // xor rdx, rdx
          cb_emit(cb, "\x48\x83\xEC\x28", 4); // sub rsp, 40
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_createdir);
          emit_call_reg(cb, 0);
          cb_emit(cb, "\x48\x83\xC4\x28", 4); // add rsp, 40
          cb_emit(cb, "\x48\x85\xC0", 3); // test rax, rax
          cb_emit(cb, "\x0F\x94\xC0", 3); // setz al
          cb_emit(cb, "\x48\x0F\xB6\xC0", 4); // movzx rax, al
        } else if(nlen==5 && strncmp(name, "rmdir", 5)==0){
          if(!ensure_iat(imp->iat_removedir, "rmdir->RemoveDirectoryA")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          cb_emit(cb, "\x48\x83\xEC\x28", 4); // sub rsp, 40
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_removedir);
          emit_call_reg(cb, 0);
          cb_emit(cb, "\x48\x83\xC4\x28", 4); // add rsp, 40
          cb_emit(cb, "\x48\x85\xC0", 3); // test rax, rax
          cb_emit(cb, "\x0F\x94\xC0", 3); // setz al
          cb_emit(cb, "\x48\x0F\xB6\xC0", 4); // movzx rax, al
        } else if(nlen==6 && strncmp(name, "remove", 6)==0){
          // C remove(path)->int through UCRT, preserving C-compatible return codes.
          if(!ensure_iat(imp->iat_remove, "remove")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_remove);
          emit_call_reg(cb, 0);
        } else if(nlen==6 && strncmp(name, "rename", 6)==0){
          // C rename(old,new)->int through UCRT, preserving C-compatible return codes.
          if(!ensure_iat(imp->iat_rename, "rename")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_rename);
          emit_call_reg(cb, 0);
        } else if(nlen==9 && strncmp(name, "file_size", 9)==0){
          // file_size(path:rcx)->int via CreateFileA+GetFileSizeEx+CloseHandle
          if(!ensure_iat(imp->iat_createfile, "file_size->CreateFileA")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          if(!ensure_iat(imp->iat_getfilesize, "file_size->GetFileSizeEx")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          if(!ensure_iat(imp->iat_closehandle, "file_size->CloseHandle")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          cb_emit(cb, "\x48\x83\xEC\x40", 4);            // sub rsp, 0x40
          cb_emit(cb, "\x48\x89\x4C\x24\x38", 5);        // mov [rsp+0x38], rcx (path)
          cb_emit(cb, "\x48\xB8\x00\x00\x00\x80\x00\x00\x00\x00", 10); // mov rax, 0x80000000
          cb_emit(cb, "\x48\x89\xC2", 3);                // mov rdx, rax (GENERIC_READ)
          cb_emit(cb, "\x49\xB8\x01\x00\x00\x00\x00\x00\x00\x00", 10); // mov r8, 1 (FILE_SHARE_READ)
          cb_emit(cb, "\x49\xB9\x00\x00\x00\x00\x00\x00\x00\x00", 10); // mov r9, 0
          cb_emit(cb, "\x48\xC7\x44\x24\x20\x03\x00\x00\x00", 9);       // mov [rsp+0x20], 3 (OPEN_EXISTING)
          cb_emit(cb, "\x48\xC7\x44\x24\x28\x00\x00\x00\x00", 9);
          cb_emit(cb, "\x48\xC7\x44\x24\x30\x00\x00\x00\x00", 9);
          cb_emit(cb, "\x48\x8B\x4C\x24\x38", 5);        // restore rcx=path
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_createfile);
          emit_call_reg(cb, 0);
          cb_emit(cb, "\x48\x83\xF8\xFF", 4);            // cmp rax, -1
          size_t jz_fs_fail = cb->len;
          cb_emit(cb, "\x74\x00", 2);
          cb_emit(cb, "\x48\x89\x44\x24\x28", 5);        // save handle
          cb_emit(cb, "\x48\x89\xC1", 3);                // mov rcx, handle
          cb_emit(cb, "\x48\x8D\x54\x24\x20", 5);        // lea rdx, [rsp+0x20]
          cb_emit(cb, "\x48\xC7\x44\x24\x20\x00\x00\x00\x00", 9); // init size=0
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_getfilesize);
          emit_call_reg(cb, 0);
          cb_emit(cb, "\x48\x8B\x4C\x24\x28", 5);        // mov rcx, handle
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_closehandle);
          emit_call_reg(cb, 0);
          cb_emit(cb, "\x48\x8B\x44\x24\x20", 5);        // mov rax, size
          size_t fs_done = cb->len;
          cb_emit(cb, "\xEB\x0B", 2);
          size_t fs_fail = cb->len;
          cb->data[jz_fs_fail + 1] = (unsigned char)(fs_fail - (jz_fs_fail + 2));
          emit_mov_reg_imm64(cb, 0, (uint64_t)-1LL);
          cb->data[fs_done + 1] = (unsigned char)(cb->len - (fs_done + 2));
          cb_emit(cb, "\x48\x83\xC4\x40", 4);            // add rsp, 0x40
        } else if(nlen==9 && strncmp(name, "read_file", 9)==0){
          // read_file(path:rcx)->str via CreateFileA+GetFileSizeEx+malloc+ReadFile+CloseHandle
          if(!ensure_iat(imp->iat_createfile, "read_file->CreateFileA")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          if(!ensure_iat(imp->iat_getfilesize, "read_file->GetFileSizeEx")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          if(!ensure_iat(imp->iat_readfile, "read_file->ReadFile")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          if(!ensure_iat(imp->iat_closehandle, "read_file->CloseHandle")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          cb_emit(cb, "\x48\x83\xEC\x60", 4);            // sub rsp, 0x60
          cb_emit(cb, "\x48\x89\x4C\x24\x50", 5);        // mov [rsp+0x50], rcx (path)
          cb_emit(cb, "\x48\xB8\x00\x00\x00\x80\x00\x00\x00\x00", 10);
          cb_emit(cb, "\x48\x89\xC2", 3);
          cb_emit(cb, "\x49\xB8\x01\x00\x00\x00\x00\x00\x00\x00", 10);
          cb_emit(cb, "\x49\xB9\x00\x00\x00\x00\x00\x00\x00\x00", 10);
          cb_emit(cb, "\x48\xC7\x44\x24\x20\x03\x00\x00\x00", 9);
          cb_emit(cb, "\x48\xC7\x44\x24\x28\x00\x00\x00\x00", 9);
          cb_emit(cb, "\x48\xC7\x44\x24\x30\x00\x00\x00\x00", 9);
          cb_emit(cb, "\x48\x8B\x4C\x24\x50", 5);        // restore rcx=path
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_createfile);
          emit_call_reg(cb, 0);
          cb_emit(cb, "\x48\x83\xF8\xFF", 4);
          size_t jz_rf_fail = cb->len;
          cb_emit(cb, "\x74\x00", 2);
          cb_emit(cb, "\x48\x89\x44\x24\x48", 5);        // save handle at [rsp+0x48]
          cb_emit(cb, "\x48\x89\xC1", 3);
          cb_emit(cb, "\x48\x8D\x54\x24\x40", 5);        // lea rdx, [rsp+0x40]
          cb_emit(cb, "\x48\xC7\x44\x24\x40\x00\x00\x00\x00", 9); // init sz=0
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_getfilesize);
          emit_call_reg(cb, 0);
          // malloc(sz+1)
          cb_emit(cb, "\x48\x8B\x4C\x24\x40", 5);        // mov rcx, sz
          cb_emit(cb, "\x48\x83\xC1\x01", 4);            // add rcx, 1
          { int32_t disp = (int32_t)((int64_t)(text_rva + malloc_off) - (int64_t)(text_rva + (uint32_t)(cb->len + 5))); emit_call_rel32(cb, disp); }
          cb_emit(cb, "\x48\x89\x44\x24\x38", 5);        // save buf at [rsp+0x38]
          // ReadFile(handle, buf, sz, &nread, NULL)
          cb_emit(cb, "\x48\x8B\x4C\x24\x48", 5);        // mov rcx, handle
          cb_emit(cb, "\x48\x8B\x54\x24\x38", 5);        // mov rdx, buf
          cb_emit(cb, "\x4C\x8B\x44\x24\x40", 5);        // mov r8, sz
          cb_emit(cb, "\x4C\x8D\x4C\x24\x30", 5);        // lea r9, [rsp+0x30]
          cb_emit(cb, "\x48\xC7\x44\x24\x20\x00\x00\x00\x00", 9);
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_readfile);
          emit_call_reg(cb, 0);
          // null-terminate: buf[nread]=0
          cb_emit(cb, "\x48\x8B\x44\x24\x38", 5);        // mov rax, buf
          cb_emit(cb, "\x8B\x4C\x24\x30", 4);            // mov ecx, dword [rsp+0x30]
          cb_emit(cb, "\xC6\x04\x08\x00", 4);            // mov byte [rax+rcx], 0
          // CloseHandle
          cb_emit(cb, "\x48\x8B\x4C\x24\x48", 5);
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_closehandle);
          emit_call_reg(cb, 0);
          cb_emit(cb, "\x48\x8B\x44\x24\x38", 5);        // rax=buf
          size_t rf_done = cb->len;
          cb_emit(cb, "\xEB\x0B", 2);
          size_t rf_fail = cb->len;
          cb->data[jz_rf_fail + 1] = (unsigned char)(rf_fail - (jz_rf_fail + 2));
          emit_mov_reg_imm64(cb, 0, 0);
          cb->data[rf_done + 1] = (unsigned char)(cb->len - (rf_done + 2));
          cb_emit(cb, "\x48\x83\xC4\x60", 4);            // add rsp, 0x60
        } else if(nlen==10 && strncmp(name, "write_file", 10)==0){
          // write_file(path:rcx, data:rdx, len:r8) -> bytes_written:rax
          // Uses Win32: CreateFileA + WriteFile + CloseHandle (no wide string issues)
          if(!ensure_iat(imp->iat_createfile, "write_file->CreateFileA")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          if(!ensure_iat(imp->iat_write, "write_file->WriteFile")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          if(!ensure_iat(imp->iat_closehandle, "write_file->CloseHandle")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          cb_emit(cb, "\x48\x83\xEC\x50", 4);           // sub rsp, 0x50
          cb_emit(cb, "\x4C\x89\x44\x24\x48", 5);       // mov [rsp+0x48], r8  (len)
          cb_emit(cb, "\x48\x89\x54\x24\x40", 5);       // mov [rsp+0x40], rdx (data)
          cb_emit(cb, "\x48\x89\x4C\x24\x38", 5);       // mov [rsp+0x38], rcx (path)
          // CreateFileA(path, GENERIC_WRITE=0x40000000, 0, NULL, CREATE_ALWAYS=2, FILE_ATTRIBUTE_NORMAL=0x80, NULL)
          // rcx=path, rdx=0x40000000, r8=0, r9=NULL, [rsp+0x20]=2, [rsp+0x28]=0x80, [rsp+0x30]=NULL
          // rcx already = path
          cb_emit(cb, "\x48\xB8\x00\x00\x00\x40\x00\x00\x00\x00", 10); // mov rax, 0x40000000
          cb_emit(cb, "\x48\x89\xC2", 3);               // mov rdx, rax  (GENERIC_WRITE)
          cb_emit(cb, "\x49\xB8\x00\x00\x00\x00\x00\x00\x00\x00", 10); // mov r8, 0
          cb_emit(cb, "\x49\xB9\x00\x00\x00\x00\x00\x00\x00\x00", 10); // mov r9, 0 (NULL security)
          cb_emit(cb, "\x48\xC7\x44\x24\x20\x02\x00\x00\x00", 9);      // mov [rsp+0x20], 2  (CREATE_ALWAYS)
          cb_emit(cb, "\x48\xC7\x44\x24\x28\x80\x00\x00\x00", 9);      // mov [rsp+0x28], 0x80 (FILE_ATTRIBUTE_NORMAL)
          cb_emit(cb, "\x48\xC7\x44\x24\x30\x00\x00\x00\x00", 9);      // mov [rsp+0x30], 0 (NULL template)
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_createfile);
          emit_call_reg(cb, 0);
          // test handle (INVALID_HANDLE_VALUE = -1)
          cb_emit(cb, "\x48\x83\xF8\xFF", 4);           // cmp rax, -1
          size_t jz_create_fail = cb->len;
          cb_emit(cb, "\x74\x00", 2);                   // je fail (patch later)
          // save handle
          cb_emit(cb, "\x48\x89\x44\x24\x30", 5);       // mov [rsp+0x30], rax (handle)
          // WriteFile(handle, data, len, &written, NULL)
          cb_emit(cb, "\x48\x89\xC1", 3);               // mov rcx, rax (handle)
          cb_emit(cb, "\x48\x8B\x54\x24\x40", 5);       // mov rdx, [rsp+0x40] (data)
          cb_emit(cb, "\x4C\x8B\x44\x24\x48", 5);       // mov r8, [rsp+0x48] (len)
          cb_emit(cb, "\x4C\x8D\x4C\x24\x28", 5);       // lea r9, [rsp+0x28] (&written)
          cb_emit(cb, "\x48\xC7\x44\x24\x28\x00\x00\x00\x00", 9); // mov [rsp+0x28], 0 (init written)
          cb_emit(cb, "\x48\xC7\x44\x24\x20\x00\x00\x00\x00", 9); // mov [rsp+0x20], 0 (NULL overlapped)
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_write);
          emit_call_reg(cb, 0);
          // CloseHandle(handle)
          cb_emit(cb, "\x48\x8B\x4C\x24\x30", 5);       // mov rcx, [rsp+0x30] (handle)
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_closehandle);
          emit_call_reg(cb, 0);
          // return written count
          cb_emit(cb, "\x48\x8B\x44\x24\x28", 5);       // mov rax, [rsp+0x28] (written)
          size_t wf_done = cb->len;
          cb_emit(cb, "\xEB\x0B", 2);                   // jmp past fail
          // fail: return 0
          size_t wf_fail = cb->len;
          cb->data[jz_create_fail + 1] = (unsigned char)(wf_fail - (jz_create_fail + 2));
          emit_mov_reg_imm64(cb, 0, 0);
          // patch done jmp
          cb->data[wf_done + 1] = (unsigned char)(cb->len - (wf_done + 2));
          cb_emit(cb, "\x48\x83\xC4\x50", 4);           // add rsp, 0x50
        } else if(nlen==8 && strncmp(name, "date_now", 8)==0){
          emit_lea_reg_rip(cb, 0, text_rva, nl_rva);
        } else if(nlen==12 && strncmp(name, "date_now_utc", 12)==0){
          emit_lea_reg_rip(cb, 0, text_rva, nl_rva);
        } else if(nlen==8 && strncmp(name, "sleep_ms", 8)==0){
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(nlen==7 && strncmp(name, "os_name", 7)==0){
          // buildexe fallback lane: return a valid static string pointer.
          emit_lea_reg_rip(cb, 0, text_rva, nl_rva);
        } else if(nlen==8 && strncmp(name, "proc_run", 8)==0){
          if(imp->iat_system){
            cb_emit(cb, "\x48\x83\xEC\x20", 4); // sub rsp, 32
            emit_mov_reg_rip(cb, 0, text_rva, imp->iat_system);
            emit_call_reg(cb, 0);
            cb_emit(cb, "\x48\x83\xC4\x20", 4); // add rsp, 32
          } else if(imp->iat_proc_run){
            emit_mov_reg_rip(cb, 0, text_rva, imp->iat_proc_run);
            emit_call_reg(cb, 0);
          } else {
            // buildexe fallback lane: pretend success when no OS runner exists.
            emit_mov_reg_imm64(cb, 0, 0);
          }
        } else if(nlen==8 && strncmp(name, "proc_out", 8)==0){
          if(imp->iat_popen && imp->iat_pclose && imp->iat_fread){
            if(emit_win_proc_out_inline(cb, text_rva, imp, malloc_off) != 0){
              free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1;
            }
          } else if(imp->iat_proc_out){
            emit_mov_reg_rip(cb, 0, text_rva, imp->iat_proc_out);
            emit_call_reg(cb, 0);
          } else {
            // Direct-native process capture remains fail-closed on targets without pipes.
            emit_mov_reg_imm64(cb, 0, 0);
          }
        } else if(nlen==14 && strncmp(name, "tls_connect_ex", 14)==0){
          emit_mov_reg_imm64(cb, 0, UINT64_MAX);
        } else if(nlen==19 && strncmp(name, "tezz_ai_vec_add_f32", 19)==0){
          emit_mov_reg_imm64(cb, 0, UINT64_MAX);
        } else if(nlen==19 && strncmp(name, "tezz_ai_vec_mul_f32", 19)==0){
          emit_mov_reg_imm64(cb, 0, UINT64_MAX);
        } else if(nlen==20 && strncmp(name, "tezz_ai_vec_axpy_f32", 20)==0){
          emit_mov_reg_imm64(cb, 0, UINT64_MAX);
        } else if(nlen==16 && strncmp(name, "tezz_ai_dot_f32", 16)==0){
          emit_mov_reg_imm64(cb, 0, UINT64_MAX);
        } else if(nlen==18 && strncmp(name, "tezz_ai_matmul_f32", 18)==0){
          emit_mov_reg_imm64(cb, 0, UINT64_MAX);
        } else if(nlen==24 && strncmp(name, "tezz_ai_rmsnorm_rows_f32", 24)==0){
          emit_mov_reg_imm64(cb, 0, UINT64_MAX);
        } else if(nlen==18 && strncmp(name, "tezz_ai_matmul_f64", 18)==0){
          emit_mov_reg_imm64(cb, 0, UINT64_MAX);
        } else if(nlen==21 && strncmp(name, "tezz_ai_matmul_i8_f64", 21)==0){
          emit_mov_reg_imm64(cb, 0, UINT64_MAX);
        } else if(nlen==24 && strncmp(name, "tezz_ai_rmsnorm_rows_f64", 24)==0){
          emit_mov_reg_imm64(cb, 0, UINT64_MAX);
        } else if(nlen==24 && strncmp(name, "tezz_ai_softmax_rows_f64", 24)==0){
          emit_mov_reg_imm64(cb, 0, UINT64_MAX);
        } else if(nlen==16 && strncmp(name, "tezz_ai_rope_f64", 16)==0){
          emit_mov_reg_imm64(cb, 0, UINT64_MAX);
        } else if(nlen==22 && strncmp(name, "tn_tzimage_decode_file", 22)==0){
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(nlen==15 && strncmp(name, "tn_tzimage_free", 15)==0){
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(nlen==16 && strncmp(name, "tls_policy_reset", 16)==0){
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(nlen==18 && strncmp(name, "tls_policy_set_min", 18)==0){
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(nlen==25 && strncmp(name, "tls_policy_set_pin_sha256", 25)==0){
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(nlen==32 && strncmp(name, "tls_policy_set_handshake_timeout", 32)==0){
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(nlen==18 && strncmp(name, "tls_policy_get_min", 18)==0){
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(nlen==32 && strncmp(name, "tls_policy_get_handshake_timeout", 32)==0){
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(nlen==22 && strncmp(name, "tls_policy_pin_enabled", 22)==0){
          emit_mov_reg_imm64(cb, 0, 0);
        } else if(is_net_runtime_name(imp, name, nlen)){
          if(emit_net_runtime_call(cb, text_rva, imp, name, nlen) != 1){
            free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1;
          }
        } else if(nlen==6 && strncmp(name, "malloc", 6)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)malloc_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
        } else if(nlen==4 && strncmp(name, "free", 4)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)free_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
        } else if(nlen==6 && strncmp(name, "system", 6)==0){
          if(!ensure_iat(imp->iat_system, "system")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_system);
          emit_call_reg(cb, 0);
        } else if(nlen==7 && strncmp(name, "putchar", 7)==0){
          if(!ensure_iat(imp->iat_putchar, "putchar")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_putchar);
          emit_call_reg(cb, 0);
        } else if(nlen==7 && strncmp(name, "getchar", 7)==0){
          if(!ensure_iat(imp->iat_getchar, "getchar")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_getchar);
          emit_call_reg(cb, 0);
        } else if(nlen==7 && strncmp(name, "realloc", 7)==0){
          if(!ensure_iat(imp->iat_realloc, "realloc")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_realloc);
          emit_call_reg(cb, 0);
        } else if(nlen==5 && strncmp(name, "fopen", 5)==0){
          if(!ensure_iat(imp->iat_fopen, "fopen")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fopen);
          emit_call_reg(cb, 0);
        } else if(nlen==6 && strncmp(name, "fclose", 6)==0){
          if(!ensure_iat(imp->iat_fclose, "fclose")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fclose);
          emit_call_reg(cb, 0);
        } else if(nlen==5 && strncmp(name, "fread", 5)==0){
          if(!ensure_iat(imp->iat_fread, "fread")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fread);
          emit_call_reg(cb, 0);
        } else if(nlen==6 && strncmp(name, "fwrite", 6)==0){
          if(!ensure_iat(imp->iat_fwrite, "fwrite")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fwrite);
          emit_call_reg(cb, 0);
        } else if(nlen==5 && strncmp(name, "fseek", 5)==0){
          if(!ensure_iat(imp->iat_fseek, "fseek")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fseek);
          emit_call_reg(cb, 0);
        } else if(nlen==5 && strncmp(name, "ftell", 5)==0){
          if(!ensure_iat(imp->iat_ftell, "ftell")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_ftell);
          emit_call_reg(cb, 0);
        } else if(nlen==6 && strncmp(name, "fflush", 6)==0){
          if(!ensure_iat(imp->iat_fflush, "fflush")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fflush);
          emit_call_reg(cb, 0);
        } else if(nlen==6 && strncmp(name, "io_err", 6)==0){
          if(!ensure_iat(imp->iat_io_err, "io_err")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_io_err);
          emit_call_reg(cb, 0);
        } else if(nlen==18 && strncmp(name, "CreateFileMappingA", 18)==0){
          if(!ensure_iat(imp->iat_createfilemap, "CreateFileMappingA")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_createfilemap);
          emit_call_reg(cb, 0);
        } else if(nlen==13 && strncmp(name, "MapViewOfFile", 13)==0){
          if(!ensure_iat(imp->iat_mapview, "MapViewOfFile")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_mapview);
          emit_call_reg(cb, 0);
        } else if(nlen==15 && strncmp(name, "UnmapViewOfFile", 15)==0){
          if(!ensure_iat(imp->iat_unmapview, "UnmapViewOfFile")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_unmapview);
          emit_call_reg(cb, 0);
        } else if(nlen==12 && strncmp(name, "CreateThread", 12)==0){
          if(!ensure_iat(imp->iat_createthread, "CreateThread")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_createthread);
          emit_call_reg(cb, 0);
        } else if(nlen==19 && strncmp(name, "WaitForSingleObject", 19)==0){
          if(!ensure_iat(imp->iat_waitforsingleobject, "WaitForSingleObject")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_waitforsingleobject);
          emit_call_reg(cb, 0);
        } else if(nlen==5 && strncmp(name, "Sleep", 5)==0){
          if(!ensure_iat(imp->iat_sleep, "Sleep")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_sleep);
          emit_call_reg(cb, 0);
        } else if(nlen==11 && strncmp(name, "CreateFileA", 11)==0){
          if(!ensure_iat(imp->iat_createfile, "CreateFileA")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_createfile);
          emit_call_reg(cb, 0);
        } else if(nlen==11 && strncmp(name, "CloseHandle", 11)==0){
          if(!ensure_iat(imp->iat_closehandle, "CloseHandle")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_closehandle);
          emit_call_reg(cb, 0);
        } else if(nlen==6 && strncmp(name, "io_eof", 6)==0){
          if(!ensure_iat(imp->iat_io_eof, "io_eof")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_io_eof);
          emit_call_reg(cb, 0);
        } else if(nlen==9 && strncmp(name, "read_line", 9)==0){
          if(!ensure_iat(imp->iat_read_line, "read_line")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_read_line);
          emit_call_reg(cb, 0);
        } else if(nlen==10 && strncmp(name, "read_bytes", 10)==0){
          if(!ensure_iat(imp->iat_read_bytes, "read_bytes")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_read_bytes);
          emit_call_reg(cb, 0);
        } else if(nlen==10 && strncmp(name, "write_line", 10)==0){
          if(!ensure_iat(imp->iat_write_line, "write_line")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_write_line);
          emit_call_reg(cb, 0);
        } else if(nlen==5 && strncmp(name, "stdin", 5)==0){
          if(!ensure_iat(imp->iat_stdin, "stdin")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_stdin);
          emit_call_reg(cb, 0);
        } else if(nlen==6 && strncmp(name, "stdout", 6)==0){
          if(!ensure_iat(imp->iat_stdout, "stdout")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_stdout);
          emit_call_reg(cb, 0);
        } else if(nlen==6 && strncmp(name, "stderr", 6)==0){
          if(!ensure_iat(imp->iat_stderr, "stderr")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_stderr);
          emit_call_reg(cb, 0);
        } else if(nlen==3 && strncmp(name, "say", 3)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)say_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
        } else if(nlen==3 && strncmp(name, "len", 3)==0){
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrlen);
          emit_call_reg(cb, 0);
        } else if(nlen==6 && strncmp(name, "strcmp", 6)==0){
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrcmp);
          emit_call_reg(cb, 0);
        } else if(nlen==8 && strncmp(name, "tn_strcmp", 8)==0){
          emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrcmp);
          emit_call_reg(cb, 0);
        } else if(nlen==7 && strncmp(name, "say_str", 7)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)saystr_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
        } else if(nlen==10 && strncmp(name, "bit_popcnt", 10)==0){
          cb_emit(cb, "\xF3\x48\x0F\xB8\xC1", 5);
        } else if(nlen==7 && strncmp(name, "bit_clz", 7)==0){
          cb_emit(cb, "\xF3\x48\x0F\xBD\xC1", 5);
        } else if(nlen==7 && strncmp(name, "bit_ctz", 7)==0){
          cb_emit(cb, "\xF3\x48\x0F\xBC\xC1", 5);
        } else if(nlen==9 && strncmp(name, "bit_bswap", 9)==0){
          cb_emit(cb, "\x48\x89\xC8", 3);
          cb_emit(cb, "\x48\x0F\xC8", 3);
        } else if(nlen==8 && strncmp(name, "bit_rotl", 8)==0){
          cb_emit(cb, "\x48\x89\xC8", 3);
          cb_emit(cb, "\x48\x89\xD1", 3);
          cb_emit(cb, "\x48\xD3\xC0", 3);
        } else if(nlen==8 && strncmp(name, "bit_rotr", 8)==0){
          cb_emit(cb, "\x48\x89\xC8", 3);
          cb_emit(cb, "\x48\x89\xD1", 3);
          cb_emit(cb, "\x48\xD3\xC8", 3);
        } else {
          int tidx = call_target_index(finfo, fcount, name, nlen);
          if(tidx < 0){
            fprintf(stderr, "buildexe: unknown call target '%.*s'\n", nlen, name);
            free(amap.dst_reg);
            free(amap.off_bytes);
            free(label_off);
            return -1;
          }
          if(finfo[tidx].is_extern){
            // extern call mapping (limited)
            if(nlen==9 && strncmp(name, "say_multi", 9)==0){
              int32_t disp = (int32_t)((text_rva + (uint32_t)saymulti_off) - (text_rva + (uint32_t)cb->len + 5));
              emit_call_rel32(cb, disp);
            } else if(nlen==5 && strncmp(name, "say_f", 5)==0){
              int32_t disp = (int32_t)((text_rva + (uint32_t)sayf_off) - (text_rva + (uint32_t)cb->len + 5));
              emit_call_rel32(cb, disp);
              emit_movq_reg_xmm(cb, 0, 0); // Mirror result XMM0 -> RAX (though say_f is void)
            } else if(nlen==6 && strncmp(name, "memcpy", 6)==0){
              int32_t disp = (int32_t)((text_rva + (uint32_t)memcpy_off) - (text_rva + (uint32_t)cb->len + 5));
              emit_call_rel32(cb, disp);
            } else if(nlen==7 && strncmp(name, "memmove", 7)==0){
              int32_t disp = (int32_t)((text_rva + (uint32_t)memmove_off) - (text_rva + (uint32_t)cb->len + 5));
              emit_call_rel32(cb, disp);
            } else if(nlen==6 && strncmp(name, "memcmp", 6)==0){
              int32_t disp = (int32_t)((text_rva + (uint32_t)memcmp_off) - (text_rva + (uint32_t)cb->len + 5));
              emit_call_rel32(cb, disp);
            } else if(nlen==5 && strncmp(name, "fopen", 5)==0){
              if(!ensure_iat(imp->iat_fopen, "fopen")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fopen);
              emit_call_reg(cb, 0);
            } else if(nlen==6 && strncmp(name, "fclose", 6)==0){
              if(!ensure_iat(imp->iat_fclose, "fclose")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fclose);
              emit_call_reg(cb, 0);
            } else if(nlen==7 && strncmp(name, "get_env", 7)==0){
              if(!ensure_iat(imp->iat_getenv, "getenv")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_getenv);
              emit_call_reg(cb, 0);
            } else if(nlen==19 && strncmp(name, "color_is_tty_stdout", 19)==0){
              emit_mov_reg_imm64(cb, 0, 1);
            } else if(nlen==12 && strncmp(name, "tts_play_pcm", 12)==0){
              emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_tts_play_pcm);
              emit_call_reg(cb, 0);
            } else if(nlen==15 && strncmp(name, "stt_capture_mic", 15)==0){
              emit_mov_reg_imm64(cb, 0, (uint64_t)&tn_stt_capture_mic);
              emit_call_reg(cb, 0);
            } else if(is_net_runtime_name(imp, name, nlen)){
              if(emit_net_runtime_call(cb, text_rva, imp, name, nlen) != 1){
                free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1;
              }
            } else if(nlen==5 && strncmp(name, "fread", 5)==0){
              if(!ensure_iat(imp->iat_fread, "fread")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fread);
              emit_call_reg(cb, 0);
            } else if(nlen==6 && strncmp(name, "fwrite", 6)==0){
              if(!ensure_iat(imp->iat_fwrite, "fwrite")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fwrite);
              emit_call_reg(cb, 0);
            } else if(nlen==5 && strncmp(name, "fseek", 5)==0){
              if(!ensure_iat(imp->iat_fseek, "fseek")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fseek);
              emit_call_reg(cb, 0);
            } else if(nlen==5 && strncmp(name, "ftell", 5)==0){
              if(!ensure_iat(imp->iat_ftell, "ftell")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_ftell);
              emit_call_reg(cb, 0);
            } else if(nlen==6 && strncmp(name, "fflush", 6)==0){
              if(!ensure_iat(imp->iat_fflush, "fflush")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_fflush);
              emit_call_reg(cb, 0);
            } else if(nlen==6 && strncmp(name, "io_err", 6)==0){
              if(!ensure_iat(imp->iat_io_err, "io_err")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_io_err);
              emit_call_reg(cb, 0);
            } else if(nlen==18 && strncmp(name, "CreateFileMappingA", 18)==0){
              if(!ensure_iat(imp->iat_createfilemap, "CreateFileMappingA")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_createfilemap);
              emit_call_reg(cb, 0);
            } else if(nlen==13 && strncmp(name, "MapViewOfFile", 13)==0){
              if(!ensure_iat(imp->iat_mapview, "MapViewOfFile")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_mapview);
              emit_call_reg(cb, 0);
            } else if(nlen==15 && strncmp(name, "UnmapViewOfFile", 15)==0){
              if(!ensure_iat(imp->iat_unmapview, "UnmapViewOfFile")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_unmapview);
              emit_call_reg(cb, 0);
            } else if(nlen==11 && strncmp(name, "CreateFileA", 11)==0){
              if(!ensure_iat(imp->iat_createfile, "CreateFileA")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_createfile);
              emit_call_reg(cb, 0);
            } else if(nlen==11 && strncmp(name, "CloseHandle", 11)==0){
              if(!ensure_iat(imp->iat_closehandle, "CloseHandle")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_closehandle);
              emit_call_reg(cb, 0);
            } else if(nlen==12 && strncmp(name, "CreateThread", 12)==0){
              if(!ensure_iat(imp->iat_createthread, "CreateThread")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_createthread);
              emit_call_reg(cb, 0);
            } else if(nlen==19 && strncmp(name, "WaitForSingleObject", 19)==0){
              if(!ensure_iat(imp->iat_waitforsingleobject, "WaitForSingleObject")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_waitforsingleobject);
              emit_call_reg(cb, 0);
            } else if(nlen==5 && strncmp(name, "Sleep", 5)==0){
              if(!ensure_iat(imp->iat_sleep, "Sleep")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_sleep);
              emit_call_reg(cb, 0);
            } else if(nlen==6 && strncmp(name, "io_eof", 6)==0){
              if(!ensure_iat(imp->iat_io_eof, "io_eof")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_io_eof);
              emit_call_reg(cb, 0);
            } else if(nlen==9 && strncmp(name, "read_line", 9)==0){
              if(!ensure_iat(imp->iat_read_line, "read_line")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_read_line);
              emit_call_reg(cb, 0);
            } else if(nlen==10 && strncmp(name, "read_bytes", 10)==0){
              if(!ensure_iat(imp->iat_read_bytes, "read_bytes")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_read_bytes);
              emit_call_reg(cb, 0);
            } else if(nlen==10 && strncmp(name, "write_line", 10)==0){
              if(!ensure_iat(imp->iat_write_line, "write_line")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_write_line);
              emit_call_reg(cb, 0);
            } else if(nlen==5 && strncmp(name, "stdin", 5)==0){
              if(!ensure_iat(imp->iat_stdin, "stdin")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_stdin);
              emit_call_reg(cb, 0);
            } else if(nlen==6 && strncmp(name, "stdout", 6)==0){
              if(!ensure_iat(imp->iat_stdout, "stdout")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_stdout);
              emit_call_reg(cb, 0);
            } else if(nlen==6 && strncmp(name, "stderr", 6)==0){
              if(!ensure_iat(imp->iat_stderr, "stderr")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_stderr);
              emit_call_reg(cb, 0);
            } else if(nlen==3 && strncmp(name, "len", 3)==0){
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrlen);
              emit_call_reg(cb, 0);
            } else if(nlen==6 && strncmp(name, "strcmp", 6)==0){
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrcmp);
              emit_call_reg(cb, 0);
            } else if(nlen==8 && strncmp(name, "tn_strcmp", 8)==0){
              emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrcmp);
              emit_call_reg(cb, 0);
            } else {
              free(amap.dst_reg);
              free(amap.off_bytes);
              free(label_off);
              return -1;
            }
          } else {
            if(emit_func_patch_call(cb, fpatches, fpatch_n, fpatch_cap, tidx) != 0){
              free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1;
            }
          }
        }
        emit_call_finish(cb, total, pushed_mregs);
        if(in->a >= 0){
          ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
        }
        break;
      }
      case I_CALLPTR: {
        if(!in->args){
          fprintf(stderr, "buildexe: callptr missing args\n");
          free(amap.dst_reg);
          free(amap.off_bytes);
          free(label_off);
          return -1;
        }
        int pushed_mregs = 0;
        int total = emit_call_prepare(cb, in->argc, in->args, vreg_to_mreg, nreg, iv, k, &pushed_mregs);
        ra_load_rax(cb, in->b, vreg_to_mreg, nreg); // fn ptr
        emit_call_reg(cb, 0);
        emit_call_finish(cb, total, pushed_mregs);
        if(in->a >= 0) ra_store_rax(cb, in->a, vreg_to_mreg, nreg);
        break;
      }
      case I_RET:
        if(in->a >= 0) ra_load_rax(cb, in->a, vreg_to_mreg, nreg);
        else emit_mov_reg_imm64(cb, 0, 0);
        cb_emit1(cb, 0xE9); // jmp rel32 to epilogue
        if(ret_n < (int)(sizeof(ret_patches)/sizeof(ret_patches[0]))){
          ret_patches[ret_n++] = cb->len;
          cb_emit4(cb, 0);
        } else {
          fprintf(stderr, "buildexe: too many returns in %.*s\n", fn->len, fn->name);
          free(amap.dst_reg);
          free(amap.off_bytes);
          free(label_off);
          return -1;
        }
        break;
      default:
        fprintf(stderr, "buildexe: unsupported IR op %d\n", in->op);
        free(amap.dst_reg);
        free(amap.off_bytes);
        free(label_off);
        return -1;
    }
  }
  // fallthrough default return
  emit_mov_reg_imm64(cb, 0, 0);
  size_t epilogue_pos = cb->len;
  emit_lsra_restores(cb, lsra_phy_mask);  // restore callee-saved regs used by LSRA
  emit_epilogue(cb, frame);

  for(int i=0;i<ret_n;i++){
    uint32_t rip = text_rva + (uint32_t)ret_patches[i] + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)epilogue_pos) - rip);
    cb_patch4(cb, ret_patches[i], (uint32_t)disp);
  }

  for(int i=0;i<*lpatch_n;i++){
    int lid = lpatches[i].label;
    if(lid >=0 && lid < lbl_n && label_off && label_off[lid] != (size_t)-1){
      uint32_t rip = text_rva + (uint32_t)lpatches[i].pos + 4;
      int32_t disp = (int32_t)((text_rva + (uint32_t)label_off[lid]) - rip);
      cb_patch4(cb, lpatches[i].pos, (uint32_t)disp);
    } else {
      int fallback = -1;
      if(label_off){
        for(int j=lid+1; j<lbl_n; j++){
          if(label_off[j] != (size_t)-1){ fallback = j; break; }
        }
      }
      if(fallback >= 0){
        fprintf(stderr, "buildexe: missing label %d in %.*s; using L%d\n", lid, fn->len, fn->name, fallback);
        uint32_t rip = text_rva + (uint32_t)lpatches[i].pos + 4;
        int32_t disp = (int32_t)((text_rva + (uint32_t)label_off[fallback]) - rip);
        cb_patch4(cb, lpatches[i].pos, (uint32_t)disp);
      } else {
        fprintf(stderr, "buildexe: missing label %d (lbl_n=%d) in %.*s\n", lid, lbl_n, fn->len, fn->name);
        free(amap.dst_reg);
        free(amap.off_bytes);
        free(label_off);
        return -1;
      }
    }
  }

  free(amap.dst_reg);
  free(amap.off_bytes);
  free(label_off);
  free(vreg_to_mreg);
  (void)func_start;
  return 0;
}

// ----------------- x86 lowering (32-bit) -----------------

static void emit_x86_load64(CodeBuf* cb, int disp){
  emit_x86_mov_eax_membp(cb, disp);
  emit_x86_mov_edx_membp(cb, disp - 4);
}
static void emit_x86_store64(CodeBuf* cb, int disp){
  emit_x86_mov_membp_eax(cb, disp);
  emit_x86_mov_membp_edx(cb, disp - 4);
}
static void emit_x86_zero_edx(CodeBuf* cb){
  cb_emit(cb, "\x31\xD2", 2); // xor edx, edx
}

static void emit_x86_push_arg64(CodeBuf* cb, int disp){
  emit_x86_mov_edx_membp(cb, disp - 4);
  emit_x86_mov_eax_membp(cb, disp);
  emit_x86_push_reg(cb, 2); // edx
  emit_x86_push_reg(cb, 0); // eax
}

static void emit_x86_set_result(CodeBuf* cb, int v){
  emit_x86_mov_eax_imm32(cb, (uint32_t)v);
  emit_x86_zero_edx(cb);
}

static int lower_func_x86(IRFunc* fn, CodeBuf* cb, uint32_t text_rva,
                          const uint32_t* str_rva, int str_n,
                          size_t say_off, size_t sayf_off, size_t saystr_off, size_t saymulti_off,
                          size_t memcpy_off, size_t memmove_off, size_t memcmp_off,
                          size_t len_off, size_t strcmp_off, size_t tnstrcmp_off,
                          size_t mul_off, size_t div_off, size_t mod_off, size_t shl_off, size_t shr_off,
                          size_t time_off, size_t time_ms_off, size_t time_ns_off,
                          size_t malloc_off, size_t free_off,
                          FuncInfo* finfo, int fcount,
                          GlobalInfo* ginfo, int gcount,
                          const PeImportInfo* imp,
                          RelocList* rel,
                          FuncPatch* fpatches, int* fpatch_n, int fpatch_cap,
                          LabelPatch* lpatches, int* lpatch_n, int lpatch_cap){
  (void)imp;
  int nreg = (fn->max_reg >= 0) ? (fn->max_reg + 1) : fn->next_reg;
  if(nreg <= 0) nreg = 1;
  int reg_bytes = nreg * 8;
  int off = reg_bytes + 8;
  AllocaMap amap = {0};

  for(int k=0;k<fn->n;k++){
    IRIns* in = &fn->ins[k];
    if(in->op == I_ALLOCA){
      int align = in->align ? in->align : 8;
      int base = (int)align_up_u32((uint32_t)off, (uint32_t)align);
      amap_add(&amap, in->a, base + in->size);
      off = base + in->size;
    }
  }
  int frame = (int)align_up_u32((uint32_t)off, 16);

  emit_x86_prologue(cb, frame);

  int preg[32];
  int pn = 0;
  for(int i=0;i<fn->n && pn<fn->param_count;i++){
    IRIns* pi = &fn->ins[i];
    if(pi->op == I_LABEL) break;
    if(pi->op == I_STORE){
      preg[pn++] = pi->b;
    }
  }
  if(pn == 0){
    for(int i=0;i<fn->param_count && i<32;i++){
      preg[pn++] = i;
    }
  }
  for(int i=0;i<pn && i<fn->param_count;i++){
    int arg_disp = -(8 + i*8);
    emit_x86_mov_eax_membp(cb, arg_disp);
    emit_x86_mov_edx_membp(cb, arg_disp - 4);
    emit_x86_store64(cb, reg_slot_off(preg[i]));
  }

  int lbl_n = fn->next_label > 0 ? fn->next_label : 0;
  size_t* label_off = NULL;
  if(lbl_n > 0){
    label_off = (size_t*)malloc(sizeof(size_t) * (size_t)lbl_n);
    for(int i=0;i<lbl_n;i++) label_off[i] = (size_t)-1;
  }

  size_t ret_patches[512];
  int ret_n = 0;

  for(int k=0;k<fn->n;k++){
    IRIns* in = &fn->ins[k];
    switch(in->op){
      case I_LABEL:
        if(label_off && in->a >=0 && in->a < lbl_n){
          label_off[in->a] = cb->len;
        }
        break;
      case I_JMP: {
        cb_emit1(cb, 0xE9);
        size_t pos = cb->len;
        cb_emit4(cb, 0);
        if(*lpatch_n < lpatch_cap){
          lpatches[*lpatch_n].pos = pos;
          lpatches[*lpatch_n].label = in->a;
          (*lpatch_n)++;
        }
        break;
      }
      case I_JZ: {
        emit_x86_load64(cb, reg_slot_off(in->a));
        cb_emit(cb, "\x09\xD0", 2); // or eax, edx
        cb_emit(cb, "\x0F\x84", 2);
        size_t pos = cb->len;
        cb_emit4(cb, 0);
        if(*lpatch_n < lpatch_cap){
          lpatches[*lpatch_n].pos = pos;
          lpatches[*lpatch_n].label = in->b;
          (*lpatch_n)++;
        }
        break;
      }
      case I_ICONST: {
        uint64_t v = (uint64_t)in->imm;
        emit_x86_mov_eax_imm32(cb, (uint32_t)(v & 0xFFFFFFFFu));
        emit_x86_mov_edx_imm32(cb, (uint32_t)(v >> 32));
        emit_x86_store64(cb, reg_slot_off(in->a));
        break;
      }
      case I_FCONST: {
        uint64_t v = (uint64_t)in->immf;
        emit_x86_mov_eax_imm32(cb, (uint32_t)(v & 0xFFFFFFFFu));
        emit_x86_mov_edx_imm32(cb, (uint32_t)(v >> 32));
        emit_x86_store64(cb, reg_slot_off(in->a));
        break;
      }
      case I_ADDRSYM: {
        const char* nm = in->name;
        int nlen = in->nlen;
        int done = 0;
        if(nlen==3 && strncmp(nm, "say", 3)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)say_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        } else if(nlen==5 && strncmp(nm, "say_f", 5)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)sayf_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        } else if(nlen==7 && strncmp(nm, "say_str", 7)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)saystr_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        } else if(nlen==9 && strncmp(nm, "say_multi", 9)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)saymulti_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        } else if(nlen==6 && strncmp(nm, "memcpy", 6)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)memcpy_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        } else if(nlen==7 && strncmp(nm, "memmove", 7)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)memmove_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        } else if(nlen==6 && strncmp(nm, "memcmp", 6)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)memcmp_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        } else if(nlen==3 && strncmp(nm, "len", 3)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)len_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        } else if(nlen==6 && strncmp(nm, "strcmp", 6)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)strcmp_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        } else if(nlen==8 && strncmp(nm, "tn_strcmp", 8)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)tnstrcmp_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        } else if(nlen==8 && strncmp(nm, "time_now", 8)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)time_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        } else if(nlen==11 && strncmp(nm, "time_now_ms", 11)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)time_ms_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        } else if(nlen==11 && strncmp(nm, "time_now_ns", 11)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)time_ns_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        } else if(nlen==6 && strncmp(nm, "malloc", 6)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)malloc_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        } else if(nlen==4 && strncmp(nm, "free", 4)==0){
          emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)free_off, rel);
          emit_x86_zero_edx(cb);
          emit_x86_store64(cb, reg_slot_off(in->a));
          done = 1;
        }
        if(!done){
          for(int gi=0; gi<gcount && !done; gi++){
            if(!ginfo[gi].is_extern && ginfo[gi].len==nlen &&
               strncmp(ginfo[gi].name, nm, (size_t)nlen)==0){
              emit_x86_mov_eax_abs(cb, text_rva, ginfo[gi].rva, rel);
              emit_x86_zero_edx(cb);
              emit_x86_store64(cb, reg_slot_off(in->a));
              done = 1;
              break;
            }
          }
        }
        if(!done){
          for(int fi=0; fi<fcount; fi++){
            if(finfo[fi].len==nlen && strncmp(finfo[fi].name, nm, (size_t)nlen)==0){
              if(!finfo[fi].is_extern){
                emit_x86_mov_eax_abs(cb, text_rva, text_rva + (uint32_t)finfo[fi].off, rel);
                emit_x86_zero_edx(cb);
                emit_x86_store64(cb, reg_slot_off(in->a));
                done = 1;
                break;
              }
            }
          }
        }
        if(!done){
          fprintf(stderr, "buildexe: addrsym unresolved '%.*s'\n", nlen, nm);
          free(amap.dst_reg);
          free(amap.off_bytes);
          free(label_off);
          return -1;
        }
        break;
      }
      case I_SCONST:
        if(in->sid < 0 || in->sid >= str_n){
          fprintf(stderr, "buildexe: sconst out of range (%d)\n", in->sid);
          free(amap.dst_reg);
          free(amap.off_bytes);
          free(label_off);
          return -1;
        }
        emit_x86_mov_eax_abs(cb, text_rva, str_rva[in->sid], rel);
        emit_x86_zero_edx(cb);
        emit_x86_store64(cb, reg_slot_off(in->a));
        break;
      case I_MOV:
        emit_x86_load64(cb, reg_slot_off(in->b));
        emit_x86_store64(cb, reg_slot_off(in->a));
        break;
      case I_BIN: {
        // move rhs into ecx:ebx
        emit_x86_mov_eax_membp(cb, reg_slot_off(in->c));
        cb_emit(cb, "\x89\xC1", 2); // mov ecx, eax
        emit_x86_mov_edx_membp(cb, reg_slot_off(in->c) - 4);
        cb_emit(cb, "\x89\xD3", 2); // mov ebx, edx
        emit_x86_load64(cb, reg_slot_off(in->b));
        switch(in->binop){
          case B_ADD:
            cb_emit(cb, "\x01\xC8", 2); // add eax, ecx
            cb_emit(cb, "\x11\xDA", 2); // adc edx, ebx
            break;
          case B_SUB:
            cb_emit(cb, "\x29\xC8", 2); // sub eax, ecx
            cb_emit(cb, "\x19\xDA", 2); // sbb edx, ebx
            break;
          case B_AND:
            cb_emit(cb, "\x21\xC8", 2);
            cb_emit(cb, "\x21\xDA", 2);
            break;
          case B_OR:
            cb_emit(cb, "\x09\xC8", 2);
            cb_emit(cb, "\x09\xDA", 2);
            break;
          case B_XOR:
            cb_emit(cb, "\x31\xC8", 2);
            cb_emit(cb, "\x31\xDA", 2);
            break;
          case B_MUL: {
            emit_x86_push_arg64(cb, reg_slot_off(in->c));
            emit_x86_push_arg64(cb, reg_slot_off(in->b));
            int32_t disp = (int32_t)((text_rva + (uint32_t)mul_off) - (text_rva + (uint32_t)cb->len + 5));
            emit_call_rel32(cb, disp);
            emit_x86_add_esp(cb, 16);
            break;
          }
          case B_DIV: {
            emit_x86_push_arg64(cb, reg_slot_off(in->c));
            emit_x86_push_arg64(cb, reg_slot_off(in->b));
            int32_t disp = (int32_t)((text_rva + (uint32_t)div_off) - (text_rva + (uint32_t)cb->len + 5));
            emit_call_rel32(cb, disp);
            emit_x86_add_esp(cb, 16);
            break;
          }
          case B_MOD: {
            emit_x86_push_arg64(cb, reg_slot_off(in->c));
            emit_x86_push_arg64(cb, reg_slot_off(in->b));
            int32_t disp = (int32_t)((text_rva + (uint32_t)mod_off) - (text_rva + (uint32_t)cb->len + 5));
            emit_call_rel32(cb, disp);
            emit_x86_add_esp(cb, 16);
            break;
          }
          case B_SHL: {
            emit_x86_mov_eax_membp(cb, reg_slot_off(in->c));
            emit_x86_push_reg(cb, 0);
            emit_x86_push_arg64(cb, reg_slot_off(in->b));
            int32_t disp = (int32_t)((text_rva + (uint32_t)shl_off) - (text_rva + (uint32_t)cb->len + 5));
            emit_call_rel32(cb, disp);
            emit_x86_add_esp(cb, 12);
            break;
          }
          case B_SHR: {
            emit_x86_mov_eax_membp(cb, reg_slot_off(in->c));
            emit_x86_push_reg(cb, 0);
            emit_x86_push_arg64(cb, reg_slot_off(in->b));
            int32_t disp = (int32_t)((text_rva + (uint32_t)shr_off) - (text_rva + (uint32_t)cb->len + 5));
            emit_call_rel32(cb, disp);
            emit_x86_add_esp(cb, 12);
            break;
          }
          default:
            fprintf(stderr, "buildexe: unsupported binop %d\n", in->binop);
            free(amap.dst_reg);
            free(amap.off_bytes);
            free(label_off);
            return -1;
        }
        emit_x86_store64(cb, reg_slot_off(in->a));
        break;
      }
      case I_CMP: {
        emit_x86_mov_eax_membp(cb, reg_slot_off(in->c));
        cb_emit(cb, "\x89\xC1", 2); // mov ecx, eax
        emit_x86_mov_edx_membp(cb, reg_slot_off(in->c) - 4);
        cb_emit(cb, "\x89\xD3", 2); // mov ebx, edx
        emit_x86_load64(cb, reg_slot_off(in->b));
        cb_emit(cb, "\x39\xDA", 2); // cmp edx, ebx
        cb_emit(cb, "\x0F\x8C", 2); size_t jl1 = cb->len; cb_emit4(cb, 0);
        cb_emit(cb, "\x0F\x8F", 2); size_t jg1 = cb->len; cb_emit4(cb, 0);
        cb_emit(cb, "\x39\xC8", 2); // cmp eax, ecx
        cb_emit(cb, "\x0F\x8C", 2); size_t jl2 = cb->len; cb_emit4(cb, 0);
        cb_emit(cb, "\x0F\x8F", 2); size_t jg2 = cb->len; cb_emit4(cb, 0);
        // equal
        emit_x86_set_result(cb, (in->cmpop==C_EQ || in->cmpop==C_LTE || in->cmpop==C_GTE) ? 1 : 0);
        cb_emit1(cb, 0xE9); size_t jend = cb->len; cb_emit4(cb, 0);
        size_t lless = cb->len;
        emit_x86_set_result(cb, (in->cmpop==C_LT || in->cmpop==C_LTE || in->cmpop==C_NEQ) ? 1 : 0);
        cb_emit1(cb, 0xE9); size_t jend2 = cb->len; cb_emit4(cb, 0);
        size_t lgt = cb->len;
        emit_x86_set_result(cb, (in->cmpop==C_GT || in->cmpop==C_GTE || in->cmpop==C_NEQ) ? 1 : 0);
        size_t lend = cb->len;
        // patch jumps
        uint32_t rip = text_rva + (uint32_t)jl1 + 4;
        cb_patch4(cb, jl1, (uint32_t)((text_rva + (uint32_t)lless) - rip));
        rip = text_rva + (uint32_t)jl2 + 4;
        cb_patch4(cb, jl2, (uint32_t)((text_rva + (uint32_t)lless) - rip));
        rip = text_rva + (uint32_t)jg1 + 4;
        cb_patch4(cb, jg1, (uint32_t)((text_rva + (uint32_t)lgt) - rip));
        rip = text_rva + (uint32_t)jg2 + 4;
        cb_patch4(cb, jg2, (uint32_t)((text_rva + (uint32_t)lgt) - rip));
        rip = text_rva + (uint32_t)jend + 4;
        cb_patch4(cb, jend, (uint32_t)((text_rva + (uint32_t)lend) - rip));
        rip = text_rva + (uint32_t)jend2 + 4;
        cb_patch4(cb, jend2, (uint32_t)((text_rva + (uint32_t)lend) - rip));
        emit_x86_store64(cb, reg_slot_off(in->a));
        break;
      }
      case I_FBIN:
        emit_x86_movsd_xmm_membp(cb, 0, reg_slot_off(in->b));
        emit_x86_movsd_xmm_membp(cb, 1, reg_slot_off(in->c));
        switch(in->fop){
          case F_ADD: emit_fbin(cb, 0); break;
          case F_SUB: emit_fbin(cb, 1); break;
          case F_MUL: emit_fbin(cb, 2); break;
          case F_DIV: emit_fbin(cb, 3); break;
          default: emit_fbin(cb, 0); break;
        }
        emit_x86_movsd_membp_xmm(cb, reg_slot_off(in->a), 0);
        break;
      case I_FCMP:
        emit_x86_movsd_xmm_membp(cb, 0, reg_slot_off(in->b));
        emit_x86_movsd_xmm_membp(cb, 1, reg_slot_off(in->c));
        emit_x86_ucomisd(cb);
        switch(in->cmpop){
          case C_EQ:  emit_setcc(cb, 4); break;
          case C_NEQ: emit_setcc(cb, 5); break;
          case C_LT:  emit_setcc(cb, 2); break;
          case C_LTE: emit_setcc(cb, 6); break;
          case C_GT:  emit_setcc(cb, 7); break;
          case C_GTE: emit_setcc(cb, 3); break;
          default: emit_setcc(cb, 4); break;
        }
        emit_movzx_eax_al(cb);
        emit_x86_zero_edx(cb);
        emit_x86_store64(cb, reg_slot_off(in->a));
        break;
      case I_I2F:
        emit_x86_load64(cb, reg_slot_off(in->b));
        emit_x86_cvtsi2sd(cb);
        emit_x86_movsd_membp_xmm(cb, reg_slot_off(in->a), 0);
        break;
      case I_F2I:
        emit_x86_movsd_xmm_membp(cb, 0, reg_slot_off(in->b));
        emit_x86_cvttsd2si(cb);
        emit_x86_zero_edx(cb);
        emit_x86_store64(cb, reg_slot_off(in->a));
        break;
      case I_ALLOCA: {
        int aoff = amap_find(&amap, in->a);
        if(aoff < 0){
          free(amap.dst_reg);
          free(amap.off_bytes);
          free(label_off);
          return -1;
        }
        emit_x86_lea_eax_membp(cb, aoff);
        emit_x86_zero_edx(cb);
        emit_x86_store64(cb, reg_slot_off(in->a));
        break;
      }
      case I_GEP:
        emit_x86_load64(cb, reg_slot_off(in->b));
        if(in->imm >= -128 && in->imm <= 127){
          cb_emit(cb, "\x83\xC0", 2);
          cb_emit1(cb, (unsigned char)(in->imm & 0xFF));
        } else {
          cb_emit(cb, "\x05", 1);
          cb_emit4(cb, (uint32_t)in->imm);
        }
        emit_x86_store64(cb, reg_slot_off(in->a));
        break;
      case I_LOAD:
        emit_x86_mov_eax_membp(cb, reg_slot_off(in->b)); // addr low
        if(in->size == 1){
          if(in->is_unsigned){
            cb_emit(cb, "\x0F\xB6\x00", 3);
            emit_x86_zero_edx(cb);
          } else {
            cb_emit(cb, "\x0F\xBE\x00", 3);
            cb_emit(cb, "\x99", 1); // cdq
          }
        } else if(in->size == 2){
          if(in->is_unsigned){
            cb_emit(cb, "\x0F\xB7\x00", 3);
            emit_x86_zero_edx(cb);
          } else {
            cb_emit(cb, "\x0F\xBF\x00", 3);
            cb_emit(cb, "\x99", 1); // cdq
          }
        } else if(in->size == 4){
          cb_emit(cb, "\x8B\x00", 2);
          if(in->is_unsigned) emit_x86_zero_edx(cb);
          else cb_emit(cb, "\x99", 1); // cdq
        } else {
          cb_emit(cb, "\x89\xC1", 2); // mov ecx, eax (addr)
          cb_emit(cb, "\x8B\x01", 2); // mov eax, [ecx]
          cb_emit(cb, "\x8B\x51\x04", 3); // mov edx, [ecx+4]
        }
        emit_x86_store64(cb, reg_slot_off(in->a));
        break;
      case I_STORE:
        emit_x86_mov_eax_membp(cb, reg_slot_off(in->a)); // addr low
        cb_emit(cb, "\x89\xC1", 2); // mov ecx, eax (addr)
        emit_x86_load64(cb, reg_slot_off(in->b));
        if(in->size == 1){
          cb_emit(cb, "\x88\x01", 2); // mov [ecx], al
        } else if(in->size == 2){
          cb_emit(cb, "\x66\x89\x01", 3); // mov [ecx], ax
        } else if(in->size == 4){
          cb_emit(cb, "\x89\x01", 2); // mov [ecx], eax
        } else {
          cb_emit(cb, "\x89\x01", 2); // mov [ecx], eax
          cb_emit(cb, "\x89\x51\x04", 3); // mov [ecx+4], edx
        }
        break;
      case I_CALL: {
        const char* name = in->name;
        int nlen = in->nlen;
        if(!name || (in->argc > 0 && !in->args)){
          free(amap.dst_reg);
          free(amap.off_bytes);
          free(label_off);
          return -1;
        }
        // push args (64-bit) right-to-left
        for(int i=in->argc-1;i>=0;i--){
          emit_x86_push_arg64(cb, reg_slot_off(in->args[i]));
        }
        if(nlen==3 && strncmp(name, "say", 3)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)say_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==7 && strncmp(name, "say_str", 7)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)saystr_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==5 && strncmp(name, "say_f", 5)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)sayf_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==9 && strncmp(name, "say_multi", 9)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)saymulti_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==6 && strncmp(name, "memcpy", 6)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)memcpy_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==7 && strncmp(name, "memmove", 7)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)memmove_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==6 && strncmp(name, "memcmp", 6)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)memcmp_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==3 && strncmp(name, "len", 3)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)len_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==6 && strncmp(name, "strcmp", 6)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)strcmp_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==8 && strncmp(name, "tn_strcmp", 8)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)tnstrcmp_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==8 && strncmp(name, "time_now", 8)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)time_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==11 && strncmp(name, "time_now_ms", 11)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)time_ms_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==11 && strncmp(name, "time_now_ns", 11)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)time_ns_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==10 && strncmp(name, "args_count", 10)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==3 && strncmp(name, "arg", 3)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==13 && strncmp(name, "async_workers", 13)==0){
          emit_x86_mov_eax_imm32(cb, 1);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==13 && strncmp(name, "async_pending", 13)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==7 && strncmp(name, "get_cwd", 7)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==8 && strncmp(name, "path_sep", 8)==0){
          emit_x86_mov_eax_imm32(cb, 92);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==9 && strncmp(name, "path_join", 9)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==13 && strncmp(name, "path_basename", 13)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==12 && strncmp(name, "path_dirname", 12)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==8 && strncmp(name, "list_dir", 8)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==18 && strncmp(name, "list_dir_recursive", 18)==0){
          int tidx = raw_io_bridge_target(finfo, fcount, name, nlen);
          if(tidx >= 0){
            if(emit_func_patch_call(cb, fpatches, fpatch_n, fpatch_cap, tidx) != 0){
              free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1;
            }
            emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
          } else {
            emit_x86_mov_eax_imm32(cb, 0);
            emit_x86_zero_edx(cb);
            emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
          }
        } else if(nlen==4 && strncmp(name, "glob", 4)==0){
          int tidx = raw_io_bridge_target(finfo, fcount, name, nlen);
          if(tidx >= 0){
            if(emit_func_patch_call(cb, fpatches, fpatch_n, fpatch_cap, tidx) != 0){
              free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1;
            }
            emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
          } else {
            emit_x86_mov_eax_imm32(cb, 0);
            emit_x86_zero_edx(cb);
            emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
          }
        } else if(nlen==14 && strncmp(name, "path_normalize", 14)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==19 && strncmp(name, "path_normalize_opts", 19)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==11 && strncmp(name, "path_exists", 11)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==11 && strncmp(name, "path_is_dir", 11)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==5 && strncmp(name, "chdir", 5)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==5 && strncmp(name, "mkdir", 5)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==5 && strncmp(name, "rmdir", 5)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==6 && strncmp(name, "remove", 6)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==6 && strncmp(name, "rename", 6)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==9 && strncmp(name, "file_size", 9)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==9 && strncmp(name, "read_file", 9)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==10 && strncmp(name, "write_file", 10)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==8 && strncmp(name, "date_now", 8)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==12 && strncmp(name, "date_now_utc", 12)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==8 && strncmp(name, "sleep_ms", 8)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==7 && strncmp(name, "os_name", 7)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==8 && strncmp(name, "proc_run", 8)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==8 && strncmp(name, "proc_out", 8)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==14 && strncmp(name, "tls_connect_ex", 14)==0){
          emit_x86_mov_eax_imm32(cb, (uint32_t)-1);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==19 && strncmp(name, "tezz_ai_vec_add_f32", 19)==0){
          emit_x86_mov_eax_imm32(cb, (uint32_t)-1);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==19 && strncmp(name, "tezz_ai_vec_mul_f32", 19)==0){
          emit_x86_mov_eax_imm32(cb, (uint32_t)-1);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==20 && strncmp(name, "tezz_ai_vec_axpy_f32", 20)==0){
          emit_x86_mov_eax_imm32(cb, (uint32_t)-1);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==16 && strncmp(name, "tezz_ai_dot_f32", 16)==0){
          emit_x86_mov_eax_imm32(cb, (uint32_t)-1);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==18 && strncmp(name, "tezz_ai_matmul_f32", 18)==0){
          emit_x86_mov_eax_imm32(cb, (uint32_t)-1);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==24 && strncmp(name, "tezz_ai_rmsnorm_rows_f32", 24)==0){
          emit_x86_mov_eax_imm32(cb, (uint32_t)-1);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==18 && strncmp(name, "tezz_ai_matmul_f64", 18)==0){
          emit_x86_mov_eax_imm32(cb, (uint32_t)-1);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==21 && strncmp(name, "tezz_ai_matmul_i8_f64", 21)==0){
          emit_x86_mov_eax_imm32(cb, (uint32_t)-1);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==24 && strncmp(name, "tezz_ai_rmsnorm_rows_f64", 24)==0){
          emit_x86_mov_eax_imm32(cb, (uint32_t)-1);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==24 && strncmp(name, "tezz_ai_softmax_rows_f64", 24)==0){
          emit_x86_mov_eax_imm32(cb, (uint32_t)-1);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==16 && strncmp(name, "tezz_ai_rope_f64", 16)==0){
          emit_x86_mov_eax_imm32(cb, (uint32_t)-1);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==22 && strncmp(name, "tn_tzimage_decode_file", 22)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==15 && strncmp(name, "tn_tzimage_free", 15)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==16 && strncmp(name, "tls_policy_reset", 16)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==18 && strncmp(name, "tls_policy_set_min", 18)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==25 && strncmp(name, "tls_policy_set_pin_sha256", 25)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==32 && strncmp(name, "tls_policy_set_handshake_timeout", 32)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==18 && strncmp(name, "tls_policy_get_min", 18)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==32 && strncmp(name, "tls_policy_get_handshake_timeout", 32)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==22 && strncmp(name, "tls_policy_pin_enabled", 22)==0){
          emit_x86_mov_eax_imm32(cb, 0);
          emit_x86_zero_edx(cb);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==6 && strncmp(name, "malloc", 6)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)malloc_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==4 && strncmp(name, "free", 4)==0){
          int32_t disp = (int32_t)((text_rva + (uint32_t)free_off) - (text_rva + (uint32_t)cb->len + 5));
          emit_call_rel32(cb, disp);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==6 && strncmp(name, "system", 6)==0){
          if(!ensure_iat(imp->iat_system, "system")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_x86_mov_eax_abs(cb, text_rva, imp->iat_system, 0);
          cb_emit(cb, "\xFF\xD0", 2);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==7 && strncmp(name, "putchar", 7)==0){
          if(!ensure_iat(imp->iat_putchar, "putchar")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_x86_mov_eax_abs(cb, text_rva, imp->iat_putchar, 0);
          cb_emit(cb, "\xFF\xD0", 2);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else if(nlen==7 && strncmp(name, "getchar", 7)==0){
          if(!ensure_iat(imp->iat_getchar, "getchar")){ free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1; }
          emit_x86_mov_eax_abs(cb, text_rva, imp->iat_getchar, 0);
          cb_emit(cb, "\xFF\xD0", 2);
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        } else {
          int tidx = call_target_index(finfo, fcount, name, nlen);
          if(tidx < 0){
            fprintf(stderr, "buildexe: unknown call target '%.*s'\n", nlen, name);
            free(amap.dst_reg);
            free(amap.off_bytes);
            free(label_off);
            return -1;
          }
          if(emit_func_patch_call(cb, fpatches, fpatch_n, fpatch_cap, tidx) != 0){
            free(amap.dst_reg); free(amap.off_bytes); free(label_off); return -1;
          }
          emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        }
        if(in->a >= 0){
          emit_x86_store64(cb, reg_slot_off(in->a));
        }
        break;
      }
      case I_CALLPTR: {
        if(!in->args){
          fprintf(stderr, "buildexe: callptr missing args\n");
          free(amap.dst_reg);
          free(amap.off_bytes);
          free(label_off);
          return -1;
        }
        for(int i=in->argc-1;i>=0;i--){
          emit_x86_push_arg64(cb, reg_slot_off(in->args[i]));
        }
        emit_x86_load64(cb, reg_slot_off(in->b));
        cb_emit(cb, "\xFF\xD0", 2); // call eax
        emit_x86_add_esp(cb, (uint32_t)in->argc * 8u);
        if(in->a >= 0) emit_x86_store64(cb, reg_slot_off(in->a));
        break;
      }
      case I_RET:
        if(in->a >= 0) emit_x86_load64(cb, reg_slot_off(in->a));
        else { emit_x86_mov_eax_imm32(cb, 0); emit_x86_zero_edx(cb); }
        cb_emit1(cb, 0xE9);
        if(ret_n < (int)(sizeof(ret_patches)/sizeof(ret_patches[0]))){
          ret_patches[ret_n++] = cb->len;
          cb_emit4(cb, 0);
        } else {
          fprintf(stderr, "buildexe: too many returns in %.*s\n", fn->len, fn->name);
          free(amap.dst_reg);
          free(amap.off_bytes);
          free(label_off);
          return -1;
        }
        break;
      default:
        fprintf(stderr, "buildexe: unsupported IR op %d\n", in->op);
        free(amap.dst_reg);
        free(amap.off_bytes);
        free(label_off);
        return -1;
    }
  }

  emit_x86_mov_eax_imm32(cb, 0);
  emit_x86_zero_edx(cb);
  size_t epilogue_pos = cb->len;
  emit_x86_epilogue(cb, frame);

  for(int i=0;i<ret_n;i++){
    uint32_t rip = text_rva + (uint32_t)ret_patches[i] + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)epilogue_pos) - rip);
    cb_patch4(cb, ret_patches[i], (uint32_t)disp);
  }
  for(int i=0;i<*lpatch_n;i++){
    int lid = lpatches[i].label;
    if(lid >=0 && lid < lbl_n && label_off && label_off[lid] != (size_t)-1){
      uint32_t rip = text_rva + (uint32_t)lpatches[i].pos + 4;
      int32_t disp = (int32_t)((text_rva + (uint32_t)label_off[lid]) - rip);
      cb_patch4(cb, lpatches[i].pos, (uint32_t)disp);
    } else {
      free(amap.dst_reg);
      free(amap.off_bytes);
      free(label_off);
      return -1;
    }
  }

  free(amap.dst_reg);
  free(amap.off_bytes);
  free(label_off);
  return 0;
}

static int lower_module_x86(IRModule* m, CodeBuf* cb, uint32_t text_rva,
                            const uint32_t* str_rva, int str_n,
                            uint32_t fmt_rva, uint32_t fmtf_rva, uint32_t nl_rva, uint32_t sp_rva,
                            uint32_t dot_rva, uint32_t z0_rva, uint32_t f0_rva, uint32_t onee6_rva,
                            const PeImportInfo* imp,
                            GlobalInfo* ginfo, int gcount,
                            RelocList* rel,
                            size_t* out_entry_off,
                            size_t* out_say_off, size_t* out_sayf_off, size_t* out_saystr_off, size_t* out_saymulti_off,
                            size_t* out_memcpy_off, size_t* out_memmove_off, size_t* out_memcmp_off,
                            size_t* out_time_off, size_t* out_time_ms_off, size_t* out_time_ns_off,
                            size_t* out_malloc_off, size_t* out_free_off){
  (void)fmt_rva;
  (void)fmtf_rva;
  (void)f0_rva;
  if(!m || !cb || !imp) return -1;

  FuncInfo* finfo = (FuncInfo*)calloc((size_t)m->fn_n, sizeof(FuncInfo));
  if(!finfo) return -1;
  for(int i=0;i<m->fn_n;i++){
    finfo[i].name = m->fns[i].name;
    finfo[i].len = m->fns[i].len;
    finfo[i].is_extern = m->fns[i].is_extern;
    finfo[i].param_count = m->fns[i].param_count;
  }

  // Reachability: only lower functions reachable from main
  unsigned char* reach = (unsigned char*)calloc((size_t)m->fn_n, 1);
  int* stack = (int*)malloc(sizeof(int)*(size_t)(m->fn_n > 0 ? m->fn_n : 1));
  int sp = 0;
  int main_idx = -1;
  int init_idx = -1;
  for(int i=0;i<m->fn_n;i++){
    if(finfo[i].len==4 && strncmp(finfo[i].name, "main", 4)==0){
      main_idx = i;
      break;
    }
  }
  for(int i=0;i<m->fn_n;i++){
    if(finfo[i].is_extern) continue;
    if((finfo[i].len==9 && strncmp(finfo[i].name, "tezz_init", 9)==0) ||
       (finfo[i].len==11 && strncmp(finfo[i].name, "__tezz_init", 11)==0)){
      init_idx = i;
      break;
    }
  }
  if(main_idx < 0){
    fprintf(stderr, "buildexe: main not found in IR module (fn_n=%d)\n", m->fn_n);
    free(reach);
    free(stack);
    free(finfo);
    return -1;
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
      if(in->op == I_ADDRSYM && in->name){
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
      if(in->op == I_CALL && in->name){
        int tidx = call_target_index(finfo, m->fn_n, in->name, in->nlen);
        if(tidx >= 0 && !finfo[tidx].is_extern && !reach[tidx]){
          reach[tidx] = 1;
          stack[sp++] = tidx;
        }
      }
    }
  }

  size_t say_inline_off = 0;
  size_t saystr_inline_off = 0;
  size_t sayf_inline_off = 0;
  size_t say_call_patch = 0;
  size_t say_nl_patch = 0;
  size_t sayf_call_patch = 0;
  size_t sayf_nl_patch = 0;
  size_t say_inline_str_patch = 0;
  size_t mul_off = 0, div_off = 0, mod_off = 0, shl_off = 0, shr_off = 0;

  // Entry stub: call main, ExitProcess(ret)
  *out_entry_off = cb->len;
  size_t entry_init_pos = 0;
  if(init_idx >= 0){
    cb_emit1(cb, 0xE8);
    entry_init_pos = cb->len;
    cb_emit4(cb, 0);
  }
  cb_emit1(cb, 0xE8);
  size_t entry_call_pos = cb->len;
  cb_emit4(cb, 0);
  cb_emit1(cb, 0x50); // push eax
  emit_x86_call_moffs(cb, text_rva, imp->iat_exit, rel);
  cb_emit1(cb, 0xF4);

  // mul helper (signed 64-bit)
  mul_off = cb->len;
  cb_emit(cb, "\x55\x89\xE5", 3); // push ebp; mov ebp, esp
  cb_emit(cb, "\x83\xEC\x10", 3); // sub esp, 16
  cb_emit(cb, "\x53\x56\x57", 3); // push ebx; push esi; push edi
  cb_emit(cb, "\x8B\x45\x08", 3); // mov eax, [ebp+8]
  cb_emit(cb, "\x8B\x55\x0C", 3); // mov edx, [ebp+12]
  cb_emit(cb, "\x8B\x4D\x10", 3); // mov ecx, [ebp+16]
  cb_emit(cb, "\x8B\x5D\x14", 3); // mov ebx, [ebp+20]
  cb_emit(cb, "\x89\xD6", 2);     // mov esi, edx
  cb_emit(cb, "\x31\xDE", 2);     // xor esi, ebx
  cb_emit(cb, "\xC1\xFE\x1F", 3); // sar esi, 31
  cb_emit(cb, "\x89\x75\xFC", 3); // mov [ebp-4], esi
  cb_emit(cb, "\x85\xD2", 2);     // test edx, edx
  cb_emit(cb, "\x0F\x89", 2);     // jns a_ok
  size_t ja_ok = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\xF7\xD0", 2);     // not eax
  cb_emit(cb, "\xF7\xD2", 2);     // not edx
  cb_emit(cb, "\x83\xC0\x01", 3); // add eax, 1
  cb_emit(cb, "\x83\xD2\x00", 3); // adc edx, 0
  size_t a_ok = cb->len;
  cb_emit(cb, "\x85\xDB", 2);     // test ebx, ebx
  cb_emit(cb, "\x0F\x89", 2);     // jns b_ok
  size_t jb_ok = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\xF7\xD1", 2);     // not ecx
  cb_emit(cb, "\xF7\xD3", 2);     // not ebx
  cb_emit(cb, "\x83\xC1\x01", 3); // add ecx, 1
  cb_emit(cb, "\x83\xD3\x00", 3); // adc ebx, 0
  size_t b_ok = cb->len;
  cb_emit(cb, "\x89\xC7", 2);     // mov edi, eax
  cb_emit(cb, "\xF7\xE1", 2);     // mul ecx
  cb_emit(cb, "\x89\xC7", 2);     // mov edi, eax
  cb_emit(cb, "\x89\xD6", 2);     // mov esi, edx
  cb_emit(cb, "\x8B\x45\x08", 3); // mov eax, [ebp+8]
  cb_emit(cb, "\xF7\xE3", 2);     // mul ebx
  cb_emit(cb, "\x01\xC6", 2);     // add esi, eax
  cb_emit(cb, "\x8B\x45\x0C", 3); // mov eax, [ebp+12]
  cb_emit(cb, "\xF7\xE1", 2);     // mul ecx
  cb_emit(cb, "\x01\xC6", 2);     // add esi, eax
  cb_emit(cb, "\x89\xF8", 2);     // mov eax, edi
  cb_emit(cb, "\x89\xF2", 2);     // mov edx, esi
  cb_emit(cb, "\x83\x7D\xFC\x00", 4); // cmp dword [ebp-4], 0
  cb_emit(cb, "\x0F\x84", 2);     // je mul_ret
  size_t jmul_ret = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\xF7\xD0", 2);     // not eax
  cb_emit(cb, "\xF7\xD2", 2);     // not edx
  cb_emit(cb, "\x83\xC0\x01", 3); // add eax, 1
  cb_emit(cb, "\x83\xD2\x00", 3); // adc edx, 0
  size_t mul_ret = cb->len;
  cb_emit(cb, "\x5F\x5E\x5B", 3); // pop edi; pop esi; pop ebx
  cb_emit(cb, "\x89\xEC\x5D\xC3", 4); // mov esp, ebp; pop ebp; ret
  // patch jumps
  {
    uint32_t rip = text_rva + (uint32_t)ja_ok + 4;
    cb_patch4(cb, ja_ok, (uint32_t)((text_rva + (uint32_t)a_ok) - rip));
    rip = text_rva + (uint32_t)jb_ok + 4;
    cb_patch4(cb, jb_ok, (uint32_t)((text_rva + (uint32_t)b_ok) - rip));
    rip = text_rva + (uint32_t)jmul_ret + 4;
    cb_patch4(cb, jmul_ret, (uint32_t)((text_rva + (uint32_t)mul_ret) - rip));
  }

  // div helper (signed 64-bit, internal)
  div_off = cb->len;
  emit_x86_prologue(cb, 32);
  cb_emit(cb, "\x53\x56\x57", 3); // push ebx; push esi; push edi
  emit_x86_mov_eax_membp(cb, -8);  // a low
  emit_x86_mov_edx_membp(cb, -12); // a high
  cb_emit(cb, "\x8B\x4D\x10", 3); // mov ecx, [ebp+16] (b low)
  cb_emit(cb, "\x8B\x5D\x14", 3); // mov ebx, [ebp+20] (b high)
  cb_emit(cb, "\x89\xCE", 2);     // mov esi, ecx
  cb_emit(cb, "\x09\xDE", 2);     // or esi, ebx
  cb_emit(cb, "\x0F\x84", 2); size_t j_divzero = cb->len; cb_emit4(cb, 0);
  // sign_q = (a ^ b) < 0
  cb_emit(cb, "\x89\xD6", 2);     // mov esi, edx
  cb_emit(cb, "\x31\xDE", 2);     // xor esi, ebx
  cb_emit(cb, "\xC1\xFE\x1F", 3); // sar esi, 31
  cb_emit(cb, "\x83\xE6\x01", 3); // and esi, 1
  cb_emit(cb, "\x89\x75\xFC", 3); // mov [ebp-4], esi
  // sign_a = a < 0
  cb_emit(cb, "\x89\xD6", 2);     // mov esi, edx
  cb_emit(cb, "\xC1\xFE\x1F", 3); // sar esi, 31
  cb_emit(cb, "\x83\xE6\x01", 3); // and esi, 1
  cb_emit(cb, "\x89\x75\xF8", 3); // mov [ebp-8], esi
  // if a negative: negate a
  cb_emit(cb, "\x85\xD2", 2);     // test edx, edx
  cb_emit(cb, "\x0F\x89", 2); size_t j_a_pos = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\xF7\xD0", 2);     // not eax
  cb_emit(cb, "\xF7\xD2", 2);     // not edx
  cb_emit(cb, "\x83\xC0\x01", 3); // add eax, 1
  cb_emit(cb, "\x83\xD2\x00", 3); // adc edx, 0
  size_t a_pos = cb->len;
  cb_patch4(cb, j_a_pos, (uint32_t)((text_rva + (uint32_t)a_pos) - (text_rva + (uint32_t)j_a_pos + 4)));
  // if b negative: negate b
  cb_emit(cb, "\x85\xDB", 2);     // test ebx, ebx
  cb_emit(cb, "\x0F\x89", 2); size_t j_b_pos = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\xF7\xD1", 2);     // not ecx
  cb_emit(cb, "\xF7\xD3", 2);     // not ebx
  cb_emit(cb, "\x83\xC1\x01", 3); // add ecx, 1
  cb_emit(cb, "\x83\xD3\x00", 3); // adc ebx, 0
  size_t b_pos = cb->len;
  cb_patch4(cb, j_b_pos, (uint32_t)((text_rva + (uint32_t)b_pos) - (text_rva + (uint32_t)j_b_pos + 4)));
  // unsigned div/mod loop
  cb_emit(cb, "\x31\xF6", 2);     // xor esi, esi (rem lo)
  cb_emit(cb, "\x31\xFF", 2);     // xor edi, edi (rem hi)
  cb_emit(cb, "\xC7\x45\xF4\x00\x00\x00\x00", 7); // qlo = 0
  cb_emit(cb, "\xC7\x45\xF0\x00\x00\x00\x00", 7); // qhi = 0
  cb_emit(cb, "\xC7\x45\xEC\x40\x00\x00\x00", 7); // cnt = 64
  size_t uloop = cb->len;
  cb_emit(cb, "\xD1\xE0", 2);     // shl eax, 1
  cb_emit(cb, "\xD1\xD2", 2);     // rcl edx, 1
  cb_emit(cb, "\xD1\xD6", 2);     // rcl esi, 1
  cb_emit(cb, "\xD1\xD7", 2);     // rcl edi, 1
  cb_emit(cb, "\xD1\x65\xF4", 3); // shl dword [ebp-12], 1
  cb_emit(cb, "\xD1\x55\xF0", 3); // rcl dword [ebp-16], 1
  cb_emit(cb, "\x39\xDF", 2);     // cmp edi, ebx
  cb_emit(cb, "\x0F\x82", 2); size_t j_nosub = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x0F\x87", 2); size_t j_dosub = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x39\xCE", 2);     // cmp esi, ecx
  cb_emit(cb, "\x0F\x82", 2); size_t j_nosub2 = cb->len; cb_emit4(cb, 0);
  size_t dosub = cb->len;
  cb_emit(cb, "\x29\xCE", 2);     // sub esi, ecx
  cb_emit(cb, "\x19\xDF", 2);     // sbb edi, ebx
  cb_emit(cb, "\x83\x4D\xF4\x01", 4); // or dword [ebp-12], 1
  size_t nosub = cb->len;
  cb_patch4(cb, j_dosub, (uint32_t)((text_rva + (uint32_t)dosub) - (text_rva + (uint32_t)j_dosub + 4)));
  cb_patch4(cb, j_nosub, (uint32_t)((text_rva + (uint32_t)nosub) - (text_rva + (uint32_t)j_nosub + 4)));
  cb_patch4(cb, j_nosub2, (uint32_t)((text_rva + (uint32_t)nosub) - (text_rva + (uint32_t)j_nosub2 + 4)));
  cb_emit(cb, "\xFF\x4D\xEC", 3); // dec dword [ebp-20]
  cb_emit(cb, "\x0F\x85", 2); size_t j_loop = cb->len; cb_emit4(cb, 0);
  cb_patch4(cb, j_loop, (uint32_t)((text_rva + (uint32_t)uloop) - (text_rva + (uint32_t)j_loop + 4)));
  // quotient
  emit_x86_mov_eax_membp(cb, 12);
  emit_x86_mov_edx_membp(cb, 16);
  cb_emit(cb, "\x83\x7D\xFC\x00", 4); // cmp dword [ebp-4], 0
  cb_emit(cb, "\x0F\x84", 2); size_t j_q_ok = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\xF7\xD0", 2); // not eax
  cb_emit(cb, "\xF7\xD2", 2); // not edx
  cb_emit(cb, "\x83\xC0\x01", 3); // add eax, 1
  cb_emit(cb, "\x83\xD2\x00", 3); // adc edx, 0
  size_t q_ok = cb->len;
  cb_patch4(cb, j_q_ok, (uint32_t)((text_rva + (uint32_t)q_ok) - (text_rva + (uint32_t)j_q_ok + 4)));
  cb_emit1(cb, 0xE9); size_t j_done = cb->len; cb_emit4(cb, 0);
  size_t div_zero = cb->len;
  emit_x86_mov_eax_imm32(cb, 0);
  emit_x86_zero_edx(cb);
  size_t div_done = cb->len;
  cb_patch4(cb, j_divzero, (uint32_t)((text_rva + (uint32_t)div_zero) - (text_rva + (uint32_t)j_divzero + 4)));
  cb_patch4(cb, j_done, (uint32_t)((text_rva + (uint32_t)div_done) - (text_rva + (uint32_t)j_done + 4)));
  cb_emit(cb, "\x5F\x5E\x5B", 3); // pop edi; pop esi; pop ebx
  emit_x86_epilogue(cb, 32);

  // mod helper (signed 64-bit, internal)
  mod_off = cb->len;
  emit_x86_prologue(cb, 32);
  cb_emit(cb, "\x53\x56\x57", 3); // push ebx; push esi; push edi
  emit_x86_mov_eax_membp(cb, -8);  // a low
  emit_x86_mov_edx_membp(cb, -12); // a high
  cb_emit(cb, "\x8B\x4D\x10", 3); // mov ecx, [ebp+16] (b low)
  cb_emit(cb, "\x8B\x5D\x14", 3); // mov ebx, [ebp+20] (b high)
  cb_emit(cb, "\x89\xCE", 2);     // mov esi, ecx
  cb_emit(cb, "\x09\xDE", 2);     // or esi, ebx
  cb_emit(cb, "\x0F\x84", 2); size_t j_modzero = cb->len; cb_emit4(cb, 0);
  // sign_a = a < 0
  cb_emit(cb, "\x89\xD6", 2);     // mov esi, edx
  cb_emit(cb, "\xC1\xFE\x1F", 3); // sar esi, 31
  cb_emit(cb, "\x83\xE6\x01", 3); // and esi, 1
  cb_emit(cb, "\x89\x75\xF8", 3); // mov [ebp-8], esi
  // if a negative: negate a
  cb_emit(cb, "\x85\xD2", 2);     // test edx, edx
  cb_emit(cb, "\x0F\x89", 2); size_t j_ma_pos = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\xF7\xD0", 2);     // not eax
  cb_emit(cb, "\xF7\xD2", 2);     // not edx
  cb_emit(cb, "\x83\xC0\x01", 3); // add eax, 1
  cb_emit(cb, "\x83\xD2\x00", 3); // adc edx, 0
  size_t ma_pos = cb->len;
  cb_patch4(cb, j_ma_pos, (uint32_t)((text_rva + (uint32_t)ma_pos) - (text_rva + (uint32_t)j_ma_pos + 4)));
  // if b negative: negate b
  cb_emit(cb, "\x85\xDB", 2);     // test ebx, ebx
  cb_emit(cb, "\x0F\x89", 2); size_t j_mb_pos = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\xF7\xD1", 2);     // not ecx
  cb_emit(cb, "\xF7\xD3", 2);     // not ebx
  cb_emit(cb, "\x83\xC1\x01", 3); // add ecx, 1
  cb_emit(cb, "\x83\xD3\x00", 3); // adc ebx, 0
  size_t mb_pos = cb->len;
  cb_patch4(cb, j_mb_pos, (uint32_t)((text_rva + (uint32_t)mb_pos) - (text_rva + (uint32_t)j_mb_pos + 4)));
  // unsigned div/mod loop
  cb_emit(cb, "\x31\xF6", 2);     // xor esi, esi (rem lo)
  cb_emit(cb, "\x31\xFF", 2);     // xor edi, edi (rem hi)
  cb_emit(cb, "\xC7\x45\xF4\x00\x00\x00\x00", 7); // qlo = 0
  cb_emit(cb, "\xC7\x45\xF0\x00\x00\x00\x00", 7); // qhi = 0
  cb_emit(cb, "\xC7\x45\xEC\x40\x00\x00\x00", 7); // cnt = 64
  size_t mloop = cb->len;
  cb_emit(cb, "\xD1\xE0", 2);     // shl eax, 1
  cb_emit(cb, "\xD1\xD2", 2);     // rcl edx, 1
  cb_emit(cb, "\xD1\xD6", 2);     // rcl esi, 1
  cb_emit(cb, "\xD1\xD7", 2);     // rcl edi, 1
  cb_emit(cb, "\xD1\x65\xF4", 3); // shl dword [ebp-12], 1
  cb_emit(cb, "\xD1\x55\xF0", 3); // rcl dword [ebp-16], 1
  cb_emit(cb, "\x39\xDF", 2);     // cmp edi, ebx
  cb_emit(cb, "\x0F\x82", 2); size_t j_mnosub = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x0F\x87", 2); size_t j_mdosub = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x39\xCE", 2);     // cmp esi, ecx
  cb_emit(cb, "\x0F\x82", 2); size_t j_mnosub2 = cb->len; cb_emit4(cb, 0);
  size_t mdosub = cb->len;
  cb_emit(cb, "\x29\xCE", 2);     // sub esi, ecx
  cb_emit(cb, "\x19\xDF", 2);     // sbb edi, ebx
  cb_emit(cb, "\x83\x4D\xF4\x01", 4); // or dword [ebp-12], 1
  size_t mnosub = cb->len;
  cb_patch4(cb, j_mdosub, (uint32_t)((text_rva + (uint32_t)mdosub) - (text_rva + (uint32_t)j_mdosub + 4)));
  cb_patch4(cb, j_mnosub, (uint32_t)((text_rva + (uint32_t)mnosub) - (text_rva + (uint32_t)j_mnosub + 4)));
  cb_patch4(cb, j_mnosub2, (uint32_t)((text_rva + (uint32_t)mnosub) - (text_rva + (uint32_t)j_mnosub2 + 4)));
  cb_emit(cb, "\xFF\x4D\xEC", 3); // dec dword [ebp-20]
  cb_emit(cb, "\x0F\x85", 2); size_t j_mloop = cb->len; cb_emit4(cb, 0);
  cb_patch4(cb, j_mloop, (uint32_t)((text_rva + (uint32_t)mloop) - (text_rva + (uint32_t)j_mloop + 4)));
  // remainder
  cb_emit(cb, "\x83\x7D\xF8\x00", 4); // cmp dword [ebp-8], 0
  cb_emit(cb, "\x0F\x84", 2); size_t j_r_ok = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\xF7\xD6", 2); // not esi
  cb_emit(cb, "\xF7\xD7", 2); // not edi
  cb_emit(cb, "\x83\xC6\x01", 3); // add esi, 1
  cb_emit(cb, "\x83\xD7\x00", 3); // adc edi, 0
  size_t r_ok = cb->len;
  cb_patch4(cb, j_r_ok, (uint32_t)((text_rva + (uint32_t)r_ok) - (text_rva + (uint32_t)j_r_ok + 4)));
  cb_emit(cb, "\x89\xF0", 2); // mov eax, esi
  cb_emit(cb, "\x89\xFA", 2); // mov edx, edi
  cb_emit1(cb, 0xE9); size_t j_mdone = cb->len; cb_emit4(cb, 0);
  size_t mod_zero = cb->len;
  emit_x86_mov_eax_imm32(cb, 0);
  emit_x86_zero_edx(cb);
  size_t mod_done = cb->len;
  cb_patch4(cb, j_modzero, (uint32_t)((text_rva + (uint32_t)mod_zero) - (text_rva + (uint32_t)j_modzero + 4)));
  cb_patch4(cb, j_mdone, (uint32_t)((text_rva + (uint32_t)mod_done) - (text_rva + (uint32_t)j_mdone + 4)));
  cb_emit(cb, "\x5F\x5E\x5B", 3); // pop edi; pop esi; pop ebx
  emit_x86_epilogue(cb, 32);

  // shl helper (64-bit)
  shl_off = cb->len;
  cb_emit(cb, "\x55\x89\xE5", 3);
  cb_emit(cb, "\x8B\x4D\x10", 3); // mov ecx, [ebp+16]
  cb_emit(cb, "\x83\xE1\x3F", 3); // and ecx, 63
  cb_emit(cb, "\x83\xF9\x00", 3); // cmp ecx, 0
  cb_emit(cb, "\x0F\x84", 2); size_t jshl_ret = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x83\xF9\x20", 3); // cmp ecx, 32
  cb_emit(cb, "\x0F\x83", 2); size_t jshl_ge32 = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x8B\x45\x08", 3); // mov eax, [ebp+8]
  cb_emit(cb, "\x8B\x55\x0C", 3); // mov edx, [ebp+12]
  cb_emit(cb, "\x0F\xA4\xD0", 3); // shld edx, eax, cl
  cb_emit(cb, "\xD3\xE0", 2);     // shl eax, cl
  cb_emit(cb, "\xE9", 1); size_t jshl_done = cb->len; cb_emit4(cb, 0);
  size_t shl_ge32 = cb->len;
  cb_emit(cb, "\x8B\x45\x08", 3); // mov eax, [ebp+8]
  cb_emit(cb, "\x31\xD2", 2);     // xor edx, edx
  cb_emit(cb, "\x83\xE9\x20", 3); // sub ecx, 32
  cb_emit(cb, "\xD3\xE0", 2);     // shl eax, cl
  cb_emit(cb, "\x89\xC2", 2);     // mov edx, eax
  cb_emit(cb, "\x31\xC0", 2);     // xor eax, eax
  size_t shl_done = cb->len;
  cb_emit(cb, "\x5D\xC3", 2);
  size_t shl_ret = cb->len;
  cb_emit(cb, "\x8B\x45\x08", 3);
  cb_emit(cb, "\x8B\x55\x0C", 3);
  cb_emit(cb, "\x5D\xC3", 2);
  {
    uint32_t rip = text_rva + (uint32_t)jshl_ret + 4;
    cb_patch4(cb, jshl_ret, (uint32_t)((text_rva + (uint32_t)shl_ret) - rip));
    rip = text_rva + (uint32_t)jshl_ge32 + 4;
    cb_patch4(cb, jshl_ge32, (uint32_t)((text_rva + (uint32_t)shl_ge32) - rip));
    rip = text_rva + (uint32_t)jshl_done + 4;
    cb_patch4(cb, jshl_done, (uint32_t)((text_rva + (uint32_t)shl_done) - rip));
  }

  // shr helper (64-bit arithmetic)
  shr_off = cb->len;
  cb_emit(cb, "\x55\x89\xE5", 3);
  cb_emit(cb, "\x8B\x4D\x10", 3); // mov ecx, [ebp+16]
  cb_emit(cb, "\x83\xE1\x3F", 3); // and ecx, 63
  cb_emit(cb, "\x83\xF9\x00", 3); // cmp ecx, 0
  cb_emit(cb, "\x0F\x84", 2); size_t jshr_ret = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x83\xF9\x20", 3); // cmp ecx, 32
  cb_emit(cb, "\x0F\x83", 2); size_t jshr_ge32 = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x8B\x45\x08", 3); // mov eax, [ebp+8]
  cb_emit(cb, "\x8B\x55\x0C", 3); // mov edx, [ebp+12]
  cb_emit(cb, "\x0F\xAC\xD0", 3); // shrd eax, edx, cl
  cb_emit(cb, "\xD3\xFA", 2);     // sar edx, cl
  cb_emit(cb, "\xE9", 1); size_t jshr_done = cb->len; cb_emit4(cb, 0);
  size_t shr_ge32 = cb->len;
  cb_emit(cb, "\x8B\x45\x0C", 3); // mov eax, [ebp+12]
  cb_emit(cb, "\x8B\x55\x0C", 3); // mov edx, [ebp+12]
  cb_emit(cb, "\x83\xE9\x20", 3); // sub ecx, 32
  cb_emit(cb, "\xD3\xF8", 2);     // sar eax, cl
  cb_emit(cb, "\xC1\xFA\x1F", 3); // sar edx, 31
  size_t shr_done = cb->len;
  cb_emit(cb, "\x5D\xC3", 2);
  size_t shr_ret = cb->len;
  cb_emit(cb, "\x8B\x45\x08", 3);
  cb_emit(cb, "\x8B\x55\x0C", 3);
  cb_emit(cb, "\x5D\xC3", 2);
  {
    uint32_t rip = text_rva + (uint32_t)jshr_ret + 4;
    cb_patch4(cb, jshr_ret, (uint32_t)((text_rva + (uint32_t)shr_ret) - rip));
    rip = text_rva + (uint32_t)jshr_ge32 + 4;
    cb_patch4(cb, jshr_ge32, (uint32_t)((text_rva + (uint32_t)shr_ge32) - rip));
    rip = text_rva + (uint32_t)jshr_done + 4;
    cb_patch4(cb, jshr_done, (uint32_t)((text_rva + (uint32_t)shr_done) - rip));
  }

  // time_now_ns helper
  if(out_time_ns_off) *out_time_ns_off = cb->len;
  emit_x86_prologue(cb, 32);
  emit_x86_lea_eax_membp(cb, 8); // &FILETIME (rbp-8)
  emit_x86_push_reg(cb, 0);
  emit_x86_call_moffs(cb, text_rva, imp->iat_gettime, rel);
  emit_x86_mov_eax_membp(cb, -(8));
  emit_x86_mov_edx_membp(cb, 4);
  emit_x86_push_imm32(cb, 100);
  emit_x86_push_reg(cb, 2);
  emit_x86_push_reg(cb, 0);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)mul_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_add_esp(cb, 12);
  emit_x86_epilogue(cb, 32);

  // time_now_ms helper
  if(out_time_ms_off) *out_time_ms_off = cb->len;
  emit_x86_prologue(cb, 32);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)(out_time_ns_off ? *out_time_ns_off : 0)) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_push_imm32(cb, 0);
  emit_x86_push_imm32(cb, 1000000);
  emit_x86_push_reg(cb, 2);
  emit_x86_push_reg(cb, 0);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)div_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_add_esp(cb, 16);
  emit_x86_epilogue(cb, 32);

  // time_now helper
  if(out_time_off) *out_time_off = cb->len;
  emit_x86_prologue(cb, 32);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)(out_time_ns_off ? *out_time_ns_off : 0)) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_push_imm32(cb, 0);
  emit_x86_push_imm32(cb, 1000000000u);
  emit_x86_push_reg(cb, 2);
  emit_x86_push_reg(cb, 0);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)div_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_add_esp(cb, 16);
  emit_x86_epilogue(cb, 32);

  // malloc helper (VirtualAlloc)
  if(out_malloc_off) *out_malloc_off = cb->len;
  emit_x86_prologue(cb, 32);
  emit_x86_mov_eax_membp(cb, -(8)); // size low
  emit_x86_push_imm32(cb, 0x04);
  emit_x86_push_imm32(cb, 0x3000);
  emit_x86_push_reg(cb, 0);
  emit_x86_push_imm32(cb, 0);
  emit_x86_call_moffs(cb, text_rva, imp->iat_valloc, rel);
  emit_x86_zero_edx(cb);
  emit_x86_epilogue(cb, 32);

  // free helper (VirtualFree)
  if(out_free_off) *out_free_off = cb->len;
  emit_x86_prologue(cb, 32);
  emit_x86_mov_eax_membp(cb, -(8)); // ptr low
  emit_x86_push_imm32(cb, 0x8000);
  emit_x86_push_imm32(cb, 0);
  emit_x86_push_reg(cb, 0);
  emit_x86_call_moffs(cb, text_rva, imp->iat_vfree, rel);
  emit_x86_mov_eax_imm32(cb, 0);
  emit_x86_zero_edx(cb);
  emit_x86_epilogue(cb, 32);

  // say helper (integer with newline) -> calls say_inline + newline
  *out_say_off = cb->len;
  emit_x86_prologue(cb, 128);
  cb_emit(cb, "\xFF\x75\x0C", 3); // push [ebp+12]
  cb_emit(cb, "\xFF\x75\x08", 3); // push [ebp+8]
  cb_emit1(cb, 0xE8); // call say_inline (patch)
  say_call_patch = cb->len;
  cb_emit4(cb, 0);
  emit_x86_add_esp(cb, 8);
  emit_x86_push_imm32(cb, 0);
  emit_x86_mov_eax_abs(cb, text_rva, nl_rva, rel);
  emit_x86_push_reg(cb, 0);
  cb_emit1(cb, 0xE8); // call saystr_inline (patch)
  say_nl_patch = cb->len;
  cb_emit4(cb, 0);
  emit_x86_add_esp(cb, 8);
  emit_x86_mov_eax_imm32(cb, 0);
  emit_x86_zero_edx(cb);
  emit_x86_epilogue(cb, 128);

  // say_str helper (with newline)
  *out_saystr_off = cb->len;
  emit_x86_prologue(cb, 128);
  emit_x86_mov_eax_membp(cb, -(8));
  emit_x86_push_reg(cb, 0);
  emit_x86_call_moffs(cb, text_rva, imp->iat_lstrlen, rel);
  emit_x86_mov_membp_eax(cb, 16); // save len
  emit_x86_push_imm32(cb, 0xFFFFFFF5);
  emit_x86_call_moffs(cb, text_rva, imp->iat_getstd, rel);
  emit_x86_mov_membp_eax(cb, 12);
  emit_x86_mov_eax_membp(cb, 16);
  cb_emit(cb, "\x89\xC1", 2); // len
  emit_x86_push_imm32(cb, 0);
  emit_x86_lea_eax_membp(cb, 8);
  emit_x86_push_reg(cb, 0);
  emit_x86_push_reg(cb, 1);
  emit_x86_mov_eax_membp(cb, -(8));
  emit_x86_push_reg(cb, 0);
  emit_x86_mov_eax_membp(cb, 12);
  emit_x86_push_reg(cb, 0);
  emit_x86_call_moffs(cb, text_rva, imp->iat_write, rel);
  emit_x86_push_imm32(cb, 0);
  emit_x86_lea_eax_membp(cb, 8);
  emit_x86_push_reg(cb, 0);
  emit_x86_push_imm32(cb, 1);
  emit_x86_mov_eax_abs(cb, text_rva, nl_rva, rel);
  emit_x86_push_reg(cb, 0);
  emit_x86_mov_eax_membp(cb, 12);
  emit_x86_push_reg(cb, 0);
  emit_x86_call_moffs(cb, text_rva, imp->iat_write, rel);
  emit_x86_mov_eax_imm32(cb, 0);
  emit_x86_zero_edx(cb);
  emit_x86_epilogue(cb, 128);

  // say_f helper (fixed 6-digit, no CRT float)
  *out_sayf_off = cb->len;
  emit_x86_prologue(cb, 128);
  // call say_f_inline, then print newline
  cb_emit(cb, "\xFF\x75\x0C", 3); // push [ebp+12]
  cb_emit(cb, "\xFF\x75\x08", 3); // push [ebp+8]
  cb_emit1(cb, 0xE8); // call sayf_inline (patch)
  sayf_call_patch = cb->len;
  cb_emit4(cb, 0);
  emit_x86_add_esp(cb, 8);
  emit_x86_push_imm32(cb, 0);
  emit_x86_mov_eax_abs(cb, text_rva, nl_rva, rel);
  emit_x86_push_reg(cb, 0);
  cb_emit1(cb, 0xE8); // call saystr_inline (patch)
  sayf_nl_patch = cb->len;
  cb_emit4(cb, 0);
  emit_x86_add_esp(cb, 8);
  emit_x86_mov_eax_imm32(cb, 0);
  emit_x86_zero_edx(cb);
  emit_x86_epilogue(cb, 128);

  // say_inline helper (no newline, no CRT) -> prints full i64
  say_inline_off = cb->len;
  emit_x86_prologue(cb, 128);
  cb_emit1(cb, 0x53); // push ebx (callee-saved)
  emit_x86_mov_eax_membp(cb, -(8));   // low
  emit_x86_mov_edx_membp(cb, -(12));  // high
  cb_emit(cb, "\x31\xC9", 2);         // xor ecx, ecx (sign=0)
  cb_emit(cb, "\x85\xD2", 2);         // test edx, edx
  cb_emit(cb, "\x0F\x89", 2); size_t jns_abs = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\xF7\xD0", 2);         // not eax
  cb_emit(cb, "\xF7\xD2", 2);         // not edx
  cb_emit(cb, "\x83\xC0\x01", 3);     // add eax, 1
  cb_emit(cb, "\x83\xD2\x00", 3);     // adc edx, 0
  cb_emit(cb, "\xB9\x01\x00\x00\x00", 5); // mov ecx, 1
  size_t abs_done = cb->len;
  cb_patch4(cb, jns_abs, (uint32_t)((text_rva + (uint32_t)abs_done) - (text_rva + (uint32_t)jns_abs + 4)));
  cb_emit(cb, "\x89\x4D\xF0", 3);     // mov [ebp-16], ecx (sign)
  emit_x86_mov_membp_eax(cb, 8);      // save value low
  emit_x86_mov_membp_edx(cb, 12);     // save value high
  cb_emit(cb, "\xC6\x45\xC0\x00", 4); // mov byte [ebp-64], 0
  cb_emit(cb, "\x8D\x4D\xBF", 3);     // lea ecx, [ebp-65] (write ptr)
  cb_emit(cb, "\x89\x4D\xEC", 3);     // mov [ebp-20], ecx
  emit_x86_mov_eax_membp(cb, 8);
  emit_x86_mov_edx_membp(cb, 12);
  cb_emit(cb, "\x09\xD0", 2);         // or eax, edx
  cb_emit(cb, "\x0F\x84", 2); size_t j_zero_si = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x83\xFA\x00", 3);     // cmp edx, 0
  cb_emit(cb, "\x0F\x85", 2); size_t j_to64_si = cb->len; cb_emit4(cb, 0);
  // 32-bit loop (eax holds value)
  size_t loop32_si = cb->len;
  cb_emit(cb, "\x31\xD2", 2);         // xor edx, edx
  cb_emit(cb, "\xB9\x0A\x00\x00\x00", 5); // mov ecx, 10
  cb_emit(cb, "\xF7\xF1", 2);         // div ecx
  cb_emit(cb, "\x8B\x4D\xEC", 3);     // mov ecx, [ebp-20]
  cb_emit(cb, "\x80\xC2\x30", 3);     // add dl, '0'
  cb_emit(cb, "\x88\x11", 2);         // mov [ecx], dl
  cb_emit1(cb, 0x49);                 // dec ecx
  cb_emit(cb, "\x89\x4D\xEC", 3);     // mov [ebp-20], ecx
  cb_emit(cb, "\x85\xC0", 2);         // test eax, eax
  cb_emit(cb, "\x0F\x85", 2); size_t j_loop32_si = cb->len; cb_emit4(cb, 0);
  cb_emit1(cb, 0xE9); size_t j_after_si = cb->len; cb_emit4(cb, 0);
  // zero case
  size_t zero_si = cb->len;
  cb_patch4(cb, j_zero_si, (uint32_t)((text_rva + (uint32_t)zero_si) - (text_rva + (uint32_t)j_zero_si + 4)));
  cb_emit(cb, "\x8B\x4D\xEC", 3);     // mov ecx, [ebp-20]
  cb_emit(cb, "\xC6\x01\x30", 3);     // mov byte [ecx], '0'
  cb_emit1(cb, 0x49);                 // dec ecx
  cb_emit(cb, "\x89\x4D\xEC", 3);     // mov [ebp-20], ecx
  cb_emit1(cb, 0xE9); size_t j_after_zero_si = cb->len; cb_emit4(cb, 0);
  // 64-bit loop
  size_t loop64_si = cb->len;
  cb_patch4(cb, j_to64_si, (uint32_t)((text_rva + (uint32_t)loop64_si) - (text_rva + (uint32_t)j_to64_si + 4)));
  cb_emit(cb, "\x8B\x4D\xEC", 3);     // mov ecx, [ebp-20]
  emit_x86_push_imm32(cb, 0);
  emit_x86_push_imm32(cb, 10);
  cb_emit(cb, "\xFF\x75\xF4", 3);     // push [ebp-12]
  cb_emit(cb, "\xFF\x75\xF8", 3);     // push [ebp-8]
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)mod_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_add_esp(cb, 16);
  cb_emit(cb, "\x8B\x4D\xEC", 3);     // mov ecx, [ebp-20]
  cb_emit(cb, "\x04\x30", 2);         // add al, '0'
  cb_emit(cb, "\x88\x01", 2);         // mov [ecx], al
  cb_emit1(cb, 0x49);                 // dec ecx
  cb_emit(cb, "\x89\x4D\xEC", 3);     // mov [ebp-20], ecx
  emit_x86_push_imm32(cb, 0);
  emit_x86_push_imm32(cb, 10);
  cb_emit(cb, "\xFF\x75\xF4", 3);     // push [ebp-12]
  cb_emit(cb, "\xFF\x75\xF8", 3);     // push [ebp-8]
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)div_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_add_esp(cb, 16);
  emit_x86_mov_membp_eax(cb, 8);
  emit_x86_mov_membp_edx(cb, 12);
  cb_emit(cb, "\x09\xD0", 2);         // or eax, edx
  cb_emit(cb, "\x0F\x85", 2); size_t j_loop64_si = cb->len; cb_emit4(cb, 0);
  size_t after_si = cb->len;
  cb_patch4(cb, j_loop64_si, (uint32_t)((text_rva + (uint32_t)loop64_si) - (text_rva + (uint32_t)j_loop64_si + 4)));
  cb_patch4(cb, j_loop32_si, (uint32_t)((text_rva + (uint32_t)loop32_si) - (text_rva + (uint32_t)j_loop32_si + 4)));
  cb_patch4(cb, j_after_si, (uint32_t)((text_rva + (uint32_t)after_si) - (text_rva + (uint32_t)j_after_si + 4)));
  cb_patch4(cb, j_after_zero_si, (uint32_t)((text_rva + (uint32_t)after_si) - (text_rva + (uint32_t)j_after_zero_si + 4)));
  cb_emit(cb, "\x8B\x4D\xEC", 3);     // mov ecx, [ebp-20]
  cb_emit(cb, "\x83\x7D\xF0\x00", 4); // cmp dword [ebp-16], 0
  cb_emit(cb, "\x0F\x84", 2); size_t je_nosign = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\xC6\x01\x2D", 3);     // mov byte [ecx], '-'
  cb_emit1(cb, 0x49);                 // dec ecx
  size_t nosign = cb->len;
  cb_patch4(cb, je_nosign, (uint32_t)((text_rva + (uint32_t)nosign) - (text_rva + (uint32_t)je_nosign + 4)));
  cb_emit(cb, "\x8D\x41\x01", 3);     // lea eax, [ecx+1]
  cb_emit1(cb, 0x50);                 // push eax
  cb_emit1(cb, 0xE8);                 // call saystr_inline (patch)
  say_inline_str_patch = cb->len;
  cb_emit4(cb, 0);
  emit_x86_add_esp(cb, 4);
  emit_x86_mov_eax_imm32(cb, 0);
  emit_x86_zero_edx(cb);
  cb_emit1(cb, 0x5B); // pop ebx
  emit_x86_epilogue(cb, 128);

  // say_str_inline helper
  saystr_inline_off = cb->len;
  emit_x86_prologue(cb, 128);
  emit_x86_mov_eax_membp(cb, -(8));
  emit_x86_push_reg(cb, 0);
  emit_x86_call_moffs(cb, text_rva, imp->iat_lstrlen, rel);
  emit_x86_mov_membp_eax(cb, 16); // save len
  emit_x86_push_imm32(cb, 0xFFFFFFF5);
  emit_x86_call_moffs(cb, text_rva, imp->iat_getstd, rel);
  emit_x86_mov_membp_eax(cb, 12);
  emit_x86_mov_eax_membp(cb, 16);
  cb_emit(cb, "\x89\xC1", 2);
  emit_x86_push_imm32(cb, 0);
  emit_x86_lea_eax_membp(cb, 8);
  emit_x86_push_reg(cb, 0);
  emit_x86_push_reg(cb, 1);
  emit_x86_mov_eax_membp(cb, -(8));
  emit_x86_push_reg(cb, 0);
  emit_x86_mov_eax_membp(cb, 12);
  emit_x86_push_reg(cb, 0);
  emit_x86_call_moffs(cb, text_rva, imp->iat_write, rel);
  emit_x86_mov_eax_imm32(cb, 0);
  emit_x86_zero_edx(cb);
  emit_x86_epilogue(cb, 128);

  // say_f_inline helper (fixed 6-digit, x87)
  sayf_inline_off = cb->len;
  emit_x86_prologue(cb, 128);
  cb_emit1(cb, 0x53); // push ebx (callee-saved)
  cb_emit(cb, "\xDB\xE3", 2); // fninit (clear x87 state)
  // set FPU rounding to truncate
  cb_emit(cb, "\xD9\x7D\xD4", 3); // fnstcw [ebp-44]
  cb_emit(cb, "\x66\x8B\x45\xD4", 4); // mov ax, [ebp-44]
  cb_emit(cb, "\x66\x0D\x00\x0C", 4); // or ax, 0x0C00
  cb_emit(cb, "\x66\x89\x45\xD2", 4); // mov [ebp-46], ax
  cb_emit(cb, "\xD9\x6D\xD2", 3); // fldcw [ebp-46]
  // int part -> [ebp-16..-12]
  cb_emit(cb, "\xDD\x45\x08", 3); // fld qword [ebp+8]
  cb_emit(cb, "\xDF\x7D\xF0", 3); // fistp qword [ebp-16]
  // abs(int part) -> [ebp-24..-20]
  emit_x86_mov_eax_membp(cb, 16);
  emit_x86_mov_edx_membp(cb, 12);
  cb_emit(cb, "\x85\xD2", 2); // test edx, edx
  cb_emit(cb, "\x0F\x89", 2); size_t j_abs_f = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\xF7\xD0", 2); // not eax
  cb_emit(cb, "\xF7\xD2", 2); // not edx
  cb_emit(cb, "\x83\xC0\x01", 3); // add eax, 1
  cb_emit(cb, "\x83\xD2\x00", 3); // adc edx, 0
  size_t abs_done_f = cb->len;
  cb_patch4(cb, j_abs_f, (uint32_t)((text_rva + (uint32_t)abs_done_f) - (text_rva + (uint32_t)j_abs_f + 4)));
  emit_x86_mov_membp_eax(cb, 24);
  emit_x86_mov_membp_edx(cb, 20);
  // scaled_i = abs(value) * 1e6 -> [ebp-32..-28]
  cb_emit(cb, "\xDD\x45\x08", 3); // fld qword [ebp+8]
  cb_emit(cb, "\xD9\xE1", 2);     // fabs
  emit_x86_fld_abs(cb, text_rva, onee6_rva, rel);
  cb_emit(cb, "\xDE\xC9", 2);     // fmulp st1, st0
  cb_emit(cb, "\xDF\x7D\xE0", 3); // fistp qword [ebp-32]
  cb_emit(cb, "\xD9\x6D\xD4", 3); // fldcw [ebp-44] restore
  // print int part
  emit_x86_mov_eax_membp(cb, 16);
  emit_x86_mov_edx_membp(cb, 12);
  emit_x86_push_reg(cb, 2);
  emit_x86_push_reg(cb, 0);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)say_inline_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_add_esp(cb, 8);
  // frac = scaled_i - abs_int_part * 1e6 -> [ebp-32..-28]
  emit_x86_push_imm32(cb, 0);
  emit_x86_push_imm32(cb, 1000000u);
  cb_emit(cb, "\xFF\x75\xEC", 3); // push [ebp-20]
  cb_emit(cb, "\xFF\x75\xE8", 3); // push [ebp-24]
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)mul_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_add_esp(cb, 16);
  emit_x86_mov_membp_eax(cb, 40); // product low
  emit_x86_mov_membp_edx(cb, 36); // product high
  emit_x86_mov_eax_membp(cb, 32); // scaled low
  cb_emit(cb, "\x89\xC1", 2);     // mov ecx, eax
  emit_x86_mov_edx_membp(cb, 28); // scaled high
  cb_emit(cb, "\x89\xD3", 2);     // mov ebx, edx
  emit_x86_mov_eax_membp(cb, 40); // product low
  emit_x86_mov_edx_membp(cb, 36); // product high
  cb_emit(cb, "\x29\xC1", 2);     // sub ecx, eax
  cb_emit(cb, "\x19\xD3", 2);     // sbb ebx, edx
  cb_emit(cb, "\x85\xDB", 2);     // test ebx, ebx
  cb_emit(cb, "\x0F\x89", 2); size_t j_frac_ok = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\xF7\xD1", 2);     // not ecx
  cb_emit(cb, "\xF7\xD3", 2);     // not ebx
  cb_emit(cb, "\x83\xC1\x01", 3); // add ecx, 1
  cb_emit(cb, "\x83\xD3\x00", 3); // adc ebx, 0
  size_t frac_done = cb->len;
  cb_patch4(cb, j_frac_ok, (uint32_t)((text_rva + (uint32_t)frac_done) - (text_rva + (uint32_t)j_frac_ok + 4)));
  cb_emit(cb, "\x89\xC8", 2);     // mov eax, ecx
  cb_emit(cb, "\x89\xDA", 2);     // mov edx, ebx
  emit_x86_mov_membp_eax(cb, 32);
  emit_x86_mov_membp_edx(cb, 28);
  // print dot
  emit_x86_push_imm32(cb, 0);
  emit_x86_mov_eax_abs(cb, text_rva, dot_rva, rel);
  emit_x86_push_reg(cb, 0);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_add_esp(cb, 8);
  // zero padding for frac (6 digits)
  emit_x86_mov_eax_membp(cb, 32);
  cb_emit(cb, "\x83\xF8\x0A", 3); // cmp eax, 10
  cb_emit(cb, "\x0F\x83", 2); size_t jpad1 = cb->len; cb_emit4(cb, 0);
  emit_x86_push_imm32(cb, 0);
  emit_x86_mov_eax_abs(cb, text_rva, z0_rva, rel);
  emit_x86_push_reg(cb, 0);
  { int32_t d = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - (text_rva + (uint32_t)cb->len + 5)); emit_call_rel32(cb, d); }
  emit_x86_add_esp(cb, 8);
  size_t pad1_end = cb->len;
  cb_patch4(cb, jpad1, (uint32_t)((text_rva + (uint32_t)pad1_end) - (text_rva + (uint32_t)jpad1 + 4)));
  emit_x86_mov_eax_membp(cb, 32);
  cb_emit(cb, "\x3D\x64\x00\x00\x00", 5); // cmp eax, 100
  cb_emit(cb, "\x0F\x83", 2); size_t jpad2 = cb->len; cb_emit4(cb, 0);
  emit_x86_push_imm32(cb, 0);
  emit_x86_mov_eax_abs(cb, text_rva, z0_rva, rel);
  emit_x86_push_reg(cb, 0);
  { int32_t d = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - (text_rva + (uint32_t)cb->len + 5)); emit_call_rel32(cb, d); }
  emit_x86_add_esp(cb, 8);
  size_t pad2_end = cb->len;
  cb_patch4(cb, jpad2, (uint32_t)((text_rva + (uint32_t)pad2_end) - (text_rva + (uint32_t)jpad2 + 4)));
  emit_x86_mov_eax_membp(cb, 32);
  cb_emit(cb, "\x3D\xE8\x03\x00\x00", 5); // cmp eax, 1000
  cb_emit(cb, "\x0F\x83", 2); size_t jpad3 = cb->len; cb_emit4(cb, 0);
  emit_x86_push_imm32(cb, 0);
  emit_x86_mov_eax_abs(cb, text_rva, z0_rva, rel);
  emit_x86_push_reg(cb, 0);
  { int32_t d = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - (text_rva + (uint32_t)cb->len + 5)); emit_call_rel32(cb, d); }
  emit_x86_add_esp(cb, 8);
  size_t pad3_end = cb->len;
  cb_patch4(cb, jpad3, (uint32_t)((text_rva + (uint32_t)pad3_end) - (text_rva + (uint32_t)jpad3 + 4)));
  emit_x86_mov_eax_membp(cb, 32);
  cb_emit(cb, "\x3D\x10\x27\x00\x00", 5); // cmp eax, 10000
  cb_emit(cb, "\x0F\x83", 2); size_t jpad4 = cb->len; cb_emit4(cb, 0);
  emit_x86_push_imm32(cb, 0);
  emit_x86_mov_eax_abs(cb, text_rva, z0_rva, rel);
  emit_x86_push_reg(cb, 0);
  { int32_t d = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - (text_rva + (uint32_t)cb->len + 5)); emit_call_rel32(cb, d); }
  emit_x86_add_esp(cb, 8);
  size_t pad4_end = cb->len;
  cb_patch4(cb, jpad4, (uint32_t)((text_rva + (uint32_t)pad4_end) - (text_rva + (uint32_t)jpad4 + 4)));
  emit_x86_mov_eax_membp(cb, 32);
  cb_emit(cb, "\x3D\xA0\x86\x01\x00", 5); // cmp eax, 100000
  cb_emit(cb, "\x0F\x83", 2); size_t jpad5 = cb->len; cb_emit4(cb, 0);
  emit_x86_push_imm32(cb, 0);
  emit_x86_mov_eax_abs(cb, text_rva, z0_rva, rel);
  emit_x86_push_reg(cb, 0);
  { int32_t d = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - (text_rva + (uint32_t)cb->len + 5)); emit_call_rel32(cb, d); }
  emit_x86_add_esp(cb, 8);
  size_t pad5_end = cb->len;
  cb_patch4(cb, jpad5, (uint32_t)((text_rva + (uint32_t)pad5_end) - (text_rva + (uint32_t)jpad5 + 4)));
  // print frac
  emit_x86_mov_eax_membp(cb, 32);
  emit_x86_zero_edx(cb);
  emit_x86_push_reg(cb, 2);
  emit_x86_push_reg(cb, 0);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)say_inline_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_add_esp(cb, 8);
  emit_x86_mov_eax_imm32(cb, 0);
  emit_x86_zero_edx(cb);
  cb_emit1(cb, 0x5B); // pop ebx
  emit_x86_epilogue(cb, 128);

  // patch say/say_f/say_inline call sites now that offsets are known
  if(say_call_patch){
    uint32_t rip = text_rva + (uint32_t)say_call_patch + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)say_inline_off) - rip);
    cb_patch4(cb, say_call_patch, (uint32_t)disp);
  }
  if(say_nl_patch){
    uint32_t rip = text_rva + (uint32_t)say_nl_patch + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - rip);
    cb_patch4(cb, say_nl_patch, (uint32_t)disp);
  }
  if(sayf_call_patch){
    uint32_t rip = text_rva + (uint32_t)sayf_call_patch + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)sayf_inline_off) - rip);
    cb_patch4(cb, sayf_call_patch, (uint32_t)disp);
  }
  if(sayf_nl_patch){
    uint32_t rip = text_rva + (uint32_t)sayf_nl_patch + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - rip);
    cb_patch4(cb, sayf_nl_patch, (uint32_t)disp);
  }
  if(say_inline_str_patch){
    uint32_t rip = text_rva + (uint32_t)say_inline_str_patch + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - rip);
    cb_patch4(cb, say_inline_str_patch, (uint32_t)disp);
  }

  // len helper
  size_t len_off = cb->len;
  emit_x86_prologue(cb, 16);
  emit_x86_mov_eax_membp(cb, -(8));
  emit_x86_push_reg(cb, 0);
  emit_x86_call_moffs(cb, text_rva, imp->iat_lstrlen, rel);
  emit_x86_zero_edx(cb);
  emit_x86_epilogue(cb, 16);

  // strcmp helper
  size_t strcmp_off = cb->len;
  emit_x86_prologue(cb, 16);
  emit_x86_mov_eax_membp(cb, -(16));
  emit_x86_push_reg(cb, 0);
  emit_x86_mov_eax_membp(cb, -(8));
  emit_x86_push_reg(cb, 0);
  emit_x86_call_moffs(cb, text_rva, imp->iat_lstrcmp, rel);
  emit_x86_zero_edx(cb);
  emit_x86_epilogue(cb, 16);

  // tn_strcmp helper (alias of strcmp)
  size_t tnstrcmp_off = cb->len;
  emit_x86_prologue(cb, 16);
  emit_x86_mov_eax_membp(cb, -(16));
  emit_x86_push_reg(cb, 0);
  emit_x86_mov_eax_membp(cb, -(8));
  emit_x86_push_reg(cb, 0);
  emit_x86_call_moffs(cb, text_rva, imp->iat_lstrcmp, rel);
  emit_x86_zero_edx(cb);
  emit_x86_epilogue(cb, 16);

  // say_multi helper (vals:*i64, tags:*i64, argc:i64)
  *out_saymulti_off = cb->len;
  emit_x86_prologue(cb, 64);
  cb_emit(cb, "\x53\x56\x57", 3); // push ebx; push esi; push edi
  // esi = vals, edi = tags, ecx = argc, ebx = i
  emit_x86_mov_eax_membp(cb, -(8));
  cb_emit(cb, "\x89\xC6", 2); // mov esi, eax
  emit_x86_mov_eax_membp(cb, -(16));
  cb_emit(cb, "\x89\xC7", 2); // mov edi, eax
  emit_x86_mov_eax_membp(cb, -(24));
  cb_emit(cb, "\x89\xC1", 2); // mov ecx, eax
  emit_x86_mov_membp_eax(cb, 12); // save argc
  cb_emit(cb, "\x31\xDB", 2); // xor ebx, ebx
  size_t loop_pos = cb->len;
  emit_x86_mov_eax_membp(cb, 12);
  cb_emit(cb, "\x89\xC1", 2); // reload argc
  cb_emit(cb, "\x39\xD9", 2); // cmp ecx, ebx
  cb_emit(cb, "\x0F\x8E", 2); size_t jdone = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x89\xD8", 2); // mov eax, ebx
  cb_emit(cb, "\xC1\xE0\x03", 3); // shl eax, 3
  cb_emit(cb, "\x89\xC2", 2); // mov edx, eax (offset)
  cb_emit(cb, "\x01\xFA", 2); // add edx, edi (tags ptr)
  cb_emit(cb, "\x8B\x12", 2); // mov edx, [edx] tag
  emit_x86_mov_membp_edx(cb, 16); // save tag
  cb_emit(cb, "\x89\xD8", 2); // mov eax, ebx
  cb_emit(cb, "\xC1\xE0\x03", 3); // shl eax, 3
  cb_emit(cb, "\x01\xF0", 2); // add eax, esi (vals ptr)
  cb_emit(cb, "\x8B\x50\x04", 3); // mov edx, [eax+4] high
  cb_emit(cb, "\x8B\x00", 2); // mov eax, [eax] low
  emit_x86_mov_membp_eax(cb, 24); // low
  emit_x86_mov_membp_edx(cb, 20); // high
  emit_x86_mov_eax_membp(cb, 16); // tag
  cb_emit(cb, "\x83\xF8\x02", 3); // cmp eax, 2
  cb_emit(cb, "\x0F\x84", 2); size_t j_str = cb->len; cb_emit4(cb, 0); // je str
  cb_emit(cb, "\x83\xF8\x01", 3); // cmp eax, 1
  cb_emit(cb, "\x0F\x84", 2); size_t j_float = cb->len; cb_emit4(cb, 0); // je float
  // int
  emit_x86_mov_eax_membp(cb, 24);
  emit_x86_mov_edx_membp(cb, 20);
  emit_x86_push_reg(cb, 2);
  emit_x86_push_reg(cb, 0);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)say_inline_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_add_esp(cb, 8);
  cb_emit1(cb, 0xE9); size_t jafter_int = cb->len; cb_emit4(cb, 0);
  // float
  size_t float_pos = cb->len;
  emit_x86_mov_eax_membp(cb, 24);
  emit_x86_mov_edx_membp(cb, 20);
  emit_x86_push_reg(cb, 2);
  emit_x86_push_reg(cb, 0);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)sayf_inline_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_add_esp(cb, 8);
  cb_emit1(cb, 0xE9); size_t jafter_float = cb->len; cb_emit4(cb, 0);
  // str
  size_t str_pos = cb->len;
  emit_x86_mov_eax_membp(cb, 24);
  emit_x86_mov_edx_membp(cb, 20);
  emit_x86_push_reg(cb, 2);
  emit_x86_push_reg(cb, 0);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_add_esp(cb, 8);
  size_t after_call = cb->len;
  {
    uint32_t rip = text_rva + (uint32_t)jafter_int + 4;
    cb_patch4(cb, jafter_int, (uint32_t)((text_rva + (uint32_t)after_call) - rip));
    rip = text_rva + (uint32_t)jafter_float + 4;
    cb_patch4(cb, jafter_float, (uint32_t)((text_rva + (uint32_t)after_call) - rip));
    rip = text_rva + (uint32_t)j_str + 4;
    cb_patch4(cb, j_str, (uint32_t)((text_rva + (uint32_t)str_pos) - rip));
    rip = text_rva + (uint32_t)j_float + 4;
    cb_patch4(cb, j_float, (uint32_t)((text_rva + (uint32_t)float_pos) - rip));
  }
  // if i < argc-1 write space
  emit_x86_mov_eax_membp(cb, 12);
  cb_emit(cb, "\x89\xC1", 2); // reload argc
  cb_emit(cb, "\x89\xD8", 2); // mov eax, ebx
  cb_emit(cb, "\x83\xC0\x01", 3); // add eax, 1
  cb_emit(cb, "\x39\xC8", 2); // cmp eax, ecx
  cb_emit(cb, "\x0F\x8D", 2); size_t jnospace = cb->len; cb_emit4(cb, 0);
  emit_x86_push_imm32(cb, 0);
  emit_x86_mov_eax_abs(cb, text_rva, sp_rva, rel);
  emit_x86_push_reg(cb, 0);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_add_esp(cb, 8);
  size_t lspace_end = cb->len;
  cb_emit(cb, "\x83\xC3\x01", 3); // add ebx, 1
  cb_emit1(cb, 0xE9); size_t jloop = cb->len; cb_emit4(cb, 0);
  size_t ldone = cb->len;
  // trailing newline
  emit_x86_push_imm32(cb, 0);
  emit_x86_mov_eax_abs(cb, text_rva, nl_rva, rel);
  emit_x86_push_reg(cb, 0);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_x86_add_esp(cb, 8);
  emit_x86_mov_eax_imm32(cb, 0);
  emit_x86_zero_edx(cb);
  cb_emit(cb, "\x5F\x5E\x5B", 3); // pop edi; pop esi; pop ebx
  emit_x86_epilogue(cb, 64);
  // patch loop jumps
  {
    uint32_t rip = text_rva + (uint32_t)jdone + 4;
    cb_patch4(cb, jdone, (uint32_t)((text_rva + (uint32_t)ldone) - rip));
    rip = text_rva + (uint32_t)jnospace + 4;
    cb_patch4(cb, jnospace, (uint32_t)((text_rva + (uint32_t)lspace_end) - rip));
    rip = text_rva + (uint32_t)jloop + 4;
    cb_patch4(cb, jloop, (uint32_t)((text_rva + (uint32_t)loop_pos) - rip));
  }

  // memcpy/memmove/memcmp wrappers
  *out_memcpy_off = cb->len;
  emit_x86_prologue(cb, 16);
  cb_emit(cb, "\x56\x57\xFC", 3); // push esi; push edi; cld
  cb_emit(cb, "\x8B\x7D\x08", 3); // mov edi, [ebp+8]  dst low
  cb_emit(cb, "\x8B\x75\x10", 3); // mov esi, [ebp+16] src low
  cb_emit(cb, "\x8B\x4D\x18", 3); // mov ecx, [ebp+24] n low
  cb_emit(cb, "\xF3\xA4", 2);     // rep movsb
  cb_emit(cb, "\x8B\x45\x08", 3); // mov eax, [ebp+8]
  cb_emit(cb, "\x8B\x55\x0C", 3); // mov edx, [ebp+12]
  cb_emit(cb, "\x5F\x5E", 2);     // pop edi; pop esi
  emit_x86_epilogue(cb, 16);

  *out_memmove_off = cb->len;
  emit_x86_prologue(cb, 16);
  cb_emit(cb, "\x56\x57\xFC", 3); // push esi; push edi; cld
  cb_emit(cb, "\x8B\x7D\x08", 3); // mov edi, [ebp+8]  dst low
  cb_emit(cb, "\x8B\x75\x10", 3); // mov esi, [ebp+16] src low
  cb_emit(cb, "\x8B\x4D\x18", 3); // mov ecx, [ebp+24] n low
  cb_emit(cb, "\x85\xC9", 2);     // test ecx, ecx
  cb_emit(cb, "\x0F\x84", 2);     // je done
  size_t mm_done = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x39\xF7", 2);     // cmp edi, esi
  cb_emit(cb, "\x0F\x82", 2);     // jb forward
  size_t mm_fwd = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x0F\x84", 2);     // je done
  size_t mm_je_done = cb->len; cb_emit4(cb, 0);
  // backward copy
  cb_emit(cb, "\x01\xCE", 2);     // add esi, ecx
  cb_emit(cb, "\x01\xCF", 2);     // add edi, ecx
  cb_emit(cb, "\x4E\x4F", 2);     // dec esi; dec edi
  cb_emit(cb, "\xFD", 1);         // std
  cb_emit(cb, "\xF3\xA4", 2);     // rep movsb
  cb_emit(cb, "\xFC", 1);         // cld
  cb_emit(cb, "\xE9", 1);         // jmp done
  size_t mm_jmp_done = cb->len; cb_emit4(cb, 0);
  // forward copy
  size_t mm_fwd_pos = cb->len;
  cb_emit(cb, "\xF3\xA4", 2);     // rep movsb
  size_t mm_end = cb->len;
  cb_emit(cb, "\x8B\x45\x08", 3); // mov eax, [ebp+8]
  cb_emit(cb, "\x8B\x55\x0C", 3); // mov edx, [ebp+12]
  cb_emit(cb, "\x5F\x5E", 2);     // pop edi; pop esi
  emit_x86_epilogue(cb, 16);
  {
    uint32_t rip = text_rva + (uint32_t)mm_done + 4;
    cb_patch4(cb, mm_done, (uint32_t)((text_rva + (uint32_t)mm_end) - rip));
    rip = text_rva + (uint32_t)mm_fwd + 4;
    cb_patch4(cb, mm_fwd, (uint32_t)((text_rva + (uint32_t)mm_fwd_pos) - rip));
    rip = text_rva + (uint32_t)mm_je_done + 4;
    cb_patch4(cb, mm_je_done, (uint32_t)((text_rva + (uint32_t)mm_end) - rip));
    rip = text_rva + (uint32_t)mm_jmp_done + 4;
    cb_patch4(cb, mm_jmp_done, (uint32_t)((text_rva + (uint32_t)mm_end) - rip));
  }
  // NOTE: epilogue already emitted for memmove
  *out_memcmp_off = cb->len;
  emit_x86_prologue(cb, 16);
  cb_emit(cb, "\x56\x57", 2);     // push esi; push edi
  cb_emit(cb, "\x8B\x75\x08", 3); // mov esi, [ebp+8]  a low
  cb_emit(cb, "\x8B\x7D\x10", 3); // mov edi, [ebp+16] b low
  cb_emit(cb, "\x8B\x4D\x18", 3); // mov ecx, [ebp+24] n low
  cb_emit(cb, "\x85\xC9", 2);     // test ecx, ecx
  cb_emit(cb, "\x0F\x84", 2);     // je done_eq
  size_t mc_done_eq = cb->len; cb_emit4(cb, 0);
  size_t mc_loop = cb->len;
  cb_emit(cb, "\x8A\x06", 2);     // mov al, [esi]
  cb_emit(cb, "\x8A\x17", 2);     // mov dl, [edi]
  cb_emit(cb, "\x3A\xD0", 2);     // cmp al, dl
  cb_emit(cb, "\x0F\x85", 2);     // jne diff
  size_t mc_jne = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x46\x47", 2);     // inc esi; inc edi
  cb_emit(cb, "\x49", 1);         // dec ecx
  cb_emit(cb, "\x0F\x85", 2);     // jne loop
  size_t mc_jne_loop = cb->len; cb_emit4(cb, 0);
  size_t mc_eq = cb->len;
  cb_emit(cb, "\x31\xC0", 2);     // xor eax, eax
  cb_emit(cb, "\x99", 1);         // cdq
  cb_emit(cb, "\x5F\x5E", 2);     // pop edi; pop esi
  emit_x86_epilogue(cb, 16);
  size_t mc_diff = cb->len;
  cb_emit(cb, "\x0F\xB6\xC0", 3); // movzx eax, al
  cb_emit(cb, "\x0F\xB6\xD2", 3); // movzx edx, dl
  cb_emit(cb, "\x2B\xC2", 2);     // sub eax, edx
  cb_emit(cb, "\x99", 1);         // cdq
  cb_emit(cb, "\x5F\x5E", 2);     // pop edi; pop esi
  emit_x86_epilogue(cb, 16);
  {
    uint32_t rip = text_rva + (uint32_t)mc_done_eq + 4;
    cb_patch4(cb, mc_done_eq, (uint32_t)((text_rva + (uint32_t)mc_eq) - rip));
    rip = text_rva + (uint32_t)mc_jne + 4;
    cb_patch4(cb, mc_jne, (uint32_t)((text_rva + (uint32_t)mc_diff) - rip));
    rip = text_rva + (uint32_t)mc_jne_loop + 4;
    cb_patch4(cb, mc_jne_loop, (uint32_t)((text_rva + (uint32_t)mc_loop) - rip));
  }

  // Lower functions
  FuncPatch fpatches[2048];
  int fpatch_n = 0;
  LabelPatch lpatches[2048];
  for(int i=0;i<m->fn_n;i++){
    if(!reach[i]) continue;
    finfo[i].off = cb->len;
    int lpatch_n = 0;
    int ok = lower_func_x86(&m->fns[i], cb, text_rva, str_rva, str_n,
                            *out_say_off, *out_sayf_off, *out_saystr_off, *out_saymulti_off,
                            *out_memcpy_off, *out_memmove_off, *out_memcmp_off,
                            len_off, strcmp_off, tnstrcmp_off,
                            mul_off, div_off, mod_off, shl_off, shr_off,
                            out_time_off ? *out_time_off : 0,
                            out_time_ms_off ? *out_time_ms_off : 0,
                            out_time_ns_off ? *out_time_ns_off : 0,
                            out_malloc_off ? *out_malloc_off : 0,
                            out_free_off ? *out_free_off : 0,
                            finfo, m->fn_n, ginfo, gcount, imp, rel,
                            fpatches, &fpatch_n, 2048, lpatches, &lpatch_n, 2048);
    if(ok != 0){
      free(reach);
      free(stack);
      free(finfo);
      return -1;
    }
  }

  // Patch entry call to main
  {
    uint32_t rip = text_rva + (uint32_t)entry_call_pos + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)finfo[main_idx].off) - rip);
    cb_patch4(cb, entry_call_pos, (uint32_t)disp);
  }
  if(entry_init_pos && init_idx >= 0){
    uint32_t rip = text_rva + (uint32_t)entry_init_pos + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)finfo[init_idx].off) - rip);
    cb_patch4(cb, entry_init_pos, (uint32_t)disp);
  }

  // Patch calls to functions
  for(int i=0;i<fpatch_n;i++){
    int idx = fpatches[i].func_idx;
    uint32_t rip = text_rva + (uint32_t)fpatches[i].pos + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)finfo[idx].off) - rip);
    cb_patch4(cb, fpatches[i].pos, (uint32_t)disp);
  }

  free(reach);
  free(stack);
  free(finfo);
  return 0;
}

static int lower_module_x64(IRModule* m, CodeBuf* cb, uint32_t text_rva,
                            const uint32_t* str_rva, int str_n,
                            uint32_t fmt_rva, uint32_t fmtf_rva, uint32_t nl_rva, uint32_t sp_rva,
                            const PeImportInfo* imp,
                            GlobalInfo* ginfo, int gcount,
                            size_t* out_entry_off,
                            size_t* out_say_off, size_t* out_sayf_off, size_t* out_saystr_off, size_t* out_saymulti_off,
                            size_t* out_memcpy_off, size_t* out_memmove_off, size_t* out_memcmp_off,
                            size_t* out_time_off, size_t* out_time_ms_off, size_t* out_time_ns_off,
                            size_t* out_malloc_off, size_t* out_free_off){
  (void)fmt_rva;
  (void)fmtf_rva;
  if(!m || !cb || !imp) return -1;

  FuncInfo* finfo = (FuncInfo*)calloc((size_t)m->fn_n, sizeof(FuncInfo));
  if(!finfo) return -1;
  for(int i=0;i<m->fn_n;i++){
    finfo[i].name = m->fns[i].name;
    finfo[i].len = m->fns[i].len;
    finfo[i].is_extern = m->fns[i].is_extern;
    finfo[i].param_count = m->fns[i].param_count;
  }

  // Reachability: only lower functions reachable from main
  unsigned char* reach = (unsigned char*)calloc((size_t)m->fn_n, 1);
  int* stack = (int*)malloc(sizeof(int)*(size_t)(m->fn_n > 0 ? m->fn_n : 1));
  int sp = 0;
  int main_idx = -1;
  int init_idx = -1;
  for(int i=0;i<m->fn_n;i++){
    if(finfo[i].len==4 && strncmp(finfo[i].name, "main", 4)==0){
      main_idx = i;
      break;
    }
  }
  for(int i=0;i<m->fn_n;i++){
    if(finfo[i].is_extern) continue;
    if((finfo[i].len==9 && strncmp(finfo[i].name, "tezz_init", 9)==0) ||
       (finfo[i].len==11 && strncmp(finfo[i].name, "__tezz_init", 11)==0)){
      init_idx = i;
      break;
    }
  }
  if(main_idx < 0){
    fprintf(stderr, "buildexe: main not found in IR module (fn_n=%d)\n", m->fn_n);
    free(reach);
    free(stack);
    free(finfo);
    return -1;
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
      if(in->op == I_ADDRSYM && in->name){
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
      if(in->op == I_CALL && in->name){
        int tidx = call_target_index(finfo, m->fn_n, in->name, in->nlen);
        if(tidx >= 0 && !finfo[tidx].is_extern && !reach[tidx]){
          reach[tidx] = 1;
          stack[sp++] = tidx;
        }
      }
    }
  }

  size_t say_inline_off = 0;
  size_t saystr_inline_off = 0;
  size_t sayf_inline_off = 0;

  // Entry stub
  *out_entry_off = cb->len;
  cb_emit(cb, "\x48\x83\xEC\x28", 4);
  size_t entry_init_pos = 0;
  if(init_idx >= 0){
    cb_emit1(cb, 0xE8);
    entry_init_pos = cb->len;
    cb_emit4(cb, 0);
  }
  cb_emit1(cb, 0xE8);
  size_t entry_call_pos = cb->len;
  cb_emit4(cb, 0);
  cb_emit(cb, "\x89\xC1", 2);
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_exit);
  emit_call_reg(cb, 0);
  cb_emit1(cb, 0xF4);

  // time_now_ns helper (uses GetSystemTimePreciseAsFileTime)
  if(out_time_ns_off) *out_time_ns_off = cb->len;
  emit_prologue(cb, 32);
  emit_lea_reg_membp(cb, 1, 16); // rcx = &FILETIME (rbp-16)
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_gettime);
  emit_call_reg(cb, 0);
  emit_mov_reg_membp(cb, 0, 16); // rax = 64-bit FILETIME (100ns)
  emit_mov_reg_imm64(cb, 1, 116444736000000000ULL); // Unix epoch offset in FILETIME ticks
  emit_sub_rax_rcx(cb);
  emit_imul_rax_imm32(cb, 100);  // ns
  emit_epilogue(cb, 32);

  // time_now_ms helper
  if(out_time_ms_off) *out_time_ms_off = cb->len;
  emit_prologue(cb, 32);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)(out_time_ns_off ? *out_time_ns_off : 0)) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_mov_reg_imm64(cb, 1, 1000000); // rcx = 1e6
  emit_cqo(cb);
  emit_idiv_rcx(cb); // rax = ns / 1e6
  emit_epilogue(cb, 32);

  // time_now helper
  if(out_time_off) *out_time_off = cb->len;
  emit_prologue(cb, 32);
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)(out_time_ns_off ? *out_time_ns_off : 0)) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_mov_reg_imm64(cb, 1, 1000000000); // rcx = 1e9
  emit_cqo(cb);
  emit_idiv_rcx(cb); // rax = ns / 1e9
  emit_epilogue(cb, 32);

  // malloc helper (VirtualAlloc)
  if(out_malloc_off) *out_malloc_off = cb->len;
  emit_prologue(cb, 32);
  if(imp && (imp->runtime_flags & PE_IMPORT_RUNTIME_TN_ALLOC)){
    // Tezz native allocator ABI: malloc(size) passes size in RCX.
  } else {
    emit_mov_reg_reg(cb, 2, 1); // rdx = size (rcx)
    emit_mov_reg_imm64(cb, 1, 0); // rcx = NULL
    emit_mov_reg_imm64(cb, 8, 0x3000); // r8 = MEM_COMMIT|MEM_RESERVE
    emit_mov_reg_imm64(cb, 9, 0x04);   // r9 = PAGE_READWRITE
  }
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_valloc);
  emit_call_reg(cb, 0);
  emit_epilogue(cb, 32);

  // free helper (VirtualFree)
  if(out_free_off) *out_free_off = cb->len;
  emit_prologue(cb, 32);
  emit_mov_reg_imm64(cb, 2, 0);      // rdx = 0
  emit_mov_reg_imm64(cb, 8, 0x8000); // r8 = MEM_RELEASE
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_vfree);
  emit_call_reg(cb, 0);
  emit_mov_reg_imm64(cb, 0, 0);
  emit_epilogue(cb, 32);

  // say helper
  *out_say_off = cb->len;
  emit_prologue(cb, 128);
  // integer to string in buffer at [rbp-64..rbp-1]
  emit_mov_reg_reg(cb, 0, 1); // rax = rcx (value)
  cb_emit(cb, "\xC6\x45\xFF\x00", 4); // mov byte [rbp-1], 0
  emit_lea_reg_membp(cb, 10, 2); // r10 = rbp-2 (write ptr)
  cb_emit(cb, "\x45\x31\xDB", 3); // xor r11d, r11d
  cb_emit(cb, "\x48\x83\xF8\x00", 4); // cmp rax, 0
  cb_emit(cb, "\x0F\x8D", 2); // jge Lpos
  size_t jge_pos = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x48\xF7\xD8", 3); // neg rax
  cb_emit(cb, "\x41\xBB\x01\x00\x00\x00", 6); // mov r11d, 1
  size_t lpos = cb->len;
  cb_emit(cb, "\x48\x83\xF8\x00", 4); // cmp rax, 0
  cb_emit(cb, "\x0F\x85", 2); // jne Lloop
  size_t jne_loop = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x41\xC6\x02\x30", 4); // mov byte [r10], '0'
  cb_emit(cb, "\x49\x83\xEA\x01", 4); // sub r10, 1
  cb_emit(cb, "\xE9", 1); // jmp Ldone
  size_t jmp_done = cb->len; cb_emit4(cb, 0);
  size_t lloop = cb->len;
  cb_emit(cb, "\x31\xD2", 2); // xor edx, edx
  cb_emit(cb, "\x48\xC7\xC1\x0A\x00\x00\x00", 7); // mov rcx, 10
  cb_emit(cb, "\x48\xF7\xF1", 3); // div rcx
  cb_emit(cb, "\x80\xC2\x30", 3); // add dl, '0'
  cb_emit(cb, "\x41\x88\x12", 3); // mov [r10], dl
  cb_emit(cb, "\x49\x83\xEA\x01", 4); // sub r10, 1
  cb_emit(cb, "\x48\x85\xC0", 3); // test rax, rax
  cb_emit(cb, "\x0F\x85", 2); // jne Lloop
  size_t jne_loop2 = cb->len; cb_emit4(cb, 0);
  size_t ldone = cb->len;
  cb_emit(cb, "\x41\x83\xFB\x00", 4); // cmp r11d, 0
  cb_emit(cb, "\x0F\x84", 2); // je Lstart
  size_t je_start = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x41\xC6\x02\x2D", 4); // mov byte [r10], '-'
  cb_emit(cb, "\x49\x83\xEA\x01", 4); // sub r10, 1
  size_t lstart = cb->len;
  cb_emit(cb, "\x49\x83\xC2\x01", 4); // add r10, 1 (start)
  emit_mov_membp_reg(cb, 88, 10); // save start ptr
  emit_mov_reg_reg(cb, 1, 10); // rcx = start
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrlen);
  emit_call_reg(cb, 0);
  emit_mov_reg_reg(cb, 8, 0); // r8 = len
  emit_mov_reg_membp(cb, 2, 88); // rdx = start
  emit_mov_rcx_imm32(cb, -11); // STD_OUTPUT_HANDLE
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_getstd);
  emit_call_reg(cb, 0);
  emit_mov_membp_reg(cb, 80, 0); // save handle
  emit_mov_reg_membp(cb, 1, 80); // rcx = handle
  emit_lea_reg_membp(cb, 9, 72); // r9 = &written
  emit_mov_reg_imm64(cb, 0, 0); // rax = 0
  emit_store_rsp_disp_reg(cb, 32, 0); // 5th arg = NULL // 5th arg = NULL
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_write);
  emit_call_reg(cb, 0);
  emit_mov_reg_membp(cb, 1, 80); // rcx = handle
  emit_lea_reg_rip(cb, 2, text_rva, nl_rva);
  emit_mov_reg_imm64(cb, 8, 1);
  emit_lea_reg_membp(cb, 9, 72);
  emit_mov_reg_imm64(cb, 0, 0); // rax = 0
  emit_store_rsp_disp_reg(cb, 32, 0); // 5th arg = NULL
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_write);
  emit_call_reg(cb, 0);
  emit_mov_reg_imm64(cb, 0, 0);
  emit_epilogue(cb, 128);

  // patch jumps in int->string
  {
    uint32_t rip = text_rva + (uint32_t)jge_pos + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)lpos) - rip);
    cb_patch4(cb, jge_pos, (uint32_t)disp);
    rip = text_rva + (uint32_t)jne_loop + 4;
    disp = (int32_t)((text_rva + (uint32_t)lloop) - rip);
    cb_patch4(cb, jne_loop, (uint32_t)disp);
    rip = text_rva + (uint32_t)jmp_done + 4;
    disp = (int32_t)((text_rva + (uint32_t)ldone) - rip);
    cb_patch4(cb, jmp_done, (uint32_t)disp);
    rip = text_rva + (uint32_t)jne_loop2 + 4;
    disp = (int32_t)((text_rva + (uint32_t)lloop) - rip);
    cb_patch4(cb, jne_loop2, (uint32_t)disp);
    rip = text_rva + (uint32_t)je_start + 4;
    disp = (int32_t)((text_rva + (uint32_t)lstart) - rip);
    cb_patch4(cb, je_start, (uint32_t)disp);
  }

  // say_inline helper (no trailing newline)
  say_inline_off = cb->len;
  emit_prologue(cb, 128);
  // integer to string in buffer at [rbp-64..rbp-1]
  emit_mov_reg_reg(cb, 0, 1); // rax = rcx (value)
  cb_emit(cb, "\xC6\x45\xFF\x00", 4); // mov byte [rbp-1], 0
  emit_lea_reg_membp(cb, 10, 2); // r10 = rbp-2 (write ptr)
  cb_emit(cb, "\x45\x31\xDB", 3); // xor r11d, r11d
  cb_emit(cb, "\x48\x83\xF8\x00", 4); // cmp rax, 0
  cb_emit(cb, "\x0F\x8D", 2); // jge Lpos
  size_t ijge_pos = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x48\xF7\xD8", 3); // neg rax
  cb_emit(cb, "\x41\xBB\x01\x00\x00\x00", 6); // mov r11d, 1
  size_t ilpos = cb->len;
  cb_emit(cb, "\x48\x83\xF8\x00", 4); // cmp rax, 0
  cb_emit(cb, "\x0F\x85", 2); // jne Lloop
  size_t ijne_loop = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x41\xC6\x02\x30", 4); // mov byte [r10], '0'
  cb_emit(cb, "\x49\x83\xEA\x01", 4); // sub r10, 1
  cb_emit(cb, "\xE9", 1); // jmp Ldone
  size_t ijmp_done = cb->len; cb_emit4(cb, 0);
  size_t illoop = cb->len;
  cb_emit(cb, "\x31\xD2", 2); // xor edx, edx
  cb_emit(cb, "\x48\xC7\xC1\x0A\x00\x00\x00", 7); // mov rcx, 10
  cb_emit(cb, "\x48\xF7\xF1", 3); // div rcx
  cb_emit(cb, "\x80\xC2\x30", 3); // add dl, '0'
  cb_emit(cb, "\x41\x88\x12", 3); // mov [r10], dl
  cb_emit(cb, "\x49\x83\xEA\x01", 4); // sub r10, 1
  cb_emit(cb, "\x48\x85\xC0", 3); // test rax, rax
  cb_emit(cb, "\x0F\x85", 2); // jne Lloop
  size_t ijne_loop2 = cb->len; cb_emit4(cb, 0);
  size_t ildone = cb->len;
  cb_emit(cb, "\x41\x83\xFB\x00", 4); // cmp r11d, 0
  cb_emit(cb, "\x0F\x84", 2); // je Lstart
  size_t ije_start = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x41\xC6\x02\x2D", 4); // mov byte [r10], '-'
  cb_emit(cb, "\x49\x83\xEA\x01", 4); // sub r10, 1
  size_t ilstart = cb->len;
  cb_emit(cb, "\x49\x83\xC2\x01", 4); // add r10, 1 (start)
  emit_mov_membp_reg(cb, 88, 10); // save start ptr
  emit_mov_reg_reg(cb, 1, 10); // rcx = start
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrlen);
  emit_call_reg(cb, 0);
  emit_mov_membp_reg(cb, 96, 0); // save len
  emit_mov_rcx_imm32(cb, -11); // STD_OUTPUT_HANDLE
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_getstd);
  emit_call_reg(cb, 0);
  emit_mov_membp_reg(cb, 80, 0); // save handle
  emit_mov_reg_membp(cb, 1, 80); // rcx = handle
  emit_mov_reg_membp(cb, 2, 88); // rdx = start
  emit_mov_reg_membp(cb, 8, 96); // r8 = len
  emit_lea_reg_membp(cb, 9, 72); // r9 = &written
  emit_mov_reg_imm64(cb, 0, 0); // rax = 0
  emit_store_rsp_disp_reg(cb, 32, 0); // 5th arg = NULL // 5th arg = NULL
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_write);
  emit_call_reg(cb, 0);
  emit_mov_reg_imm64(cb, 0, 0);
  emit_epilogue(cb, 128);

  // patch jumps in int->string (inline)
  {
    uint32_t rip = text_rva + (uint32_t)ijge_pos + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)ilpos) - rip);
    cb_patch4(cb, ijge_pos, (uint32_t)disp);
    rip = text_rva + (uint32_t)ijne_loop + 4;
    disp = (int32_t)((text_rva + (uint32_t)illoop) - rip);
    cb_patch4(cb, ijne_loop, (uint32_t)disp);
    rip = text_rva + (uint32_t)ijmp_done + 4;
    disp = (int32_t)((text_rva + (uint32_t)ildone) - rip);
    cb_patch4(cb, ijmp_done, (uint32_t)disp);
    rip = text_rva + (uint32_t)ijne_loop2 + 4;
    disp = (int32_t)((text_rva + (uint32_t)illoop) - rip);
    cb_patch4(cb, ijne_loop2, (uint32_t)disp);
    rip = text_rva + (uint32_t)ije_start + 4;
    disp = (int32_t)((text_rva + (uint32_t)ilstart) - rip);
    cb_patch4(cb, ije_start, (uint32_t)disp);
  }

  // say_str helper
  *out_saystr_off = cb->len;
  emit_prologue(cb, 128);
  emit_mov_membp_reg(cb, 80, 1); // save s
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrlen);
  emit_call_reg(cb, 0);
  emit_mov_reg_reg(cb, 8, 0); // r8 = len
  emit_mov_rcx_imm32(cb, -11);
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_getstd);
  emit_call_reg(cb, 0);
  emit_mov_membp_reg(cb, 88, 0); // save handle
  emit_mov_reg_membp(cb, 1, 88); // rcx = handle
  emit_mov_reg_membp(cb, 2, 80); // rdx = s
  emit_lea_reg_membp(cb, 9, 72);
  emit_mov_reg_imm64(cb, 0, 0); // rax = 0
  emit_store_rsp_disp_reg(cb, 32, 0); // 5th arg = NULL
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_write);
  emit_call_reg(cb, 0);
  emit_mov_reg_membp(cb, 1, 88); // rcx = handle
  emit_lea_reg_rip(cb, 2, text_rva, nl_rva);
  emit_mov_reg_imm64(cb, 8, 1);
  emit_lea_reg_membp(cb, 9, 72);
  emit_mov_reg_imm64(cb, 0, 0); // rax = 0
  emit_store_rsp_disp_reg(cb, 32, 0); // 5th arg = NULL
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_write);
  emit_call_reg(cb, 0);
  emit_mov_reg_imm64(cb, 0, 0);
  emit_epilogue(cb, 128);

  // say_str_inline helper (no trailing newline)
  saystr_inline_off = cb->len;
  emit_prologue(cb, 128);
  emit_mov_membp_reg(cb, 80, 1); // save s
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_lstrlen);
  emit_call_reg(cb, 0);
  emit_mov_membp_reg(cb, 88, 0); // save len
  emit_mov_rcx_imm32(cb, -11);
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_getstd);
  emit_call_reg(cb, 0);
  emit_mov_reg_reg(cb, 1, 0); // rcx = handle
  emit_mov_reg_membp(cb, 2, 80); // rdx = s
  emit_mov_reg_membp(cb, 8, 88); // r8 = len
  emit_lea_reg_membp(cb, 9, 72);
  emit_mov_reg_imm64(cb, 0, 0); // rax = 0
  emit_store_rsp_disp_reg(cb, 32, 0); // 5th arg = NULL
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_write);
  emit_call_reg(cb, 0);
  emit_mov_reg_imm64(cb, 0, 0);
  emit_epilogue(cb, 128);

  // say_f_inline helper (no trailing newline)
  sayf_inline_off = cb->len;
  emit_prologue(cb, 192);
  emit_mov_membp_reg(cb, 152, 1); // save bits
  emit_movsd_xmm_membp(cb, 2, 152); // xmm2 = double bits
  emit_mov_reg_membp(cb, 8, 152); // r8 = double bits (varargs)
  emit_lea_reg_membp(cb, 1, 128); // rcx = buf
  emit_lea_reg_rip(cb, 2, text_rva, fmtf_rva); // rdx = fmt
  // shadow space for varargs
  emit_store_rsp_disp_reg(cb, 0, 1);  // rcx
  emit_store_rsp_disp_reg(cb, 8, 2);  // rdx
  emit_store_rsp_disp_reg(cb, 16, 8); // r8
  emit_movsd_rsp_xmm(cb, 16, 2);      // xmm2 -> shadow
  emit_mov_reg_rip(cb, 0, text_rva, imp->iat_sprintf);
  emit_call_reg(cb, 0);
  emit_lea_reg_membp(cb, 1, 128); // rcx = buf
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_mov_reg_imm64(cb, 0, 0);
  emit_epilogue(cb, 192);

  // say_multi helper (vals:*i64, tags:*i64, argc:i64) -> i64
  *out_saymulti_off = cb->len;
  {
    cb_emit(cb, "\x41\x54\x41\x56\x41\x57", 6); // push r12; push r14; push r15
    emit_prologue(cb, 40);
    emit_mov_reg_reg(cb, 12, 1); // r12 = rcx (vals)
    emit_mov_reg_reg(cb, 14, 2); // r14 = rdx (tags)
    emit_mov_reg_reg(cb, 15, 8); // r15 = r8  (argc)
    cb_emit(cb, "\x45\x31\xC9", 3); // xor r9d, r9d
    size_t sm_loop = cb->len;
    cb_emit(cb, "\x4D\x39\xF9", 3); // cmp r9, r15
    cb_emit(cb, "\x0F\x8D", 2);
    size_t sm_jge = cb->len; cb_emit4(cb, 0);
    cb_emit(cb, "\x49\x83\xF9\x00", 4); // cmp r9, 0
    cb_emit(cb, "\x0F\x84", 2);
    size_t sm_je_nospace = cb->len; cb_emit4(cb, 0);
    emit_lea_reg_rip(cb, 1, text_rva, sp_rva);
    emit_mov_membp_reg(cb, 8, 9); // save r9
    int32_t disp_sm = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp_sm);
    emit_mov_reg_membp(cb, 9, 8); // restore r9
    size_t sm_nospace = cb->len;
    cb_emit(cb, "\x4F\x8B\x14\xCE", 4); // mov r10, [r14 + r9*8] (tag)
    cb_emit(cb, "\x4F\x8B\x1C\xCC", 4); // mov r11, [r12 + r9*8] (val)
    cb_emit(cb, "\x49\x83\xFA\x02", 4); // cmp r10, 2
    cb_emit(cb, "\x0F\x84", 2);
    size_t sm_je_str = cb->len; cb_emit4(cb, 0);
    cb_emit(cb, "\x49\x83\xFA\x01", 4); // cmp r10, 1
    cb_emit(cb, "\x0F\x84", 2);
    size_t sm_je_flt = cb->len; cb_emit4(cb, 0);
    emit_mov_reg_reg(cb, 1, 11); // rcx = val
    emit_mov_membp_reg(cb, 8, 9); // save r9
    disp_sm = (int32_t)((text_rva + (uint32_t)say_inline_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp_sm);
    emit_mov_reg_membp(cb, 9, 8); // restore r9
    cb_emit1(cb, 0xE9);
    size_t sm_jmp_next = cb->len; cb_emit4(cb, 0);
    size_t sm_str = cb->len;
    emit_mov_reg_reg(cb, 1, 11); // rcx = val
    emit_mov_membp_reg(cb, 8, 9); // save r9
    disp_sm = (int32_t)((text_rva + (uint32_t)saystr_inline_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp_sm);
    emit_mov_reg_membp(cb, 9, 8); // restore r9
    cb_emit1(cb, 0xE9);
    size_t sm_jmp_next2 = cb->len; cb_emit4(cb, 0);
    size_t sm_flt = cb->len;
    emit_mov_reg_reg(cb, 1, 11); // rcx = val
    emit_mov_membp_reg(cb, 8, 9); // save r9
    disp_sm = (int32_t)((text_rva + (uint32_t)sayf_inline_off) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp_sm);
    emit_mov_reg_membp(cb, 9, 8); // restore r9
    size_t sm_next = cb->len;
    cb_emit(cb, "\x49\x83\xC1\x01", 4); // add r9, 1
    cb_emit1(cb, 0xE9);
    size_t sm_jmp_loop = cb->len; cb_emit4(cb, 0);
    size_t sm_done = cb->len;
    emit_mov_rcx_imm32(cb, -11); // STD_OUTPUT_HANDLE
    emit_mov_reg_rip(cb, 0, text_rva, imp->iat_getstd);
    emit_call_reg(cb, 0);
    emit_mov_reg_reg(cb, 1, 0); // rcx = handle
    emit_lea_reg_rip(cb, 2, text_rva, nl_rva);
    emit_mov_reg_imm64(cb, 8, 1);
    emit_lea_reg_membp(cb, 9, 16); // &written
    emit_mov_reg_imm64(cb, 0, 0); // rax = 0
  emit_store_rsp_disp_reg(cb, 32, 0); // 5th arg = NULL
    emit_mov_reg_rip(cb, 0, text_rva, imp->iat_write);
    emit_call_reg(cb, 0);
    emit_mov_reg_imm64(cb, 0, 0);
    cb_emit(cb, "\x48\x83\xC4\x28", 4); // add rsp, 40
    cb_emit1(cb, 0x5D); // pop rbp
    cb_emit(cb, "\x41\x5F\x41\x5E\x41\x5C", 6); // pop r15; pop r14; pop r12
    cb_emit1(cb, 0xC3); // ret
    {
      uint32_t rip = text_rva + (uint32_t)sm_jge + 4;
      int32_t d = (int32_t)((text_rva + (uint32_t)sm_done) - rip);
      cb_patch4(cb, sm_jge, (uint32_t)d);
      rip = text_rva + (uint32_t)sm_je_nospace + 4;
      d = (int32_t)((text_rva + (uint32_t)sm_nospace) - rip);
      cb_patch4(cb, sm_je_nospace, (uint32_t)d);
      rip = text_rva + (uint32_t)sm_je_str + 4;
      d = (int32_t)((text_rva + (uint32_t)sm_str) - rip);
      cb_patch4(cb, sm_je_str, (uint32_t)d);
      rip = text_rva + (uint32_t)sm_je_flt + 4;
      d = (int32_t)((text_rva + (uint32_t)sm_flt) - rip);
      cb_patch4(cb, sm_je_flt, (uint32_t)d);
      rip = text_rva + (uint32_t)sm_jmp_next + 4;
      d = (int32_t)((text_rva + (uint32_t)sm_next) - rip);
      cb_patch4(cb, sm_jmp_next, (uint32_t)d);
      rip = text_rva + (uint32_t)sm_jmp_next2 + 4;
      d = (int32_t)((text_rva + (uint32_t)sm_next) - rip);
      cb_patch4(cb, sm_jmp_next2, (uint32_t)d);
      rip = text_rva + (uint32_t)sm_jmp_loop + 4;
      d = (int32_t)((text_rva + (uint32_t)sm_loop) - rip);
      cb_patch4(cb, sm_jmp_loop, (uint32_t)d);
    }
  }

  // say_f helper (build via say_multi to avoid CRT)
  if(out_sayf_off) *out_sayf_off = cb->len;
  emit_prologue(cb, 64);
  // vals[0] = bits (rcx), tags[0] = 1 (float), argc = 1
  emit_mov_membp_reg(cb, 16, 1);   // [rbp-16] = rcx (bits)
  emit_mov_reg_imm64(cb, 0, 1);
  emit_mov_membp_reg(cb, 24, 0);   // [rbp-24] = 1 (tag float)
  emit_lea_reg_membp(cb, 1, 16);   // rcx = &vals
  emit_lea_reg_membp(cb, 2, 24);   // rdx = &tags
  emit_mov_reg_imm64(cb, 8, 1);    // r8 = argc
  {
    int32_t disp = (int32_t)((text_rva + (uint32_t)(*out_saymulti_off)) - (text_rva + (uint32_t)cb->len + 5));
    emit_call_rel32(cb, disp);
  }
  emit_mov_reg_imm64(cb, 0, 0);
  emit_epilogue(cb, 64);

  // memcpy helper (dst, src, n) -> dst
  if(out_memcpy_off) *out_memcpy_off = cb->len;
  emit_mov_reg_reg(cb, 0, 1); // rax = rcx (return dst)
  cb_emit(cb, "\x4D\x85\xC0", 3); // test r8, r8
  cb_emit(cb, "\x0F\x84", 2);
  size_t memcpy_done = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x45\x31\xC9", 3); // xor r9d, r9d
  size_t memcpy_loop = cb->len;
  cb_emit(cb, "\x46\x8A\x1C\x0A", 4); // mov r11b, [rdx + r9]
  cb_emit(cb, "\x46\x88\x1C\x09", 4); // mov [rcx + r9], r11b
  cb_emit(cb, "\x49\x83\xC1\x01", 4); // add r9, 1
  cb_emit(cb, "\x4D\x3B\xC8", 3); // cmp r9, r8
  cb_emit(cb, "\x0F\x82", 2);
  size_t memcpy_jb = cb->len; cb_emit4(cb, 0);
  size_t memcpy_end = cb->len;
  cb_emit1(cb, 0xC3); // ret
  {
    uint32_t rip = text_rva + (uint32_t)memcpy_done + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)memcpy_end) - rip);
    cb_patch4(cb, memcpy_done, (uint32_t)disp);
    rip = text_rva + (uint32_t)memcpy_jb + 4;
    disp = (int32_t)((text_rva + (uint32_t)memcpy_loop) - rip);
    cb_patch4(cb, memcpy_jb, (uint32_t)disp);
  }

  // memmove helper (dst, src, n) -> dst
  if(out_memmove_off) *out_memmove_off = cb->len;
  emit_mov_reg_reg(cb, 0, 1); // rax = rcx (return dst)
  cb_emit(cb, "\x4D\x85\xC0", 3); // test r8, r8
  cb_emit(cb, "\x0F\x84", 2);
  size_t mm_done = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x48\x39\xD1", 3); // cmp rcx, rdx
  cb_emit(cb, "\x0F\x82", 2); // jb forward
  size_t mm_jb_fwd = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x0F\x84", 2); // je done
  size_t mm_je_done = cb->len; cb_emit4(cb, 0);
  // backward copy
  emit_mov_reg_reg(cb, 9, 8); // r9 = r8
  cb_emit(cb, "\x49\x83\xE9\x01", 4); // sub r9, 1
  size_t mm_back = cb->len;
  cb_emit(cb, "\x46\x8A\x1C\x0A", 4); // mov r11b, [rdx + r9]
  cb_emit(cb, "\x46\x88\x1C\x09", 4); // mov [rcx + r9], r11b
  cb_emit(cb, "\x49\x83\xE9\x01", 4); // sub r9, 1
  cb_emit(cb, "\x49\x83\xF9\xFF", 4); // cmp r9, -1
  cb_emit(cb, "\x0F\x85", 2);
  size_t mm_jne_back = cb->len; cb_emit4(cb, 0);
  cb_emit1(cb, 0xC3); // ret
  // forward copy
  size_t mm_fwd = cb->len;
  cb_emit(cb, "\x45\x31\xC9", 3); // xor r9d, r9d
  size_t mm_fwd_loop = cb->len;
  cb_emit(cb, "\x46\x8A\x1C\x0A", 4); // mov r11b, [rdx + r9]
  cb_emit(cb, "\x46\x88\x1C\x09", 4); // mov [rcx + r9], r11b
  cb_emit(cb, "\x49\x83\xC1\x01", 4); // add r9, 1
  cb_emit(cb, "\x4D\x39\xC1", 3); // cmp r9, r8
  cb_emit(cb, "\x0F\x82", 2);
  size_t mm_jb_fwdloop = cb->len; cb_emit4(cb, 0);
  size_t mm_end = cb->len;
  cb_emit1(cb, 0xC3); // ret
  {
    uint32_t rip = text_rva + (uint32_t)mm_done + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)mm_end) - rip);
    cb_patch4(cb, mm_done, (uint32_t)disp);
    rip = text_rva + (uint32_t)mm_jb_fwd + 4;
    disp = (int32_t)((text_rva + (uint32_t)mm_fwd) - rip);
    cb_patch4(cb, mm_jb_fwd, (uint32_t)disp);
    rip = text_rva + (uint32_t)mm_je_done + 4;
    disp = (int32_t)((text_rva + (uint32_t)mm_end) - rip);
    cb_patch4(cb, mm_je_done, (uint32_t)disp);
    rip = text_rva + (uint32_t)mm_jne_back + 4;
    disp = (int32_t)((text_rva + (uint32_t)mm_back) - rip);
    cb_patch4(cb, mm_jne_back, (uint32_t)disp);
    rip = text_rva + (uint32_t)mm_jb_fwdloop + 4;
    disp = (int32_t)((text_rva + (uint32_t)mm_fwd_loop) - rip);
    cb_patch4(cb, mm_jb_fwdloop, (uint32_t)disp);
  }

  // memcmp helper (a, b, n) -> i64
  if(out_memcmp_off) *out_memcmp_off = cb->len;
  emit_mov_reg_imm64(cb, 0, 0); // rax = 0
  cb_emit(cb, "\x4D\x85\xC0", 3); // test r8, r8
  cb_emit(cb, "\x0F\x84", 2);
  size_t mc_done = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x45\x31\xC9", 3); // xor r9d, r9d
  size_t mc_loop = cb->len;
  cb_emit(cb, "\x46\x8A\x1C\x0A", 4); // mov r11b, [rdx + r9]
  cb_emit(cb, "\x46\x8A\x14\x09", 4); // mov r10b, [rcx + r9]
  cb_emit(cb, "\x45\x38\xDA", 3); // cmp r10b, r11b
  cb_emit(cb, "\x0F\x85", 2);
  size_t mc_jne = cb->len; cb_emit4(cb, 0);
  cb_emit(cb, "\x49\x83\xC1\x01", 4); // add r9, 1
  cb_emit(cb, "\x4D\x39\xC1", 3); // cmp r9, r8
  cb_emit(cb, "\x0F\x82", 2);
  size_t mc_jb = cb->len; cb_emit4(cb, 0);
  size_t mc_end = cb->len;
  cb_emit1(cb, 0xC3); // ret
  size_t mc_diff = cb->len;
  cb_emit(cb, "\x45\x0F\xB6\xD2", 4); // movzx r10d, r10b
  cb_emit(cb, "\x45\x0F\xB6\xDB", 4); // movzx r11d, r11b
  cb_emit(cb, "\x45\x2B\xD3", 3); // sub r10d, r11d
  cb_emit(cb, "\x49\x63\xC2", 3); // movsxd rax, r10d
  cb_emit1(cb, 0xC3); // ret
  {
    uint32_t rip = text_rva + (uint32_t)mc_done + 4;
    int32_t disp = (int32_t)((text_rva + (uint32_t)mc_end) - rip);
    cb_patch4(cb, mc_done, (uint32_t)disp);
    rip = text_rva + (uint32_t)mc_jne + 4;
    disp = (int32_t)((text_rva + (uint32_t)mc_diff) - rip);
    cb_patch4(cb, mc_jne, (uint32_t)disp);
    rip = text_rva + (uint32_t)mc_jb + 4;
    disp = (int32_t)((text_rva + (uint32_t)mc_loop) - rip);
    cb_patch4(cb, mc_jb, (uint32_t)disp);
  }

  // functions
  size_t main_off = 0;
  int main_found = 0;
  FuncPatch fpatches[512];
  LabelPatch lpatches[512];
  int fpatch_n = 0;
  int lpatch_n = 0;

  for(int i=0;i<m->fn_n;i++){
    if(finfo[i].is_extern) continue;
    if(reach && !reach[i]) continue;
    finfo[i].off = cb->len;
    if(finfo[i].len==4 && strncmp(finfo[i].name, "main", 4)==0){
      main_off = finfo[i].off;
      main_found = 1;
    }
    lpatch_n = 0;
    if(lower_func_x64(&m->fns[i], cb, text_rva, str_rva, str_n,
                      *out_say_off, (out_sayf_off ? *out_sayf_off : 0), *out_saystr_off,
                      (out_saymulti_off ? *out_saymulti_off : 0),
                      sayf_inline_off, saystr_inline_off, nl_rva,
                      (out_memcpy_off ? *out_memcpy_off : 0),
                      (out_memmove_off ? *out_memmove_off : 0),
                      (out_memcmp_off ? *out_memcmp_off : 0),
                      (out_time_off ? *out_time_off : 0),
                      (out_time_ms_off ? *out_time_ms_off : 0),
                      (out_time_ns_off ? *out_time_ns_off : 0),
                      (out_malloc_off ? *out_malloc_off : 0),
                      (out_free_off ? *out_free_off : 0),
                      finfo, m->fn_n,
                      ginfo, gcount,
                      imp,
                      fpatches, &fpatch_n, 512,
                      lpatches, &lpatch_n, 512) != 0){
      free(finfo);
      free(reach);
      free(stack);
      return -1;
    }
  }

  if(!main_found){
    fprintf(stderr, "buildexe: main not lowered\n");
    free(finfo);
    free(reach);
    free(stack);
    return -1;
  }
  uint32_t rip = text_rva + (uint32_t)entry_call_pos + 4;
  int32_t disp = (int32_t)((text_rva + (uint32_t)main_off) - rip);
  cb_patch4(cb, entry_call_pos, (uint32_t)disp);
  if(entry_init_pos && init_idx >= 0){
    uint32_t ripi = text_rva + (uint32_t)entry_init_pos + 4;
    int32_t dispi = (int32_t)((text_rva + (uint32_t)finfo[init_idx].off) - ripi);
    cb_patch4(cb, entry_init_pos, (uint32_t)dispi);
  }

  for(int i=0;i<fpatch_n;i++){
    int tidx = fpatches[i].func_idx;
    if(tidx < 0 || tidx >= m->fn_n){
      free(finfo);
      return -1;
    }
    uint32_t trva = text_rva + (uint32_t)finfo[tidx].off;
    uint32_t rip2 = text_rva + (uint32_t)fpatches[i].pos + 4;
    int32_t d2 = (int32_t)(trva - rip2);
    cb_patch4(cb, fpatches[i].pos, (uint32_t)d2);
  }

  free(finfo);
  free(reach);
  free(stack);
  return 0;
}

int ir_main_const(IRModule* m, long long* out){
  if(!m || !out) return 0;
  for(int i=0;i<m->fn_n;i++){
    IRFunc* f = &m->fns[i];
    if(f->len==4 && strncmp(f->name, "main", 4)==0){
      int nreg = f->max_reg > 0 ? f->max_reg : f->next_reg;
      if(nreg <= 0) nreg = 1;
      long long* vals = (long long*)malloc(sizeof(long long) * (size_t)nreg);
      unsigned char* known = (unsigned char*)malloc((size_t)nreg);
      if(!vals || !known){
        free(vals); free(known);
        return 0;
      }
      memset(known, 0, (size_t)nreg);
      for(int k=0;k<f->n;k++){
        IRIns* in = &f->ins[k];
        switch(in->op){
          case I_LABEL:
            break;
          case I_ICONST:
            if(in->a >= 0 && in->a < nreg){
              vals[in->a] = in->imm;
              known[in->a] = 1;
            }
            break;
          case I_BIN: {
            if(in->a >= 0 && in->a < nreg && in->b >= 0 && in->b < nreg && in->c >= 0 && in->c < nreg &&
               known[in->b] && known[in->c]){
              long long vb = vals[in->b];
              long long vc = vals[in->c];
              long long vr = 0;
              switch(in->binop){
                case B_ADD: vr = vb + vc; break;
                case B_SUB: vr = vb - vc; break;
                case B_MUL: vr = vb * vc; break;
                case B_DIV: if(vc == 0){ free(vals); free(known); return 0; } vr = vb / vc; break;
                case B_MOD: if(vc == 0){ free(vals); free(known); return 0; } vr = vb % vc; break;
                case B_AND: vr = vb & vc; break;
                case B_OR:  vr = vb | vc; break;
                case B_XOR: vr = vb ^ vc; break;
                case B_SHL: vr = vb << vc; break;
                case B_SHR: vr = vb >> vc; break;
                default: free(vals); free(known); return 0;
              }
              vals[in->a] = vr;
              known[in->a] = 1;
            } else {
              free(vals); free(known);
              return 0;
            }
            break;
          }
          case I_MOV:
            if(in->a >= 0 && in->a < nreg && in->b >= 0 && in->b < nreg && known[in->b]){
              vals[in->a] = vals[in->b];
              known[in->a] = 1;
            } else {
              free(vals); free(known);
              return 0;
            }
            break;
          case I_RET:
            if(in->a >= 0 && in->a < nreg && known[in->a]){
              *out = vals[in->a];
              free(vals); free(known);
              return 1;
            }
            free(vals); free(known);
            return 0;
          default:
            free(vals); free(known);
            return 0;
        }
      }
      free(vals); free(known);
      return 0;
    }
  }
  return 0;
}

int ir_x64_codegen(IRModule* m, uint32_t text_rva,
                   const uint32_t* str_rva, int str_n,
                   uint32_t fmt_rva, uint32_t fmtf_rva, uint32_t nl_rva, uint32_t sp_rva,
                   const PeImportInfo* imp,
                   GlobalInfo* ginfo, int gcount,
                   unsigned char** out_text, uint32_t* out_text_size,
                   size_t* out_entry_off){
  if(!out_text || !out_text_size) return -1;
  CodeBuf cb;
  cb_init(&cb, 4096);
  size_t entry_off = 0, say_off = 0, sayf_off = 0, saystr_off = 0, saymulti_off = 0;
  size_t memcpy_off = 0, memmove_off = 0, memcmp_off = 0;
  size_t time_off = 0, time_ms_off = 0, time_ns_off = 0, malloc_off = 0, free_off = 0;
  int ok = lower_module_x64(m, &cb, text_rva, str_rva, str_n, fmt_rva, fmtf_rva, nl_rva, sp_rva,
                            imp, ginfo, gcount, &entry_off,
                            &say_off, &sayf_off, &saystr_off, &saymulti_off,
                            &memcpy_off, &memmove_off, &memcmp_off,
                            &time_off, &time_ms_off, &time_ns_off, &malloc_off, &free_off);
  if(ok != 0){
    cb_free(&cb);
    return -1;
  }
  *out_text = cb.data;
  *out_text_size = (uint32_t)cb.len;
  if(out_entry_off) *out_entry_off = entry_off;
  return 0;
}

static int write_pe_stub64(FILE* f, const unsigned char* text, uint32_t text_size,
                           const unsigned char* rdata, uint32_t rdata_size,
                           const unsigned char* data, uint32_t data_size,
                           const unsigned char* reloc, uint32_t reloc_size,
                           uint32_t rdata_rva, uint32_t import_rva, uint32_t import_size,
                           uint16_t machine){
  const uint32_t file_align = 0x200;
  const uint32_t sect_align = 0x1000;
  const uint32_t text_rva = 0x1000;
  const uint32_t data_rva = data_size ? align_up_u32(rdata_rva + rdata_size, sect_align) : 0;
  const uint32_t reloc_rva = reloc_size ? align_up_u32((data_size ? data_rva + data_size : rdata_rva + rdata_size), sect_align) : 0;
  const uint32_t text_raw = align_up_u32(text_size, file_align);
  const uint32_t rdata_raw = align_up_u32(rdata_size, file_align);
  const uint32_t reloc_raw = align_up_u32(reloc_size, file_align);
  const uint32_t data_raw = align_up_u32(data_size, file_align);
  const uint32_t nsect = 2 + (data_size ? 1 : 0) + (reloc_size ? 1 : 0);
  const uint32_t headers_size = align_up_u32(0x80 + 4 + 20 + 0xF0 + (nsect * 40), file_align);
  uint32_t end_rva = text_rva + text_size;
  uint32_t r_end = rdata_rva + rdata_size;
  if(r_end > end_rva) end_rva = r_end;
  if(data_size){
    uint32_t d_end = data_rva + data_size;
    if(d_end > end_rva) end_rva = d_end;
  }
  if(reloc_size){
    uint32_t r_end2 = reloc_rva + reloc_size;
    if(r_end2 > end_rva) end_rva = r_end2;
  }
  const uint32_t size_image = align_up_u32(end_rva, sect_align);

  unsigned char dos[0x80];
  memset(dos, 0, sizeof(dos));
  dos[0] = 'M';
  dos[1] = 'Z';
  put_u32_le(dos + 0x3C, 0x80);
  fwrite(dos, 1, sizeof(dos), f);

  fwrite("PE\0\0", 1, 4, f);
  write_u16(f, machine);
  write_u16(f, (uint16_t)nsect);
  write_u32(f, 0);
  write_u32(f, 0);
  write_u32(f, 0);
  write_u16(f, 0xF0);
  write_u16(f, 0x0022);

  write_u16(f, 0x20B);
  fputc(14, f);
  fputc(0, f);
  write_u32(f, text_raw);
  write_u32(f, rdata_raw + (data_size ? data_raw : 0));
  write_u32(f, 0);
  write_u32(f, text_rva);
  write_u32(f, text_rva);
  write_u64(f, 0x140000000ULL);
  write_u32(f, sect_align);
  write_u32(f, file_align);
  write_u16(f, 6);
  write_u16(f, 0);
  write_u16(f, 0);
  write_u16(f, 0);
  write_u16(f, 6);
  write_u16(f, 0);
  write_u32(f, 0);
  write_u32(f, size_image);
  write_u32(f, headers_size);
  write_u32(f, 0);
  write_u16(f, 3);
  write_u16(f, 0x0100); // no ASLR
  write_u64(f, 0x800000);  // 8 MB stack reserve
  write_u64(f, 0x1000);
  write_u64(f, 0x800000);  // 8 MB stack commit
  write_u64(f, 0x1000);
  write_u32(f, 0);
  write_u32(f, 16);

  for(int i=0;i<16;i++){
    if(i == 1){
      write_u32(f, import_rva);
      write_u32(f, import_size);
    } else if(i == 5 && reloc_size){
      write_u32(f, reloc_rva);
      write_u32(f, reloc_size);
    } else {
      write_u32(f, 0);
      write_u32(f, 0);
    }
  }

  char name_text[8] = {'.','t','e','x','t',0,0,0};
  fwrite(name_text, 1, 8, f);
  write_u32(f, text_size);
  write_u32(f, text_rva);
  write_u32(f, text_raw);
  write_u32(f, headers_size);
  write_u32(f, 0);
  write_u32(f, 0);
  write_u16(f, 0);
  write_u16(f, 0);
  write_u32(f, 0x60000020);

  char name_rdata[8] = {'.','r','d','a','t','a',0,0};
  fwrite(name_rdata, 1, 8, f);
  write_u32(f, rdata_size);
  write_u32(f, rdata_rva);
  write_u32(f, rdata_raw);
  write_u32(f, headers_size + text_raw);
  write_u32(f, 0);
  write_u32(f, 0);
  write_u16(f, 0);
  write_u16(f, 0);
  write_u32(f, 0x40000040);

  if(data_size){
    char name_data[8] = {'.','d','a','t','a',0,0};
    fwrite(name_data, 1, 8, f);
    write_u32(f, data_size);
    write_u32(f, data_rva);
    write_u32(f, data_raw);
    write_u32(f, headers_size + text_raw + rdata_raw);
    write_u32(f, 0);
    write_u32(f, 0);
    write_u16(f, 0);
    write_u16(f, 0);
    write_u32(f, 0xC0000040);
  }

  if(reloc_size){
    char name_reloc[8] = {'.','r','e','l','o','c',0,0};
    fwrite(name_reloc, 1, 8, f);
    write_u32(f, reloc_size);
    write_u32(f, reloc_rva);
    write_u32(f, reloc_raw);
    write_u32(f, headers_size + text_raw + rdata_raw + (data_size ? data_raw : 0));
    write_u32(f, 0);
    write_u32(f, 0);
    write_u16(f, 0);
    write_u16(f, 0);
    write_u32(f, 0x42000040);
  }

  long cur = ftell(f);
  while(cur < (long)headers_size){
    fputc(0, f);
    cur++;
  }
  fwrite(text, 1, text_size, f);
  for(uint32_t i=text_size;i<text_raw;i++) fputc(0, f);
  fwrite(rdata, 1, rdata_size, f);
  for(uint32_t i=rdata_size;i<rdata_raw;i++) fputc(0, f);
  if(data_size){
    fwrite(data, 1, data_size, f);
    for(uint32_t i=data_size;i<data_raw;i++) fputc(0, f);
  }
  if(reloc_size){
    fwrite(reloc, 1, reloc_size, f);
    for(uint32_t i=reloc_size;i<reloc_raw;i++) fputc(0, f);
  }
  return 0;
}

static int write_pe_stub32(FILE* f, const unsigned char* text, uint32_t text_size,
                           const unsigned char* rdata, uint32_t rdata_size,
                           const unsigned char* data, uint32_t data_size,
                           const unsigned char* reloc, uint32_t reloc_size,
                           uint32_t rdata_rva, uint32_t import_rva, uint32_t import_size){
  const uint32_t file_align = 0x200;
  const uint32_t sect_align = 0x1000;
  const uint32_t text_rva = 0x1000;
  const uint32_t data_rva = data_size ? align_up_u32(rdata_rva + rdata_size, sect_align) : 0;
  const uint32_t reloc_rva = reloc_size ? align_up_u32((data_size ? data_rva + data_size : rdata_rva + rdata_size), sect_align) : 0;
  const uint32_t text_raw = align_up_u32(text_size, file_align);
  const uint32_t rdata_raw = align_up_u32(rdata_size, file_align);
  const uint32_t reloc_raw = align_up_u32(reloc_size, file_align);
  const uint32_t data_raw = align_up_u32(data_size, file_align);
  const uint32_t nsect = 2 + (data_size ? 1 : 0) + (reloc_size ? 1 : 0);
  const uint32_t headers_size = align_up_u32(0x80 + 4 + 20 + 0xE0 + (nsect * 40), file_align);
  uint32_t end_rva = text_rva + text_size;
  uint32_t r_end = rdata_rva + rdata_size;
  if(r_end > end_rva) end_rva = r_end;
  if(data_size){
    uint32_t d_end = data_rva + data_size;
    if(d_end > end_rva) end_rva = d_end;
  }
  if(reloc_size){
    uint32_t r_end2 = reloc_rva + reloc_size;
    if(r_end2 > end_rva) end_rva = r_end2;
  }
  const uint32_t size_image = align_up_u32(end_rva, sect_align);

  unsigned char dos[0x80];
  memset(dos, 0, sizeof(dos));
  dos[0] = 'M';
  dos[1] = 'Z';
  put_u32_le(dos + 0x3C, 0x80);
  fwrite(dos, 1, sizeof(dos), f);

  fwrite("PE\0\0", 1, 4, f);
  write_u16(f, 0x014C);
  write_u16(f, (uint16_t)nsect);
  write_u32(f, 0);
  write_u32(f, 0);
  write_u32(f, 0);
  write_u16(f, 0xE0);
  write_u16(f, 0x0102); // EXECUTABLE_IMAGE | 32BIT_MACHINE

  write_u16(f, 0x10B);
  fputc(14, f);
  fputc(0, f);
  write_u32(f, text_raw);
  write_u32(f, rdata_raw + (data_size ? data_raw : 0));
  write_u32(f, 0);
  write_u32(f, text_rva);
  write_u32(f, text_rva);
  write_u32(f, rdata_rva);
  write_u32(f, 0x400000);
  write_u32(f, sect_align);
  write_u32(f, file_align);
  write_u16(f, 6);
  write_u16(f, 0);
  write_u16(f, 0);
  write_u16(f, 0);
  write_u16(f, 6);
  write_u16(f, 0);
  write_u32(f, 0);
  write_u32(f, size_image);
  write_u32(f, headers_size);
  write_u32(f, 0);
  write_u16(f, 3);
  write_u16(f, 0); // no ASLR
  write_u32(f, 0x800000);  // 8 MB stack reserve
  write_u32(f, 0x1000);
  write_u32(f, 0x800000);  // 8 MB stack commit
  write_u32(f, 0x1000);
  write_u32(f, 0);
  write_u32(f, 16);

  for(int i=0;i<16;i++){
    if(i == 1){
      write_u32(f, import_rva);
      write_u32(f, import_size);
    } else if(i == 5 && reloc_size){
      write_u32(f, reloc_rva);
      write_u32(f, reloc_size);
    } else {
      write_u32(f, 0);
      write_u32(f, 0);
    }
  }

  char name_text[8] = {'.','t','e','x','t',0,0,0};
  fwrite(name_text, 1, 8, f);
  write_u32(f, text_size);
  write_u32(f, text_rva);
  write_u32(f, text_raw);
  write_u32(f, headers_size);
  write_u32(f, 0);
  write_u32(f, 0);
  write_u16(f, 0);
  write_u16(f, 0);
  write_u32(f, 0x60000020);

  char name_rdata[8] = {'.','r','d','a','t','a',0,0};
  fwrite(name_rdata, 1, 8, f);
  write_u32(f, rdata_size);
  write_u32(f, rdata_rva);
  write_u32(f, rdata_raw);
  write_u32(f, headers_size + text_raw);
  write_u32(f, 0);
  write_u32(f, 0);
  write_u16(f, 0);
  write_u16(f, 0);
  write_u32(f, 0x40000040);

  if(data_size){
    char name_data[8] = {'.','d','a','t','a',0,0};
    fwrite(name_data, 1, 8, f);
    write_u32(f, data_size);
    write_u32(f, data_rva);
    write_u32(f, data_raw);
    write_u32(f, headers_size + text_raw + rdata_raw);
    write_u32(f, 0);
    write_u32(f, 0);
    write_u16(f, 0);
    write_u16(f, 0);
    write_u32(f, 0xC0000040);
  }

  if(reloc_size){
    char name_reloc[8] = {'.','r','e','l','o','c',0,0};
    fwrite(name_reloc, 1, 8, f);
    write_u32(f, reloc_size);
    write_u32(f, reloc_rva);
    write_u32(f, reloc_raw);
    write_u32(f, headers_size + text_raw + rdata_raw + (data_size ? data_raw : 0));
    write_u32(f, 0);
    write_u32(f, 0);
    write_u16(f, 0);
    write_u16(f, 0);
    write_u32(f, 0x42000040);
  }

  long cur = ftell(f);
  while(cur < (long)headers_size){
    fputc(0, f);
    cur++;
  }
  fwrite(text, 1, text_size, f);
  for(uint32_t i=text_size;i<text_raw;i++) fputc(0, f);
  fwrite(rdata, 1, rdata_size, f);
  for(uint32_t i=rdata_size;i<rdata_raw;i++) fputc(0, f);
  if(data_size){
    fwrite(data, 1, data_size, f);
    for(uint32_t i=data_size;i<data_raw;i++) fputc(0, f);
  }
  if(reloc_size){
    fwrite(reloc, 1, reloc_size, f);
    for(uint32_t i=reloc_size;i<reloc_raw;i++) fputc(0, f);
  }
  return 0;
}

int ir_compile_to_pe_exe(IRModule* m, const char* out_exe, const char* target){
  (void)m;
  if(!out_exe) return -1;
#ifndef _WIN32
  fprintf(stderr, "buildexe: PE/COFF emitter is Windows-only\n");
  return -1;
#else
  if(!target) target = "x86_64";
  const uint32_t text_rva = 0x1000;
  uint32_t rdata_rva = 0x2000;
  unsigned char* rdata = (unsigned char*)calloc(1, 1024*1024);
  uint32_t rdata_size = 0;
  PeImportInfo imp;
  memset(&imp, 0, sizeof(imp));

  if(strcmp(target, "x86")==0){
    uint32_t rdata_rva_try = 0x2000;
    for(int pass=0; pass<2; pass++){
      memset(rdata, 0, 1024*1024);
      memset(&imp, 0, sizeof(imp));
      int base = build_pe_imports(rdata, rdata_rva_try, 4, &imp);
      if(base < 0) return -1;
      uint32_t off = (uint32_t)base;
      const char* fmt = "%d";
      const char* fmtf = "%d.%06u";
      const char* nl = "\n";
      const char* sp = " ";
      const char* dot = ".";
      const char* z0 = "0";
      const char* f0 = "0.000000";
      uint32_t fmt_rva = rdata_rva_try + off;
      memcpy(rdata + off, fmt, strlen(fmt) + 1);
      off += (uint32_t)strlen(fmt) + 1;
      uint32_t fmtf_rva = rdata_rva_try + off;
      memcpy(rdata + off, fmtf, strlen(fmtf) + 1);
      off += (uint32_t)strlen(fmtf) + 1;
      // double constant 1e6 for fixed 6-digit frac
      uint32_t onee6_rva = rdata_rva_try + off;
      {
        union { double d; unsigned char b[8]; } u;
        u.d = 1000000.0;
        memcpy(rdata + off, u.b, 8);
        off += 8;
      }
      uint32_t nl_rva = rdata_rva_try + off;
      memcpy(rdata + off, nl, strlen(nl) + 1);
      off += (uint32_t)strlen(nl) + 1;
      uint32_t sp_rva = rdata_rva_try + off;
      memcpy(rdata + off, sp, strlen(sp) + 1);
      off += (uint32_t)strlen(sp) + 1;
      uint32_t dot_rva = rdata_rva_try + off;
      memcpy(rdata + off, dot, strlen(dot) + 1);
      off += (uint32_t)strlen(dot) + 1;
      uint32_t z0_rva = rdata_rva_try + off;
      memcpy(rdata + off, z0, strlen(z0) + 1);
      off += (uint32_t)strlen(z0) + 1;
      uint32_t f0_rva = rdata_rva_try + off;
      memcpy(rdata + off, f0, strlen(f0) + 1);
      off += (uint32_t)strlen(f0) + 1;
      uint32_t* str_rva = NULL;
      if(m && m->str_n > 0){
        str_rva = (uint32_t*)calloc((size_t)m->str_n, sizeof(uint32_t));
        for(int i=0;i<m->str_n;i++){
          str_rva[i] = rdata_rva_try + off;
          memcpy(rdata + off, m->strs[i].bytes, (size_t)m->strs[i].len + 1);
          off += (uint32_t)m->strs[i].len + 1;
        }
      }
      rdata_size = off;

      uint32_t data_rva = align_up_u32(rdata_rva_try + rdata_size, 0x1000);
      unsigned char* data = NULL;
      uint32_t data_size = 0;
      GlobalInfo* ginfo = NULL;
      int gcount = (m ? m->global_n : 0);
      if(gcount > 0){
        ginfo = (GlobalInfo*)calloc((size_t)gcount, sizeof(GlobalInfo));
        size_t cap = 256;
        data = (unsigned char*)malloc(cap);
        if(!ginfo || !data){ free(ginfo); free(data); if(str_rva) free(str_rva); return -1; }
        memset(data, 0, cap);
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
          uint32_t need = doff + (uint32_t)g->size;
          if(need > cap){
            size_t ncap = cap;
            while(ncap < need) ncap *= 2;
            data = (unsigned char*)realloc(data, ncap);
            memset(data + cap, 0, ncap - cap);
            cap = ncap;
          }
          ginfo[i].rva = data_rva + doff;
          if(g->has_init){
            if(g->ty && g->ty->k == TY_F64){
              uint64_t bits = (uint64_t)g->init_fbits;
              for(int b=0;b<g->size && b<8;b++){
                data[doff + (uint32_t)b] = (unsigned char)((bits >> (8*b)) & 0xFF);
              }
            } else if(g->size == 8){
              put_u64_le(data + doff, (uint64_t)g->init_int);
            } else if(g->size == 4){
              put_u32_le(data + doff, (uint32_t)g->init_int);
            } else if(g->size == 1){
              data[doff] = (unsigned char)(g->init_int & 0xFF);
            }
          }
          doff += (uint32_t)g->size;
        }
        data_size = doff;
      }

      CodeBuf cb;
      cb_init(&cb, 4096);
      RelocList rel;
      reloc_init(&rel);
      size_t entry_off = 0, say_off = 0, sayf_off = 0, saystr_off = 0, saymulti_off = 0;
      size_t memcpy_off = 0, memmove_off = 0, memcmp_off = 0;
      size_t time_off = 0, time_ms_off = 0, time_ns_off = 0, malloc_off = 0, free_off = 0;
      int ok = lower_module_x86(m, &cb, text_rva, str_rva, (m ? m->str_n : 0),
                                fmt_rva, fmtf_rva, nl_rva, sp_rva, dot_rva, z0_rva, f0_rva, onee6_rva,
                                &imp, ginfo, gcount, &rel,
                                &entry_off,
                                &say_off, &sayf_off, &saystr_off, &saymulti_off,
                                &memcpy_off, &memmove_off, &memcmp_off,
                                &time_off, &time_ms_off, &time_ns_off, &malloc_off, &free_off);
      if(str_rva) free(str_rva);
      if(ok != 0){
        cb_free(&cb);
        reloc_free(&rel);
        free(ginfo);
        free(data);
        return -1;
      }

      uint32_t desired_rva = align_up_u32(text_rva + (uint32_t)cb.len, 0x1000);
      if(desired_rva != rdata_rva_try && pass == 0){
        cb_free(&cb);
        reloc_free(&rel);
        free(ginfo);
        free(data);
        rdata_rva_try = desired_rva;
        continue;
      }

      unsigned char* reloc = NULL;
      uint32_t reloc_size = 0;
      if(build_reloc_section(&rel, &reloc, &reloc_size) != 0){
        cb_free(&cb);
        reloc_free(&rel);
        free(ginfo);
        free(data);
        return -1;
      }

      FILE* f = fopen(out_exe, "wb");
      if(!f){
        cb_free(&cb);
        reloc_free(&rel);
        free(reloc);
        free(ginfo);
        free(data);
        return -1;
      }
      int r = write_pe_stub32(f, cb.data, (uint32_t)cb.len, rdata, rdata_size,
                              data, data_size, reloc, reloc_size, rdata_rva_try, rdata_rva_try, imp.import_size);
      fclose(f);
      cb_free(&cb);
      reloc_free(&rel);
      free(reloc);
      free(ginfo);
      free(data);
      return r;
    }
  }

  if(strcmp(target, "arm64")==0){
    memset(rdata, 0, 1024*1024);
    memset(&imp, 0, sizeof(imp));
    int base = build_pe_imports(rdata, rdata_rva, 8, &imp);
    if(base < 0) return -1;
    uint32_t off = (uint32_t)base;
    uint32_t* str_rva = NULL;
    if(m && m->str_n > 0){
      str_rva = (uint32_t*)calloc((size_t)m->str_n, sizeof(uint32_t));
      for(int i=0;i<m->str_n;i++){
        str_rva[i] = rdata_rva + off;
        if (m->strs[i].bytes) {
          memcpy(rdata + off, m->strs[i].bytes, (size_t)m->strs[i].len);
        }
        rdata[off + m->strs[i].len] = '\0';
        off += (uint32_t)m->strs[i].len + 1;
      }
    }
    rdata_size = off;

    uint32_t data_rva = align_up_u32(rdata_rva + rdata_size, 0x1000);
    unsigned char* data = NULL;
    uint32_t data_size = 0;
    GlobalInfo* ginfo = NULL;
    int gcount = (m ? m->global_n : 0);
    if(gcount > 0){
      ginfo = (GlobalInfo*)calloc((size_t)gcount, sizeof(GlobalInfo));
      size_t cap = 256;
      data = (unsigned char*)malloc(cap);
      if(!ginfo || !data){ free(ginfo); free(data); if(str_rva) free(str_rva); return -1; }
      memset(data, 0, cap);
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
        uint32_t need = doff + (uint32_t)g->size;
        if(need > cap){
          size_t ncap = cap;
          while(ncap < need) ncap *= 2;
          data = (unsigned char*)realloc(data, ncap);
          if(!data){ free(ginfo); if(str_rva) free(str_rva); return -1; }
          memset(data + cap, 0, ncap - cap);
          cap = ncap;
        }
        ginfo[i].rva = data_rva + doff;
        if(g->has_init){
          if(g->ty && g->ty->k == TY_F64){
            uint64_t bits = (uint64_t)g->init_fbits;
            for(int b=0;b<g->size && b<8;b++){
              data[doff + (uint32_t)b] = (unsigned char)((bits >> (8*b)) & 0xFF);
            }
          } else if(g->size == 8){
            put_u64_le(data + doff, (uint64_t)g->init_int);
          } else if(g->size == 4){
            put_u32_le(data + doff, (uint32_t)g->init_int);
          } else if(g->size == 1){
            data[doff] = (unsigned char)(g->init_int & 0xFF);
          }
        }
        doff += (uint32_t)g->size;
      }
      data_size = doff;
    }
    unsigned char* text = NULL;
    uint32_t text_size = 0;
    size_t main_off = 0;
    uint64_t base_addr = 0x140000000ULL;
    int ok = ir_arm64_codegen_text_win(m, base_addr, &imp, str_rva, (m ? m->str_n : 0),
                                       ginfo, gcount, &text, &text_size, &main_off);
    if(str_rva) free(str_rva);
    if(ok != 0){
      free(ginfo);
      free(data);
      fprintf(stderr, "buildexe: arm64 lowering failed; refusing to emit stub executable\n");
      return -1;
    }

    // append entry stub: call main, then ExitProcess
    uint32_t entry_off = text_size;
    uint32_t stub_size = 28;
    unsigned char* new_text = (unsigned char*)realloc(text, text_size + stub_size);
    if(!new_text){ free(text); return -1; }
    text = new_text;
    uint32_t instr0 = 0x94000000; // bl imm26
    int64_t disp = ((int64_t)main_off - (int64_t)entry_off) / 4;
    if(disp < -(1<<25) || disp > ((1<<25)-1)){
      free(text);
      return -1;
    }
    instr0 |= ((uint32_t)disp & 0x03FFFFFFu);
    uint32_t instr1 = 0x58000010; // ldr x16, [pc, #imm]
    uint32_t instr2 = 0xF9400210; // ldr x16, [x16]
    uint32_t instr3 = 0xD63F0200; // blr x16
    uint32_t instr4 = 0xD4200000; // brk #0
    uint64_t lit_addr = 0x140000000ULL + (uint64_t)imp.iat_exit;
    uint32_t ldr_off = 4;
    uint32_t lit_off = 20;
    int32_t off_disp = (int32_t)((lit_off - ldr_off) / 4);
    instr1 |= ((uint32_t)off_disp & 0x7FFFF) << 5;
    memcpy(text + entry_off + 0, &instr0, 4);
    memcpy(text + entry_off + 4, &instr1, 4);
    memcpy(text + entry_off + 8, &instr2, 4);
    memcpy(text + entry_off + 12, &instr3, 4);
    memcpy(text + entry_off + 16, &instr4, 4);
    memcpy(text + entry_off + 20, &lit_addr, 8);
    text_size += stub_size;

    unsigned char reloc[16];
    memset(reloc, 0, sizeof(reloc));
    uint32_t page_rva = text_rva + (entry_off & 0xFFFFF000u);
    uint16_t entry = (uint16_t)((10u << 12) | ((entry_off + 20) & 0xFFFu)); // DIR64
    put_u32_le(reloc + 0, page_rva);
    put_u32_le(reloc + 4, 12);
    reloc[8] = (unsigned char)(entry & 0xFF);
    reloc[9] = (unsigned char)((entry >> 8) & 0xFF);
    FILE* f = fopen(out_exe, "wb");
    if(!f){ free(text); free(ginfo); free(data); return -1; }
    int r = write_pe_stub64(f, text, text_size, rdata, rdata_size, data, data_size, reloc, 12, rdata_rva, rdata_rva, imp.import_size, 0xAA64);
    fclose(f);
    free(text);
    free(ginfo);
    free(data);
    return r;
  }

  // default x86_64
  for(int pass=0; pass<2; pass++){
    memset(rdata, 0, 1024*1024);
    memset(&imp, 0, sizeof(imp));
    int base = build_pe_imports(rdata, rdata_rva, 8, &imp);
    if(base < 0) return -1;
    uint32_t off = (uint32_t)base;
    const char* fmt = "%lld";
    const char* fmtf = "%.6f";
    const char* nl = "\n";
    const char* sp = " ";
    uint32_t fmt_rva = rdata_rva + off;
    memcpy(rdata + off, fmt, strlen(fmt) + 1);
    off += (uint32_t)strlen(fmt) + 1;
    uint32_t fmtf_rva = rdata_rva + off;
    memcpy(rdata + off, fmtf, strlen(fmtf) + 1);
    off += (uint32_t)strlen(fmtf) + 1;
    uint32_t nl_rva = rdata_rva + off;
    memcpy(rdata + off, nl, strlen(nl) + 1);
    off += (uint32_t)strlen(nl) + 1;
    uint32_t sp_rva = rdata_rva + off;
    memcpy(rdata + off, sp, strlen(sp) + 1);
    off += (uint32_t)strlen(sp) + 1;
    uint32_t* str_rva = NULL;
    if(m && m->str_n > 0){
      str_rva = (uint32_t*)calloc((size_t)m->str_n, sizeof(uint32_t));
      for(int i=0;i<m->str_n;i++){
        str_rva[i] = rdata_rva + off;
        if (m->strs[i].bytes) {
          memcpy(rdata + off, m->strs[i].bytes, (size_t)m->strs[i].len);
        }
        rdata[off + m->strs[i].len] = '\0';
        off += (uint32_t)m->strs[i].len + 1;
      }
    }
    rdata_size = off;

    uint32_t data_rva = align_up_u32(rdata_rva + rdata_size, 0x1000);
    unsigned char* data = NULL;
    uint32_t data_size = 0;
    GlobalInfo* ginfo = NULL;
    int gcount = (m ? m->global_n : 0);
    if(gcount > 0){
      ginfo = (GlobalInfo*)calloc((size_t)gcount, sizeof(GlobalInfo));
      size_t cap = 256;
      data = (unsigned char*)malloc(cap);
      if(!ginfo || !data){ free(ginfo); free(data); if(str_rva) free(str_rva); return -1; }
      memset(data, 0, cap);
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
        uint32_t need = doff + (uint32_t)g->size;
        if(need > cap){
          size_t ncap = cap;
          while(ncap < need) ncap *= 2;
          data = (unsigned char*)realloc(data, ncap);
          if(!data){ free(ginfo); if(str_rva) free(str_rva); return -1; }
          memset(data + cap, 0, ncap - cap);
          cap = ncap;
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
        doff = need;
      }
      data_size = doff;
    }

    CodeBuf cb;
    cb_init(&cb, 4096);
    size_t entry_off = 0, say_off = 0, sayf_off = 0, saystr_off = 0, saymulti_off = 0, memcpy_off = 0, memmove_off = 0, memcmp_off = 0;
    size_t time_off = 0, time_ms_off = 0, time_ns_off = 0, malloc_off = 0, free_off = 0;
    int ok = lower_module_x64(m, &cb, text_rva, str_rva, m ? m->str_n : 0, fmt_rva, fmtf_rva, nl_rva, sp_rva,
                              &imp, ginfo, gcount, &entry_off, &say_off, &sayf_off, &saystr_off, &saymulti_off,
                              &memcpy_off, &memmove_off, &memcmp_off,
                              &time_off, &time_ms_off, &time_ns_off,
                              &malloc_off, &free_off);
    if(str_rva) free(str_rva);
    unsigned char* text = cb.data;
    uint32_t text_size = (uint32_t)cb.len;
    if(ok != 0){
      cb_free(&cb);
      free(ginfo);
      free(data);
      fprintf(stderr, "buildexe: x64 lowering failed; refusing to emit stub executable\n");
      return -1;
    }

    uint32_t need_rdata = align_up_u32(text_rva + text_size, 0x1000);
    if(need_rdata > rdata_rva && pass == 0){
      cb_free(&cb);
      free(ginfo);
      free(data);
      rdata_rva = need_rdata;
      continue;
    }

    FILE* f = fopen(out_exe, "wb");
    if(!f) return -1;
    int r = write_pe_stub64(f, text, text_size, rdata, rdata_size, data, data_size, NULL, 0, rdata_rva, rdata_rva, imp.import_size, 0x8664);
    fclose(f);
    cb_free(&cb);
    free(ginfo);
    free(data);
    return r;
  }
  return -1;
#endif
}

