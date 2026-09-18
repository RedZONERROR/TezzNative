#ifndef TN_GUARD_ALLOC_H
#define TN_GUARD_ALLOC_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif
#endif

#define TN_GUARD_MAGIC 0x544E474152444D47ULL

typedef struct tn_guard_hdr {
  uint64_t magic;
  void* base;
  size_t total;
  size_t user;
} tn_guard_hdr;

static size_t tn_guard_align_up(size_t v, size_t a){
  return (v + (a - 1)) & ~(a - 1);
}

static size_t tn_guard_page_size(void){
  static size_t ps = 0;
  if(ps) return ps;
#ifdef _WIN32
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  ps = (size_t)si.dwPageSize;
#else
  long v = sysconf(_SC_PAGESIZE);
  if(v <= 0) v = 4096;
  ps = (size_t)v;
#endif
  return ps;
}

static size_t tn_guard_header_size(void){
  return tn_guard_align_up(sizeof(tn_guard_hdr), 16);
}

static void* tn_guard_malloc(size_t n){
  if(n == 0) n = 1;
  size_t page = tn_guard_page_size();
  size_t hsz = tn_guard_header_size();
  size_t payload = hsz + n;
  size_t access = tn_guard_align_up(payload, page);
  size_t total = access + page * 2;
#ifdef _WIN32
  unsigned char* base = (unsigned char*)VirtualAlloc(NULL, total, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if(!base) return NULL;
  DWORD old = 0;
  VirtualProtect(base, page, PAGE_NOACCESS, &old);
  VirtualProtect(base + page + access, page, PAGE_NOACCESS, &old);
#else
  unsigned char* base = (unsigned char*)mmap(NULL, total, PROT_READ | PROT_WRITE,
                                             MAP_PRIVATE | MAP_ANON, -1, 0);
  if(base == (unsigned char*)MAP_FAILED) return NULL;
  mprotect(base, page, PROT_NONE);
  mprotect(base + page + access, page, PROT_NONE);
#endif
  unsigned char* hdrp = base + page;
  tn_guard_hdr* hdr = (tn_guard_hdr*)hdrp;
  hdr->magic = TN_GUARD_MAGIC;
  hdr->base = base;
  hdr->total = total;
  hdr->user = n;
  return hdrp + hsz;
}

static void tn_guard_free(void* p){
  if(!p) return;
  size_t hsz = tn_guard_header_size();
  tn_guard_hdr* hdr = (tn_guard_hdr*)((unsigned char*)p - hsz);
  if(hdr->magic != TN_GUARD_MAGIC){
    fprintf(stderr, "tn_guard_free: invalid pointer %p\n", p);
    abort();
  }
#ifdef _WIN32
  VirtualFree(hdr->base, 0, MEM_RELEASE);
#else
  munmap(hdr->base, hdr->total);
#endif
}

static void* tn_guard_realloc(void* p, size_t n){
  if(!p) return tn_guard_malloc(n);
  if(n == 0){
    tn_guard_free(p);
    return NULL;
  }
  size_t hsz = tn_guard_header_size();
  tn_guard_hdr* hdr = (tn_guard_hdr*)((unsigned char*)p - hsz);
  if(hdr->magic != TN_GUARD_MAGIC){
    fprintf(stderr, "tn_guard_realloc: invalid pointer %p\n", p);
    abort();
  }
  size_t old = hdr->user;
  void* np = tn_guard_malloc(n);
  if(!np) return NULL;
  size_t cp = old < n ? old : n;
  memcpy(np, p, cp);
  tn_guard_free(p);
  return np;
}

#endif
