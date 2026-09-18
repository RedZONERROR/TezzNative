// src/bc_vm.c - bytecode VM (threaded dispatch)
#include "bc_vm.h"
#include "ir.h"
#include "vm.h"
#include "sema.h"
#include "util.h"
#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

static void* xmalloc(size_t n){ void* p=malloc(n); if(!p) die("out of memory"); return p; }
static void* xrealloc(void* p, size_t n){ void* q=realloc(p,n); if(!q) die("out of memory"); return q; }
static int align_up_i(int v, int align){
  if(align <= 1) return v;
  int r = v % align;
  return r ? (v + (align - r)) : v;
}

static int g_vm_async_zero = 0;
void vm_set_async_zero(int on){ g_vm_async_zero = on ? 1 : 0; }

static char* dup_cstr(const char* s){
  if(!s) s="";
  size_t n=strlen(s);
  char* out=(char*)xmalloc(n+1);
  memcpy(out,s,n+1);
  return out;
}

static const char* dirname_of(const char* path){
  const char* last=path;
  for(const char* p=path; *p; p++){
    if(*p=='/'||*p=='\\') last=p;
  }
  if(last==path) return ".";
  int n=(int)(last-path);
  char* out=(char*)xmalloc((size_t)n+1);
  memcpy(out,path,(size_t)n);
  out[n]=0;
  return out;
}

static void scan_imports(const char* src, const char* path, const char*** out_list, int* out_n){
  Lexer L;
  lex_init(&L, src, path);
  const char** items = NULL;
  int n = 0;
  for(;;){
    if(L.cur.kind == TK_IMPORT){
      lex_next(&L);
      if(L.cur.kind == TK_STRING){
        const char* ts = L.cur.str ? L.cur.str : "";
        char* s = dup_cstr(ts);
        items = (const char**)xrealloc((void*)items, sizeof(char*)*(size_t)(n+1));
        items[n++] = s;
      }
    }
    if(L.cur.kind == TK_EOF) break;
    lex_next(&L);
  }
  *out_list = items;
  *out_n = n;
}

static int ci_has_substr(const char* s, const char* needle){
  if(!s || !needle || !*needle) return 0;
  size_t nlen = strlen(needle);
  for(const char* p=s; *p; p++){
    size_t i=0;
    while(i<nlen){
      char a = p[i];
      char b = needle[i];
      if(a>='A' && a<='Z') a = (char)(a - 'A' + 'a');
      if(b>='A' && b<='Z') b = (char)(b - 'A' + 'a');
      if(a != b) break;
      i++;
    }
    if(i==nlen) return 1;
  }
  return 0;
}

static int path_is_stdlib(const char* path){
  if(!path) return 0;
  if(ci_has_substr(path, "/lib/") || ci_has_substr(path, "\\lib\\") ||
     ci_has_substr(path, "lib/")  || ci_has_substr(path, "lib\\")  ||
     ci_has_substr(path, "/tools/") || ci_has_substr(path, "\\tools\\") ||
     ci_has_substr(path, "tools/")  || ci_has_substr(path, "tools\\"))
    return 1;
  return 0;
}

static int import_list_has(const char** list, int n, const char* name){
  if(!list || !name) return 0;
  for(int i=0;i<n;i++){
    if(list[i] && strcmp(list[i], name)==0) return 1;
  }
  return 0;
}

static void import_list_add(const char*** list, int* n, const char* name){
  if(!list || !n || !name) return;
  if(import_list_has(*list, *n, name)) return;
  char* s = (char*)xmalloc(strlen(name) + 1);
  strcpy(s, name);
  const char** items = (const char**)realloc((void*)(*list), sizeof(char*)*(size_t)(*n+1));
  if(!items) die("out of memory");
  items[*n] = s;
  *list = items;
  *n = *n + 1;
}

static inline Value v_i64(long long x){ Value v; v.k=V_I64; v.i=x; v.f=0.0; v.p=NULL; v.ty=NULL; return v; }
static inline Value v_f64(double x){ Value v; v.k=V_F64; v.f=x; v.i=0; v.p=NULL; v.ty=NULL; return v; }
static inline Value v_ptr(void* p){ Value v; v.k=V_PTR; v.p=p; v.i=0; v.f=0.0; v.ty=NULL; return v; }

static inline long long val_as_i64(Value v){
  if(v.k==V_I64) return v.i;
  if(v.k==V_F64) return (long long)v.f;
  return (long long)(intptr_t)v.p;
}
static inline double val_as_f64(Value v){
  if(v.k==V_F64) return v.f;
  if(v.k==V_I64) return (double)v.i;
  return (double)(intptr_t)v.p;
}
static inline void* val_as_ptr(Value v){
  if(v.k==V_PTR) return v.p;
  return (void*)(intptr_t)v.i;
}

typedef enum {
  BC_ICONST,
  BC_FCONST,
  BC_ADDRSYM,
  BC_SCONST,
  BC_MOV,
  BC_BIN,
  BC_CMP,
  BC_FBIN,
  BC_FCMP,
  BC_I2F,
  BC_F2I,
  BC_CALL,
  BC_CALLPTR,
  BC_ALLOCA,
  BC_LOAD,
  BC_STORE,
  BC_GEP,
  BC_RET,
  BC_JMP,
  BC_JZ
} BcOp;

typedef struct BCIns {
  BcOp op;
  int a, b, c;
  long long imm;
  long long immf;
  int sid;
  int size;
  int is_unsigned;
  int argc;
  int* args;
  int callee;           // >=0 func idx, <0 builtin/unknown
  const char* name;     // fallback name (for builtin_call)
  int nlen;
  int sym_kind;         // 0=none,1=global,2=func
  int builtin_id;
  void* cache_ptr;      // callptr inline cache
  int cache_idx;
} BCIns;

typedef struct BCFunc {
  BCIns* ins;
  int n;
  int nreg;
  int param_count;
  int frame_size;
  int* param_regs;
  int param_regs_n;
} BCFunc;

typedef struct BCModule {
  IRModule* irm;
  BCFunc* fns;
  int fn_n;
} BCModule;

static int bc_find_func(IRModule* M, const char* name, int nlen){
  if(!M || !name) return -1;
  for(int i=0;i<M->fn_n;i++){
    IRFunc* f = &M->fns[i];
    if(f->len==nlen && strncmp(f->name, name, (size_t)nlen)==0) return i;
  }
  return -1;
}

static int bc_find_global(IRModule* M, const char* name, int nlen){
  if(!M || !name) return -1;
  for(int i=0;i<M->global_n;i++){
    IRGlobal* g = &M->globals[i];
    if(g->len==nlen && strncmp(g->name, name, (size_t)nlen)==0) return i;
  }
  return -1;
}

enum {
  BC_BI_ASYNC = 0,
  BC_BI_AWAIT,
  BC_BI_SAY_MULTI,
  BC_BI_MALLOC,
  BC_BI_FREE,
  BC_BI_MEMCPY,
  BC_BI_MEMMOVE,
  BC_BI_MEMCMP
};

static int bc_builtin_id(const char* name, int nlen){
  if(nlen==5 && strncmp(name, "async", 5)==0) return BC_BI_ASYNC;
  if(nlen==5 && strncmp(name, "await", 5)==0) return BC_BI_AWAIT;
  if(nlen==9 && strncmp(name, "say_multi", 9)==0) return BC_BI_SAY_MULTI;
  if(nlen==6 && strncmp(name, "malloc", 6)==0) return BC_BI_MALLOC;
  if(nlen==4 && strncmp(name, "free", 4)==0) return BC_BI_FREE;
  if(nlen==6 && strncmp(name, "memcpy", 6)==0) return BC_BI_MEMCPY;
  if(nlen==7 && strncmp(name, "memmove", 7)==0) return BC_BI_MEMMOVE;
  if(nlen==6 && strncmp(name, "memcmp", 6)==0) return BC_BI_MEMCMP;
  return -1;
}

static BCModule* bc_build(IRModule* M){
  if(!M) return NULL;
  BCModule* bm = (BCModule*)xmalloc(sizeof(BCModule));
  memset(bm, 0, sizeof(*bm));
  bm->irm = M;
  bm->fn_n = M->fn_n;
  bm->fns = (BCFunc*)xmalloc(sizeof(BCFunc)*(size_t)bm->fn_n);
  memset(bm->fns, 0, sizeof(BCFunc)*(size_t)bm->fn_n);

  for(int fi=0; fi<M->fn_n; fi++){
    IRFunc* F = &M->fns[fi];
    BCFunc* B = &bm->fns[fi];
    int nreg = (F->max_reg >= 0) ? (F->max_reg + 1) : F->next_reg;
    if(nreg <= 0) nreg = 1;
    B->nreg = nreg;
    B->param_count = F->param_count;
    B->param_regs = NULL;
    B->param_regs_n = 0;

    if(F->param_count > 0){
      B->param_regs = (int*)xmalloc(sizeof(int)*(size_t)F->param_count);
      int pn = 0;
      if(F->param_regs && F->param_regs_n > 0){
        pn = F->param_regs_n;
        if(pn > F->param_count) pn = F->param_count;
        for(int i=0;i<pn;i++) B->param_regs[i] = F->param_regs[i];
      } else {
        for(int i=0;i<F->n;i++){
          IRIns* in = &F->ins[i];
          if(in->op==I_LABEL) break;
          if(in->op==I_STORE){
            if(pn < F->param_count) B->param_regs[pn++] = in->b;
            if(pn >= F->param_count) break;
          }
        }
      }
      B->param_regs_n = pn;
    }

    int frame_size = 0;
    int max_label = -1;
    for(int i=0;i<F->n;i++){
      IRIns* in = &F->ins[i];
      if(in->op==I_LABEL && in->a > max_label) max_label = in->a;
      if(in->op==I_JMP && in->a > max_label) max_label = in->a;
      if(in->op==I_JZ && in->b > max_label) max_label = in->b;
    }
    int* label_ip = NULL;
    if(max_label >= 0){
      label_ip = (int*)xmalloc(sizeof(int)*(size_t)(max_label+1));
      for(int i=0;i<=max_label;i++) label_ip[i] = -1;
    }

    int ip = 0;
    for(int i=0;i<F->n;i++){
      IRIns* in = &F->ins[i];
      if(in->op==I_LABEL){
        if(label_ip && in->a >=0 && in->a <= max_label) label_ip[in->a] = ip;
        continue;
      }
      ip++;
    }

    B->n = ip;
    B->ins = (BCIns*)xmalloc(sizeof(BCIns)*(size_t)ip);
    memset(B->ins, 0, sizeof(BCIns)*(size_t)ip);

    ip = 0;
    for(int i=0;i<F->n;i++){
      IRIns* in = &F->ins[i];
      if(in->op==I_LABEL) continue;
      BCIns* bi = &B->ins[ip++];
      bi->a = in->a;
      bi->b = in->b;
      bi->c = in->c;
      bi->imm = in->imm;
      bi->immf = in->immf;
      bi->sid = in->sid;
      bi->size = in->size;
      bi->is_unsigned = in->is_unsigned;
      bi->argc = in->argc;
      bi->args = in->args;
      bi->name = in->name;
      bi->nlen = in->nlen;
      bi->callee = -1;
      bi->sym_kind = 0;
        bi->builtin_id = -1;
        bi->cache_ptr = NULL;
        bi->cache_idx = -1;

      switch(in->op){
        case I_ICONST: bi->op = BC_ICONST; break;
        case I_FCONST: bi->op = BC_FCONST; break;
        case I_ADDRSYM: {
          bi->op = BC_ADDRSYM;
          int gi = bc_find_global(M, in->name, in->nlen);
          if(gi >= 0){ bi->sym_kind = 1; bi->imm = gi; break; }
          int fi2 = bc_find_func(M, in->name, in->nlen);
          if(fi2 >= 0){ bi->sym_kind = 2; bi->imm = fi2; break; }
          bi->sym_kind = 0;
          break;
        }
        case I_SCONST: bi->op = BC_SCONST; break;
        case I_MOV: bi->op = BC_MOV; break;
        case I_BIN: bi->op = BC_BIN; bi->imm = (long long)in->binop; break;
        case I_CMP: bi->op = BC_CMP; bi->imm = (long long)in->cmpop; break;
        case I_FBIN: bi->op = BC_FBIN; bi->imm = (long long)in->fop; break;
        case I_FCMP: bi->op = BC_FCMP; bi->imm = (long long)in->cmpop; break;
        case I_I2F: bi->op = BC_I2F; break;
        case I_F2I: bi->op = BC_F2I; break;
        case I_CALL: {
          bi->op = BC_CALL;
          int callee = bc_find_func(M, in->name, in->nlen);
          if(callee >= 0 && M->fns[callee].is_extern){
            callee = -1;
          }
          bi->callee = callee;
          bi->builtin_id = bc_builtin_id(in->name, in->nlen);
          break;
        }
        case I_CALLPTR: bi->op = BC_CALLPTR; break;
        case I_ALLOCA: {
          bi->op = BC_ALLOCA;
          int align = in->align > 0 ? in->align : in->size;
          if(align <= 0) align = 1;
          frame_size = align_up_i(frame_size, align);
          bi->imm = frame_size;
          frame_size += in->size;
          break;
        }
        case I_LOAD: bi->op = BC_LOAD; break;
        case I_STORE: bi->op = BC_STORE; break;
        case I_GEP: bi->op = BC_GEP; break;
        case I_RET: bi->op = BC_RET; break;
        case I_JMP: {
          bi->op = BC_JMP;
          if(label_ip && in->a >=0 && in->a <= max_label) bi->a = label_ip[in->a];
          break;
        }
        case I_JZ: {
          bi->op = BC_JZ;
          if(label_ip && in->b >=0 && in->b <= max_label) bi->b = label_ip[in->b];
          break;
        }
        default:
          bi->op = BC_ICONST;
          bi->imm = 0;
          break;
      }
    }

    if(label_ip) free(label_ip);
    B->frame_size = frame_size;
  }

  return bm;
}

typedef struct {
  unsigned char* base;
  size_t size;
  unsigned char* tags; // 0=int,1=f64,2=ptr
} MemBlock;

typedef struct {
  BCModule* bm;
  MemBlock* blocks;
  int bcount, bcap;
  void** globals;
  int gcount;
  Value* reg_stack;
  int reg_cap;
  int reg_top;
  Value* arg_stack;
  int arg_cap;
  int arg_top;
} BCVM;

static void mem_add(BCVM* vm, void* base, size_t size){
  if(!vm || !base || size==0) return;
  if(vm->bcount==vm->bcap){
    vm->bcap = vm->bcap? vm->bcap*2 : 32;
    vm->blocks = (MemBlock*)xrealloc(vm->blocks, sizeof(MemBlock)*(size_t)vm->bcap);
  }
  MemBlock* b = &vm->blocks[vm->bcount++];
  b->base = (unsigned char*)base;
  b->size = size;
  size_t tn = (size + 7) / 8;
  b->tags = (unsigned char*)xmalloc(tn);
  memset(b->tags, 0, tn);
}

static void mem_remove(BCVM* vm, void* base){
  if(!vm || !base) return;
  for(int i=0;i<vm->bcount;i++){
    if(vm->blocks[i].base == (unsigned char*)base){
      free(vm->blocks[i].tags);
      vm->blocks[i] = vm->blocks[vm->bcount-1];
      vm->bcount--;
      return;
    }
  }
}

static MemBlock* mem_find(BCVM* vm, void* addr, size_t size, size_t* out_off){
  if(!vm || !addr) return NULL;
  uintptr_t a = (uintptr_t)addr;
  for(int i=0;i<vm->bcount;i++){
    MemBlock* b = &vm->blocks[i];
    uintptr_t lo = (uintptr_t)b->base;
    uintptr_t hi = lo + b->size;
    if(a >= lo && (a + size) <= hi){
      if(out_off) *out_off = (size_t)(a - lo);
      return b;
    }
  }
  return NULL;
}

static void mem_tag_set(BCVM* vm, void* addr, size_t size, int tag){
  size_t off = 0;
  MemBlock* b = mem_find(vm, addr, size, &off);
  if(!b) return;
  if(size >= 8){
    size_t idx = off / 8;
    size_t tn = (b->size + 7) / 8;
    if(idx < tn) b->tags[idx] = (unsigned char)tag;
  } else {
    size_t idx = off / 8;
    size_t tn = (b->size + 7) / 8;
    if(idx < tn) b->tags[idx] = 0;
  }
}

static void mem_tag_f64_range(BCVM* vm, void* addr, long long count){
  if(!vm || !addr || count <= 0) return;
  for(long long i = 0; i < count; ++i){
    mem_tag_set(vm, (unsigned char*)addr + (size_t)i * 8u, 8, 1);
  }
}

static void normalize_call_name(const char** name, int* nlen){
  if(!name || !*name || !nlen || *nlen <= 0) return;
  for(int i = *nlen - 1; i >= 0; --i){
    if((*name)[i] == '.'){
      if(i + 1 < *nlen){
        *name = *name + i + 1;
        *nlen = *nlen - (i + 1);
      }
      return;
    }
  }
}

static void bc_tag_ai_builtin_outputs(BCVM* vm, const char* name, int nlen, Value* args, int argc, Value* out){
  normalize_call_name(&name, &nlen);
  if(!out || out->k != V_I64 || out->i != 0) return;
  if(nlen==18 && strncmp(name, "tezz_ai_matmul_f64", 18)==0 && argc==6){
    long long m = val_as_i64(args[3]);
    long long n = val_as_i64(args[4]);
    if(m > 0 && n > 0) mem_tag_f64_range(vm, val_as_ptr(args[0]), m * n);
    return;
  }
  if(nlen==21 && strncmp(name, "tezz_ai_matmul_i8_f64", 21)==0 && argc==7){
    long long m = val_as_i64(args[4]);
    long long n = val_as_i64(args[5]);
    if(m > 0 && n > 0) mem_tag_f64_range(vm, val_as_ptr(args[0]), m * n);
    return;
  }
  if(nlen==24 && strncmp(name, "tezz_ai_rmsnorm_rows_f64", 24)==0 && argc==6){
    long long rows = val_as_i64(args[3]);
    long long cols = val_as_i64(args[4]);
    if(rows > 0 && cols > 0) mem_tag_f64_range(vm, val_as_ptr(args[0]), rows * cols);
    return;
  }
  if(nlen==24 && strncmp(name, "tezz_ai_softmax_rows_f64", 24)==0 && argc==4){
    long long rows = val_as_i64(args[2]);
    long long cols = val_as_i64(args[3]);
    if(rows > 0 && cols > 0) mem_tag_f64_range(vm, val_as_ptr(args[0]), rows * cols);
    return;
  }
  if(nlen==16 && strncmp(name, "tezz_ai_rope_f64", 16)==0 && argc==5){
    long long dim = val_as_i64(args[3]);
    if(dim > 0){
      mem_tag_f64_range(vm, val_as_ptr(args[0]), dim);
      mem_tag_f64_range(vm, val_as_ptr(args[1]), dim);
    }
  }
}

static int mem_tag_get(BCVM* vm, void* addr){
  size_t off = 0;
  MemBlock* b = mem_find(vm, addr, 8, &off);
  if(!b) return 0;
  size_t idx = off / 8;
  size_t tn = (b->size + 7) / 8;
  if(idx < tn) return (int)b->tags[idx];
  return 0;
}

static void mem_tag_copy(BCVM* vm, void* dst, void* src, size_t n){
  if(!vm || !dst || !src || n==0) return;
  size_t off = 0;
  while(off < n){
    size_t step = 8;
    if(off + step > n) break;
    size_t so = 0, doff = 0;
    MemBlock* sb = mem_find(vm, (unsigned char*)src + off, step, &so);
    MemBlock* db = mem_find(vm, (unsigned char*)dst + off, step, &doff);
    if(db){
      int tag = 0;
      if(sb){
        size_t sidx = so / 8;
        size_t stn = (sb->size + 7) / 8;
        if(sidx < stn) tag = (int)sb->tags[sidx];
      }
      size_t didx = doff / 8;
      size_t dtn = (db->size + 7) / 8;
      if(didx < dtn) db->tags[didx] = (unsigned char)tag;
    }
    off += step;
  }
}

static Value mem_load(BCVM* vm, void* addr, int size, int is_unsigned){
  if(!addr) return v_i64(0);
  if(size == 1){
    if(is_unsigned){
      uint8_t v = 0;
      memcpy(&v, addr, 1);
      return v_i64((long long)v);
    }
    int8_t v = 0;
    memcpy(&v, addr, 1);
    return v_i64((long long)v);
  }
  if(size == 2){
    if(is_unsigned){
      uint16_t v = 0;
      memcpy(&v, addr, 2);
      return v_i64((long long)v);
    }
    int16_t v = 0;
    memcpy(&v, addr, 2);
    return v_i64((long long)v);
  }
  if(size == 4){
    if(is_unsigned){
      uint32_t v = 0;
      memcpy(&v, addr, 4);
      return v_i64((long long)v);
    }
    int32_t v = 0;
    memcpy(&v, addr, 4);
    return v_i64((long long)v);
  }
  int tag = mem_tag_get(vm, addr);
  if(tag == 1){
    double d = 0.0;
    memcpy(&d, addr, 8);
    return v_f64(d);
  }
  if(tag == 2){
    void* p = NULL;
    memcpy(&p, addr, 8);
    return v_ptr(p);
  }
  long long v = 0;
  memcpy(&v, addr, 8);
  return v_i64(v);
}

static void mem_store(BCVM* vm, void* addr, int size, Value v){
  if(!addr) return;
  if(size == 1){
    unsigned char x = (unsigned char)(val_as_i64(v) & 0xFF);
    memcpy(addr, &x, 1);
    mem_tag_set(vm, addr, 1, 0);
    return;
  }
  if(size == 2){
    unsigned short x = (unsigned short)(val_as_i64(v) & 0xFFFF);
    memcpy(addr, &x, 2);
    mem_tag_set(vm, addr, 2, 0);
    return;
  }
  if(size == 4){
    unsigned int x = (unsigned int)(val_as_i64(v) & 0xFFFFFFFFu);
    memcpy(addr, &x, 4);
    mem_tag_set(vm, addr, 4, 0);
    return;
  }
  if(v.k==V_F64){
    double d = v.f;
    memcpy(addr, &d, 8);
    mem_tag_set(vm, addr, 8, 1);
  } else if(v.k==V_PTR){
    void* p = v.p;
    memcpy(addr, &p, 8);
    mem_tag_set(vm, addr, 8, 2);
  } else {
    long long x = v.i;
    memcpy(addr, &x, 8);
    mem_tag_set(vm, addr, 8, 0);
  }
}

static Value bc_exec(BCVM* vm, int fidx, Value* args, int argc);

  static Value bc_call_ptr(BCVM* vm, Value callee, Value* args, int argc){
    void* p = val_as_ptr(callee);
    if(p){
      BCFunc* fn = (BCFunc*)p;
      int idx = (int)(fn - vm->bm->fns);
      if(idx >= 0 && idx < vm->bm->fn_n){
        return bc_exec(vm, idx, args, argc);
      }
    }
    die("BC-VM: bad function pointer");
    return v_i64(0);
  }

  static int bc_builtin_fast(BCVM* vm, int id, Value* args, int argc, Value* out){
    if(id == BC_BI_ASYNC){
      if(argc!=1) return 0;
      Value r = bc_call_ptr(vm, args[0], NULL, 0);
      if(g_vm_async_zero){
        out->k=V_I64; out->i=val_as_i64(r); out->p=NULL; out->ty=NULL;
        return 1;
      }
      typedef struct { long long result; } AsyncTaskBC;
      AsyncTaskBC* t = (AsyncTaskBC*)xmalloc(sizeof(AsyncTaskBC));
      t->result = val_as_i64(r);
      out->k=V_PTR; out->p=t; out->i=0; out->ty=NULL;
      return 1;
    }
    if(id == BC_BI_AWAIT){
      if(argc!=1) return 0;
      if(g_vm_async_zero){
        out->k=V_I64; out->i=val_as_i64(args[0]); out->p=NULL; out->ty=NULL;
        return 1;
      }
      if(args[0].k==V_PTR && args[0].p){
        typedef struct { long long result; } AsyncTaskBC;
        AsyncTaskBC* t = (AsyncTaskBC*)args[0].p;
        long long r = t->result;
        free(t);
      out->k=V_I64; out->i=r; out->p=NULL; out->ty=NULL;
      return 1;
    }
    out->k=V_I64; out->i=val_as_i64(args[0]); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(id == BC_BI_SAY_MULTI){
    if(argc!=3) return 0;
    long long* vals = (long long*)val_as_ptr(args[0]);
    long long* tags = (long long*)val_as_ptr(args[1]);
    long long n = val_as_i64(args[2]);
    if(n < 0) n = 0;
    if(n > 1024) n = 1024;
    Value stack_av[64];
    Value* av = NULL;
    if(n <= 64){
      av = stack_av;
    } else {
      av = (Value*)xmalloc(sizeof(Value)*(size_t)n);
    }
    for(long long i=0;i<n;i++){
      long long tag = tags ? tags[i] : 0;
      if(tag == 1){
        double dv = 0.0;
        memcpy(&dv, &vals[i], sizeof(double));
        av[i] = v_f64(dv);
      } else if(tag == 2){
        av[i] = v_ptr((void*)(intptr_t)vals[i]);
      } else {
        av[i] = v_i64(vals ? vals[i] : 0);
      }
    }
    int ok = builtin_call("say", 3, av, (int)n, out);
    if(n > 64) free(av);
    return ok;
  }
  if(id == BC_BI_MALLOC){
    if(argc!=1) return 0;
    long long n = val_as_i64(args[0]);
    if(n < 0) n = 0;
    void* p = malloc((size_t)n);
    if(p) mem_add(vm, p, (size_t)n);
    out->k=V_PTR; out->p=p; out->i=0; out->ty=NULL;
    return 1;
  }
  if(id == BC_BI_FREE){
    if(argc!=1) return 0;
    void* p = val_as_ptr(args[0]);
    if(p){
      mem_remove(vm, p);
      free(p);
    }
    out->k=V_I64; out->i=0; out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(id == BC_BI_MEMCPY){
    if(argc!=3) return 0;
    void* dst = val_as_ptr(args[0]);
    void* src = val_as_ptr(args[1]);
    long long n = val_as_i64(args[2]);
    if(n < 0) n = 0;
    if(dst && src && n > 0){
      memcpy(dst, src, (size_t)n);
      mem_tag_copy(vm, dst, src, (size_t)n);
    }
    out->k=V_PTR; out->p=dst; out->i=0; out->ty=NULL;
    return 1;
  }
  if(id == BC_BI_MEMMOVE){
    if(argc!=3) return 0;
    void* dst = val_as_ptr(args[0]);
    void* src = val_as_ptr(args[1]);
    long long n = val_as_i64(args[2]);
    if(n < 0) n = 0;
    if(dst && src && n > 0){
      memmove(dst, src, (size_t)n);
      mem_tag_copy(vm, dst, src, (size_t)n);
    }
    out->k=V_PTR; out->p=dst; out->i=0; out->ty=NULL;
    return 1;
  }
  if(id == BC_BI_MEMCMP){
    if(argc!=3) return 0;
    void* a = val_as_ptr(args[0]);
    void* b = val_as_ptr(args[1]);
    long long n = val_as_i64(args[2]);
    if(n < 0) n = 0;
    int r = 0;
    if(a && b && n > 0) r = memcmp(a, b, (size_t)n);
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }
  return 0;
}

static int bc_builtin(BCVM* vm, const char* name, int nlen, Value* args, int argc, Value* out){
  int id = bc_builtin_id(name, nlen);
  if(id >= 0 && bc_builtin_fast(vm, id, args, argc, out)) return 1;
  {
    int ok = builtin_call(name, nlen, args, argc, out);
    if(ok) bc_tag_ai_builtin_outputs(vm, name, nlen, args, argc, out);
    return ok;
  }
}

static Value bc_exec(BCVM* vm, int fidx, Value* args, int argc){
  BCFunc* fn = &vm->bm->fns[fidx];
  int nreg = fn->nreg;
  int reg_base = vm->reg_top;
  int reg_need = reg_base + nreg;
  if(reg_need > vm->reg_cap){
    int newcap = vm->reg_cap ? vm->reg_cap * 2 : 256;
    while(newcap < reg_need) newcap *= 2;
    vm->reg_stack = (Value*)xrealloc(vm->reg_stack, sizeof(Value)*(size_t)newcap);
    vm->reg_cap = newcap;
  }
  vm->reg_top = reg_need;
  Value* regs = vm->reg_stack + reg_base;
  memset(regs, 0, sizeof(Value)*(size_t)nreg);
  for(int i=0;i<fn->param_count && i<argc;i++){
    int r = (i < fn->param_regs_n) ? fn->param_regs[i] : i;
    if(r >= 0 && r < nreg) regs[r] = args[i];
  }

  void* frame = NULL;
  if(fn->frame_size > 0){
    frame = xmalloc((size_t)fn->frame_size);
    memset(frame, 0, (size_t)fn->frame_size);
    mem_add(vm, frame, (size_t)fn->frame_size);
  }

  Value ret = v_i64(0);
  int ip = 0;
  BCIns* ins = fn->ins;
  int ins_n = fn->n;

#if defined(__GNUC__)
  static void* dispatch[] = {
    &&op_ICONST, &&op_FCONST, &&op_ADDRSYM, &&op_SCONST, &&op_MOV,
    &&op_BIN, &&op_CMP, &&op_FBIN, &&op_FCMP, &&op_I2F, &&op_F2I,
    &&op_CALL, &&op_CALLPTR, &&op_ALLOCA, &&op_LOAD, &&op_STORE,
    &&op_GEP, &&op_RET, &&op_JMP, &&op_JZ
  };
#define DISPATCH() goto *dispatch[ins[ip].op]
  if(ins_n == 0){
    if(frame){
      mem_remove(vm, frame);
      free(frame);
    }
    vm->reg_top = reg_base;
    return ret;
  }
  DISPATCH();

op_ICONST: {
    BCIns* in = &ins[ip];
    regs[in->a] = v_i64(in->imm);
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_FCONST: {
    BCIns* in = &ins[ip];
    double d = 0.0;
    memcpy(&d, &in->immf, sizeof(double));
    regs[in->a] = v_f64(d);
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_ADDRSYM: {
    BCIns* in = &ins[ip];
    if(in->sym_kind == 1){
      regs[in->a] = v_ptr(vm->globals[(int)in->imm]);
    } else if(in->sym_kind == 2){
      regs[in->a] = v_ptr(&vm->bm->fns[(int)in->imm]);
    } else {
      regs[in->a] = v_ptr(NULL);
    }
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_SCONST: {
    BCIns* in = &ins[ip];
    if(in->sid >= 0 && in->sid < vm->bm->irm->str_n){
      regs[in->a] = v_ptr(vm->bm->irm->strs[in->sid].bytes);
    } else {
      regs[in->a] = v_ptr(NULL);
    }
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_MOV: {
    BCIns* in = &ins[ip];
    regs[in->a] = regs[in->b];
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_BIN: {
    BCIns* in = &ins[ip];
    long long a = val_as_i64(regs[in->b]);
    long long b = val_as_i64(regs[in->c]);
    long long r = 0;
    switch((IRBin)in->imm){
      case B_ADD: r = a + b; break;
      case B_SUB: r = a - b; break;
      case B_MUL: r = a * b; break;
      case B_DIV: r = b ? (a / b) : 0; break;
      case B_MOD: r = b ? (a % b) : 0; break;
      case B_AND: r = a & b; break;
      case B_OR:  r = a | b; break;
      case B_XOR: r = a ^ b; break;
      case B_SHL: r = a << (b & 63); break;
      case B_SHR: r = a >> (b & 63); break;
      default: r = 0; break;
    }
    regs[in->a] = v_i64(r);
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_CMP: {
    BCIns* in = &ins[ip];
    long long a = val_as_i64(regs[in->b]);
    long long b = val_as_i64(regs[in->c]);
    long long r = 0;
    switch((IRCmp)in->imm){
      case C_EQ:  r = (a==b); break;
      case C_NEQ: r = (a!=b); break;
      case C_LT:  r = (a<b); break;
      case C_LTE: r = (a<=b); break;
      case C_GT:  r = (a>b); break;
      case C_GTE: r = (a>=b); break;
      default: r = 0; break;
    }
    regs[in->a] = v_i64(r);
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_FBIN: {
    BCIns* in = &ins[ip];
    double a = val_as_f64(regs[in->b]);
    double b = val_as_f64(regs[in->c]);
    double r = 0.0;
    switch((IRFlt)in->imm){
      case F_ADD: r = a + b; break;
      case F_SUB: r = a - b; break;
      case F_MUL: r = a * b; break;
      case F_DIV: r = b!=0.0 ? (a / b) : 0.0; break;
      default: r = 0.0; break;
    }
    regs[in->a] = v_f64(r);
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_FCMP: {
    BCIns* in = &ins[ip];
    double a = val_as_f64(regs[in->b]);
    double b = val_as_f64(regs[in->c]);
    long long r = 0;
    switch((IRCmp)in->imm){
      case C_EQ:  r = (a==b); break;
      case C_NEQ: r = (a!=b); break;
      case C_LT:  r = (a<b); break;
      case C_LTE: r = (a<=b); break;
      case C_GT:  r = (a>b); break;
      case C_GTE: r = (a>=b); break;
      default: r = 0; break;
    }
    regs[in->a] = v_i64(r);
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_I2F: {
    BCIns* in = &ins[ip];
    regs[in->a] = v_f64((double)val_as_i64(regs[in->b]));
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_F2I: {
    BCIns* in = &ins[ip];
    regs[in->a] = v_i64((long long)val_as_f64(regs[in->b]));
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_CALL: {
    BCIns* in = &ins[ip];
    int argc2 = in->argc;
    Value av_small[8];
    Value* av = NULL;
    int arg_base = -1;
    if(argc2 > 0){
      if(argc2 <= (int)(sizeof(av_small)/sizeof(av_small[0]))){
        av = av_small;
      } else {
        int need = vm->arg_top + argc2;
        if(need > vm->arg_cap){
          int newcap = vm->arg_cap ? vm->arg_cap * 2 : 128;
          while(newcap < need) newcap *= 2;
          vm->arg_stack = (Value*)xrealloc(vm->arg_stack, sizeof(Value)*(size_t)newcap);
          vm->arg_cap = newcap;
        }
        arg_base = vm->arg_top;
        vm->arg_top = need;
        av = vm->arg_stack + arg_base;
      }
      for(int i=0;i<argc2;i++){
        av[i] = regs[in->args[i]];
      }
    }
    Value r;
    if(in->callee >= 0){
      r = bc_exec(vm, in->callee, av, argc2);
    } else {
      Value out;
      int ok = 0;
      if(in->builtin_id >= 0){
        ok = bc_builtin_fast(vm, in->builtin_id, av, argc2, &out);
      }
      if(!ok && !bc_builtin(vm, in->name, in->nlen, av, argc2, &out)){
        fprintf(stderr, "tezzc error: BC-VM: unknown function call '%.*s'\n", in->nlen, in->name);
        exit(1);
      }
      r = out;
    }
    if(in->a >= 0) regs[in->a] = r;
    if(arg_base >= 0) vm->arg_top = arg_base;
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
  op_CALLPTR: {
      BCIns* in = &ins[ip];
      int argc2 = in->argc;
      Value av_small[8];
      Value* av = NULL;
    int arg_base = -1;
    if(argc2 > 0){
      if(argc2 <= (int)(sizeof(av_small)/sizeof(av_small[0]))){
        av = av_small;
      } else {
        int need = vm->arg_top + argc2;
        if(need > vm->arg_cap){
          int newcap = vm->arg_cap ? vm->arg_cap * 2 : 128;
          while(newcap < need) newcap *= 2;
          vm->arg_stack = (Value*)xrealloc(vm->arg_stack, sizeof(Value)*(size_t)newcap);
          vm->arg_cap = newcap;
        }
        arg_base = vm->arg_top;
        vm->arg_top = need;
        av = vm->arg_stack + arg_base;
      }
        for(int i=0;i<argc2;i++){
          av[i] = regs[in->args[i]];
        }
      }
      Value r;
      void* p = val_as_ptr(regs[in->b]);
      if(p && p == in->cache_ptr && in->cache_idx >= 0){
        r = bc_exec(vm, in->cache_idx, av, argc2);
      } else {
        r = bc_call_ptr(vm, regs[in->b], av, argc2);
        in->cache_ptr = p;
        if(p){
          int idx = (int)(((BCFunc*)p) - vm->bm->fns);
          in->cache_idx = (idx >= 0 && idx < vm->bm->fn_n) ? idx : -1;
        } else {
          in->cache_idx = -1;
        }
      }
      if(in->a >= 0) regs[in->a] = r;
      if(arg_base >= 0) vm->arg_top = arg_base;
      ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
    }
op_ALLOCA: {
    BCIns* in = &ins[ip];
    if(frame){
      unsigned char* base = (unsigned char*)frame;
      regs[in->a] = v_ptr(base + (size_t)in->imm);
    } else if(in->size > 0){
      void* p = xmalloc((size_t)in->size);
      memset(p, 0, (size_t)in->size);
      regs[in->a] = v_ptr(p);
      mem_add(vm, p, (size_t)in->size);
    } else {
      regs[in->a] = v_ptr(NULL);
    }
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_LOAD: {
    BCIns* in = &ins[ip];
    void* p = val_as_ptr(regs[in->b]);
    regs[in->a] = mem_load(vm, p, in->size, in->is_unsigned);
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_STORE: {
    BCIns* in = &ins[ip];
    void* p = val_as_ptr(regs[in->a]);
    mem_store(vm, p, in->size, regs[in->b]);
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_GEP: {
    BCIns* in = &ins[ip];
    void* p = val_as_ptr(regs[in->b]);
    regs[in->a] = v_ptr((unsigned char*)p + in->imm);
    ip++; if(ip < ins_n) DISPATCH(); goto bc_end;
  }
op_RET: {
    BCIns* in = &ins[ip];
    if(in->a >= 0) ret = regs[in->a];
    ip = ins_n;
    goto bc_end;
  }
op_JMP: {
    BCIns* in = &ins[ip];
    ip = in->a;
    if(ip < ins_n) DISPATCH();
    goto bc_end;
  }
op_JZ: {
    BCIns* in = &ins[ip];
    long long v = val_as_i64(regs[in->a]);
    if(v==0) ip = in->b;
    else ip++;
    if(ip < ins_n) DISPATCH();
    goto bc_end;
  }
#undef DISPATCH
bc_end:
#else
  while(ip < ins_n){
    BCIns* in = &ins[ip];
    switch(in->op){
      case BC_ICONST: regs[in->a] = v_i64(in->imm); ip++; break;
      case BC_FCONST: { double d=0.0; memcpy(&d,&in->immf,sizeof(double)); regs[in->a]=v_f64(d); ip++; break; }
      case BC_ADDRSYM:
        if(in->sym_kind==1) regs[in->a]=v_ptr(vm->globals[(int)in->imm]);
        else if(in->sym_kind==2) regs[in->a]=v_ptr(&vm->bm->fns[(int)in->imm]);
        else regs[in->a]=v_ptr(NULL);
        ip++; break;
      case BC_SCONST:
        if(in->sid>=0 && in->sid<vm->bm->irm->str_n) regs[in->a]=v_ptr(vm->bm->irm->strs[in->sid].bytes);
        else regs[in->a]=v_ptr(NULL);
        ip++; break;
      case BC_MOV: regs[in->a]=regs[in->b]; ip++; break;
      case BC_BIN: {
        long long a=val_as_i64(regs[in->b]);
        long long b=val_as_i64(regs[in->c]);
        long long r=0;
        switch((IRBin)in->imm){
          case B_ADD: r=a+b; break; case B_SUB: r=a-b; break; case B_MUL: r=a*b; break;
          case B_DIV: r=b? a/b:0; break; case B_MOD: r=b? a%b:0; break;
          case B_AND: r=a&b; break; case B_OR: r=a|b; break; case B_XOR: r=a^b; break;
          case B_SHL: r=a << (b&63); break; case B_SHR: r=a >> (b&63); break;
          default: r=0; break;
        }
        regs[in->a]=v_i64(r); ip++; break;
      }
      case BC_CMP: {
        long long a=val_as_i64(regs[in->b]);
        long long b=val_as_i64(regs[in->c]);
        long long r=0;
        switch((IRCmp)in->imm){
          case C_EQ: r=(a==b); break; case C_NEQ: r=(a!=b); break;
          case C_LT: r=(a<b); break; case C_LTE: r=(a<=b); break;
          case C_GT: r=(a>b); break; case C_GTE: r=(a>=b); break;
          default: r=0; break;
        }
        regs[in->a]=v_i64(r); ip++; break;
      }
      case BC_FBIN: {
        double a=val_as_f64(regs[in->b]);
        double b=val_as_f64(regs[in->c]);
        double r=0.0;
        switch((IRFlt)in->imm){
          case F_ADD: r=a+b; break; case F_SUB: r=a-b; break; case F_MUL: r=a*b; break;
          case F_DIV: r=b!=0.0? a/b:0.0; break; default: r=0.0; break;
        }
        regs[in->a]=v_f64(r); ip++; break;
      }
      case BC_FCMP: {
        double a=val_as_f64(regs[in->b]);
        double b=val_as_f64(regs[in->c]);
        long long r=0;
        switch((IRCmp)in->imm){
          case C_EQ: r=(a==b); break; case C_NEQ: r=(a!=b); break;
          case C_LT: r=(a<b); break; case C_LTE: r=(a<=b); break;
          case C_GT: r=(a>b); break; case C_GTE: r=(a>=b); break;
          default: r=0; break;
        }
        regs[in->a]=v_i64(r); ip++; break;
      }
      case BC_I2F: regs[in->a]=v_f64((double)val_as_i64(regs[in->b])); ip++; break;
      case BC_F2I: regs[in->a]=v_i64((long long)val_as_f64(regs[in->b])); ip++; break;
      case BC_CALL: {
        int argc2=in->argc;
        Value av_small[8];
        Value* av=NULL;
        int arg_base = -1;
        if(argc2>0){
          if(argc2 <= (int)(sizeof(av_small)/sizeof(av_small[0]))){
            av = av_small;
          } else {
            int need = vm->arg_top + argc2;
            if(need > vm->arg_cap){
              int newcap = vm->arg_cap ? vm->arg_cap * 2 : 128;
              while(newcap < need) newcap *= 2;
              vm->arg_stack = (Value*)xrealloc(vm->arg_stack, sizeof(Value)*(size_t)newcap);
              vm->arg_cap = newcap;
            }
            arg_base = vm->arg_top;
            vm->arg_top = need;
            av = vm->arg_stack + arg_base;
          }
          for(int i=0;i<argc2;i++) av[i]=regs[in->args[i]];
        }
        Value r;
        if(in->callee>=0) r=bc_exec(vm, in->callee, av, argc2);
        else {
          Value out;
          int ok = 0;
          if(in->builtin_id >= 0) ok = bc_builtin_fast(vm, in->builtin_id, av, argc2, &out);
          if(!ok && !bc_builtin(vm,in->name,in->nlen,av,argc2,&out)){
            fprintf(stderr,"tezzc error: BC-VM: unknown function call '%.*s'\n", in->nlen, in->name);
            exit(1);
          }
          r=out;
        }
        if(in->a>=0) regs[in->a]=r;
        if(arg_base >= 0) vm->arg_top = arg_base;
        ip++; break;
      }
        case BC_CALLPTR: {
          int argc2=in->argc;
          Value av_small[8];
          Value* av=NULL;
          int arg_base = -1;
        if(argc2>0){
          if(argc2 <= (int)(sizeof(av_small)/sizeof(av_small[0]))){
            av = av_small;
          } else {
            int need = vm->arg_top + argc2;
            if(need > vm->arg_cap){
              int newcap = vm->arg_cap ? vm->arg_cap * 2 : 128;
              while(newcap < need) newcap *= 2;
              vm->arg_stack = (Value*)xrealloc(vm->arg_stack, sizeof(Value)*(size_t)newcap);
              vm->arg_cap = newcap;
            }
            arg_base = vm->arg_top;
            vm->arg_top = need;
            av = vm->arg_stack + arg_base;
            }
            for(int i=0;i<argc2;i++) av[i]=regs[in->args[i]];
          }
          Value r;
          void* p = val_as_ptr(regs[in->b]);
          if(p && p == in->cache_ptr && in->cache_idx >= 0){
            r = bc_exec(vm, in->cache_idx, av, argc2);
          } else {
            r = bc_call_ptr(vm, regs[in->b], av, argc2);
            in->cache_ptr = p;
            if(p){
              int idx = (int)(((BCFunc*)p) - vm->bm->fns);
              in->cache_idx = (idx >= 0 && idx < vm->bm->fn_n) ? idx : -1;
            } else {
              in->cache_idx = -1;
            }
          }
          if(in->a>=0) regs[in->a]=r;
          if(arg_base >= 0) vm->arg_top = arg_base;
          ip++; break;
        }
      case BC_ALLOCA: {
        if(frame){
          unsigned char* base = (unsigned char*)frame;
          regs[in->a]=v_ptr(base + (size_t)in->imm);
        } else if(in->size > 0){
          void* p=xmalloc((size_t)in->size);
          memset(p,0,(size_t)in->size);
          regs[in->a]=v_ptr(p);
          mem_add(vm,p,(size_t)in->size);
        } else {
          regs[in->a]=v_ptr(NULL);
        }
        ip++; break;
      }
      case BC_LOAD: { void* p=val_as_ptr(regs[in->b]); regs[in->a]=mem_load(vm,p,in->size,in->is_unsigned); ip++; break; }
      case BC_STORE: { void* p=val_as_ptr(regs[in->a]); mem_store(vm,p,in->size,regs[in->b]); ip++; break; }
      case BC_GEP: { void* p=val_as_ptr(regs[in->b]); regs[in->a]=v_ptr((unsigned char*)p + in->imm); ip++; break; }
      case BC_RET: if(in->a>=0) ret=regs[in->a]; ip=ins_n; break;
      case BC_JMP: ip=in->a; break;
      case BC_JZ: { long long v=val_as_i64(regs[in->a]); if(v==0) ip=in->b; else ip++; break; }
      default: ip++; break;
    }
  }
#endif

  if(frame){
    mem_remove(vm, frame);
    free(frame);
  }
  vm->reg_top = reg_base;
  return ret;
}

extern void stdlib_set_args(int argc, char** argv);

int bc_vm_run_file(const char* path, int debug, int argc, char** argv){
  (void)debug;
  TypeEnv tenv;
  tenv_init(&tenv);

  ModuleGraph G;
  mg_init(&G);

  char* src = read_file_cstr(path);
  const char* base = dirname_of(path);

  const char** pre_imports = NULL;
  int pre_n = 0;
  scan_imports(src, path, &pre_imports, &pre_n);
  if(!path_is_stdlib(path)){
    import_list_add(&pre_imports, &pre_n, "std");
  }
  for(int i=0;i<pre_n;i++){
    (void)mg_load(&G, &tenv, base, pre_imports[i]);
  }

  Module* root = parse_module(&tenv, src, path);
  for(int i=0;i<root->import_n;i++){
    (void)mg_load(&G, &tenv, base, root->imports[i]);
  }

  if(G.mod_n==G.mod_cap){
    G.mod_cap*=2;
    G.mods=(Module**)xrealloc(G.mods,sizeof(Module*)*G.mod_cap);
  }
  G.mods[G.mod_n++]=root;

  sema_check_graph(&G, &tenv, root, 1);

  IRModule* M = ir_build_from_graph(&G, &tenv, root);
  ir_optimize(M);

  BCModule* bm = bc_build(M);

  BCVM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bm = bm;
  vm.gcount = M->global_n;
  if(vm.gcount > 0){
    vm.globals = (void**)xmalloc(sizeof(void*)*(size_t)vm.gcount);
    for(int i=0;i<vm.gcount;i++){
      IRGlobal* g = &M->globals[i];
      if(g->is_extern){
        vm.globals[i] = NULL;
        continue;
      }
      void* mem = xmalloc((size_t)g->size);
      memset(mem, 0, (size_t)g->size);
      vm.globals[i] = mem;
      mem_add(&vm, mem, (size_t)g->size);
      if(g->has_init){
        if(g->ty && g->ty->k==TY_F64){
          double d = 0.0;
          memcpy(&d, &g->init_fbits, sizeof(double));
          mem_store(&vm, mem, 8, v_f64(d));
        } else {
          mem_store(&vm, mem, g->size, v_i64(g->init_int));
        }
      }
    }
  }

  stdlib_set_args(argc, argv);

  int main_idx = bc_find_func(M, "main", 4);
  if(main_idx < 0) err_at2(path, src, 1, 1, "missing fn main()");

  Value out = bc_exec(&vm, main_idx, NULL, 0);
  int exit_code = 0;
  if(out.k == V_I64){
    exit_code = (int)out.i;
  } else if(out.k == V_F64){
    exit_code = (int)out.f;
  }

  for(int i=0;i<vm.gcount;i++){
    if(vm.globals && vm.globals[i]){
      mem_remove(&vm, vm.globals[i]);
      free(vm.globals[i]);
    }
  }
  free(vm.globals);
  for(int i=0;i<vm.bcount;i++){
    free(vm.blocks[i].tags);
  }
  free(vm.blocks);
  free(vm.reg_stack);
  free(vm.arg_stack);

  for(int i=0;i<bm->fn_n;i++){
    free(bm->fns[i].ins);
    free(bm->fns[i].param_regs);
  }
  free(bm->fns);
  free(bm);

  ir_free(M);
  mg_free(&G);
  tenv_free(&tenv);
  return exit_code;
}

