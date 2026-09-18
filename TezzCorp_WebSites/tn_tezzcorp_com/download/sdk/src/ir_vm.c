// src/ir_vm.c - IR interpreter (fast VM)
#include "ir_vm.h"
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

static Value v_i64(long long x){ Value v; v.k=V_I64; v.i=x; v.f=0.0; v.p=NULL; v.ty=NULL; return v; }
static Value v_f64(double x){ Value v; v.k=V_F64; v.f=x; v.i=0; v.p=NULL; v.ty=NULL; return v; }
static Value v_ptr(void* p){ Value v; v.k=V_PTR; v.p=p; v.i=0; v.f=0.0; v.ty=NULL; return v; }

static long long val_as_i64(Value v){
  if(v.k==V_I64) return v.i;
  if(v.k==V_F64) return (long long)v.f;
  return (long long)(intptr_t)v.p;
}
static double val_as_f64(Value v){
  if(v.k==V_F64) return v.f;
  if(v.k==V_I64) return (double)v.i;
  return (double)(intptr_t)v.p;
}
static void* val_as_ptr(Value v){
  if(v.k==V_PTR) return v.p;
  return (void*)(intptr_t)v.i;
}

typedef struct {
  unsigned char* base;
  size_t size;
  unsigned char* tags; // one tag per 8 bytes (1 = f64)
} MemBlock;

typedef struct {
  IRModule* M;
  MemBlock* blocks;
  int bcount, bcap;
  void** globals;
  int gcount;
} IRVM;

static void mem_add(IRVM* vm, void* base, size_t size){
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

static void mem_remove(IRVM* vm, void* base){
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

static MemBlock* mem_find(IRVM* vm, void* addr, size_t size, size_t* out_off){
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

static void mem_tag_set(IRVM* vm, void* addr, size_t size, int tag){
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

static void mem_tag_f64_range(IRVM* vm, void* addr, long long count){
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

static void irvm_tag_ai_builtin_outputs(IRVM* vm, const char* name, int nlen, Value* args, int argc, Value* out){
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

static int mem_tag_get(IRVM* vm, void* addr){
  size_t off = 0;
  MemBlock* b = mem_find(vm, addr, 8, &off);
  if(!b) return 0;
  size_t idx = off / 8;
  size_t tn = (b->size + 7) / 8;
  if(idx < tn) return (int)b->tags[idx];
  return 0;
}

static void mem_tag_copy(IRVM* vm, void* dst, void* src, size_t n){
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

static IRFunc* find_func(IRModule* M, const char* name, int nlen){
  if(!M || !name) return NULL;
  for(int i=0;i<M->fn_n;i++){
    IRFunc* f = &M->fns[i];
    if(f->len==nlen && strncmp(f->name, name, (size_t)nlen)==0) return f;
  }
  return NULL;
}

static int find_global(IRModule* M, const char* name, int nlen){
  if(!M || !name) return -1;
  for(int i=0;i<M->global_n;i++){
    IRGlobal* g = &M->globals[i];
    if(g->len==nlen && strncmp(g->name, name, (size_t)nlen)==0) return i;
  }
  return -1;
}

static Value irvm_call(IRVM* vm, IRFunc* fn, Value* args, int argc);
static Value irvm_call_ptr(IRVM* vm, Value callee, Value* args, int argc);

static int irvm_collect_param_regs(IRFunc* fn, int* out, int cap){
  int n = 0;
  if(!fn || !out || cap <= 0) return 0;
  for(int i=0;i<fn->n;i++){
    IRIns* in = &fn->ins[i];
    if(in->op==I_LABEL) break;
    if(in->op==I_STORE){
      if(n < cap) out[n++] = in->b;
      if(n >= fn->param_count) break;
    }
  }
  return n;
}

static int irvm_builtin(IRVM* vm, const char* name, int nlen, Value* args, int argc, Value* out){
  if(nlen==5 && strncmp(name, "async", 5)==0){
    if(argc!=1) return 0;
    Value callee = args[0];
    Value r = irvm_call_ptr(vm, callee, NULL, 0);
    typedef struct { long long result; } AsyncTaskIR;
    AsyncTaskIR* t = (AsyncTaskIR*)xmalloc(sizeof(AsyncTaskIR));
    t->result = val_as_i64(r);
    out->k=V_PTR; out->p=t; out->i=0; out->ty=NULL;
    return 1;
  }
  if(nlen==5 && strncmp(name, "await", 5)==0){
    if(argc!=1) return 0;
    if(args[0].k==V_PTR && args[0].p){
      typedef struct { long long result; } AsyncTaskIR;
      AsyncTaskIR* t = (AsyncTaskIR*)args[0].p;
      long long r = t->result;
      free(t);
      out->k=V_I64; out->i=r; out->p=NULL; out->ty=NULL;
      return 1;
    }
    out->k=V_I64; out->i=val_as_i64(args[0]); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==9 && strncmp(name, "say_multi", 9)==0){
    if(argc!=3) return 0;
    long long* vals = (long long*)val_as_ptr(args[0]);
    long long* tags = (long long*)val_as_ptr(args[1]);
    long long n = val_as_i64(args[2]);
    if(n < 0) n = 0;
    if(n > 1024) n = 1024;
    Value* av = (Value*)xmalloc(sizeof(Value)*(size_t)n);
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
    free(av);
    return ok;
  }
  if(nlen==6 && strncmp(name, "malloc", 6)==0){
    if(argc!=1) return 0;
    long long n = val_as_i64(args[0]);
    if(n < 0) n = 0;
    void* p = malloc((size_t)n);
    if(p) mem_add(vm, p, (size_t)n);
    out->k=V_PTR; out->p=p; out->i=0; out->ty=NULL;
    return 1;
  }
  if(nlen==4 && strncmp(name, "free", 4)==0){
    if(argc!=1) return 0;
    void* p = val_as_ptr(args[0]);
    if(p){
      mem_remove(vm, p);
      free(p);
    }
    out->k=V_I64; out->i=0; out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==6 && strncmp(name, "memcpy", 6)==0){
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
  if(nlen==7 && strncmp(name, "memmove", 7)==0){
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
  if(nlen==6 && strncmp(name, "memcmp", 6)==0){
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
  {
    int ok = builtin_call(name, nlen, args, argc, out);
    if(ok) irvm_tag_ai_builtin_outputs(vm, name, nlen, args, argc, out);
    return ok;
  }
}

static Value irvm_call_by_name(IRVM* vm, const char* name, int nlen, Value* args, int argc){
  IRFunc* fn = find_func(vm->M, name, nlen);
  if(fn) return irvm_call(vm, fn, args, argc);
  Value out;
  if(irvm_builtin(vm, name, nlen, args, argc, &out)) return out;
  {
    char buf[256];
    int m = nlen;
    if(m > 200) m = 200;
    memcpy(buf, name, (size_t)m);
    buf[m] = 0;
    fprintf(stderr, "tezzc error: IR-VM: unknown function call '%s'\n", buf);
    exit(1);
  }
  return v_i64(0);
}

static Value irvm_call_ptr(IRVM* vm, Value callee, Value* args, int argc){
  void* p = val_as_ptr(callee);
  if(p){
    IRFunc* fn = (IRFunc*)p;
    return irvm_call(vm, fn, args, argc);
  }
  die("IR-VM: bad function pointer");
  return v_i64(0);
}

static Value mem_load(IRVM* vm, void* addr, int size, int is_unsigned){
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
  // size >= 8
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

static void mem_store(IRVM* vm, void* addr, int size, Value v){
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
  // size >= 8
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

static Value irvm_call(IRVM* vm, IRFunc* fn, Value* args, int argc){
  int nreg = (fn->max_reg >= 0) ? (fn->max_reg + 1) : fn->next_reg;
  if(nreg <= 0) nreg = 1;
  Value* regs = (Value*)xmalloc(sizeof(Value)*(size_t)nreg);
  for(int i=0;i<nreg;i++) regs[i] = v_i64(0);

  int* preg = NULL;
  int pn = 0;
  if(fn->param_count > 0){
    preg = (int*)xmalloc(sizeof(int)*(size_t)fn->param_count);
    pn = irvm_collect_param_regs(fn, preg, fn->param_count);
  }
  for(int i=0;i<fn->param_count && i<argc;i++){
    int r = (i < pn) ? preg[i] : i;
    if(r >= 0 && r < nreg) regs[r] = args[i];
  }

  // label index
  int max_label = -1;
  for(int i=0;i<fn->n;i++){
    IRIns* in = &fn->ins[i];
    if(in->op==I_LABEL && in->a > max_label) max_label = in->a;
    if(in->op==I_JMP && in->a > max_label) max_label = in->a;
    if(in->op==I_JZ && in->b > max_label) max_label = in->b;
  }
  int* label_idx = NULL;
  if(max_label>=0){
    label_idx = (int*)xmalloc(sizeof(int)*(size_t)(max_label+1));
    for(int i=0;i<=max_label;i++) label_idx[i] = -1;
    for(int i=0;i<fn->n;i++){
      if(fn->ins[i].op==I_LABEL) label_idx[fn->ins[i].a] = i;
    }
  }

  // track allocas for cleanup
  void** allocs = NULL;
  int alloc_n = 0, alloc_cap = 0;

  int ip = 0;
  Value ret = v_i64(0);
  while(ip < fn->n){
    IRIns* in = &fn->ins[ip];
    switch(in->op){
      case I_LABEL:
        ip++;
        break;
      case I_ICONST:
        regs[in->a] = v_i64(in->imm);
        ip++;
        break;
      case I_FCONST: {
        double d = 0.0;
        memcpy(&d, &in->immf, sizeof(double));
        regs[in->a] = v_f64(d);
        ip++;
        break;
      }
      case I_ADDRSYM: {
        int gi = find_global(vm->M, in->name, in->nlen);
        if(gi >= 0 && gi < vm->gcount){
          regs[in->a] = v_ptr(vm->globals[gi]);
          ip++;
          break;
        }
        IRFunc* f = find_func(vm->M, in->name, in->nlen);
        if(f){
          regs[in->a] = v_ptr(f);
          ip++;
          break;
        }
        // unknown symbol -> null ptr
        regs[in->a] = v_ptr(NULL);
        ip++;
        break;
      }
      case I_SCONST:
        if(in->sid >= 0 && in->sid < vm->M->str_n){
          regs[in->a] = v_ptr(vm->M->strs[in->sid].bytes);
        } else {
          regs[in->a] = v_ptr(NULL);
        }
        ip++;
        break;
      case I_MOV:
        regs[in->a] = regs[in->b];
        ip++;
        break;
      case I_BIN: {
        long long a = val_as_i64(regs[in->b]);
        long long b = val_as_i64(regs[in->c]);
        long long r = 0;
        switch(in->binop){
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
        ip++;
        break;
      }
      case I_CMP: {
        long long a = val_as_i64(regs[in->b]);
        long long b = val_as_i64(regs[in->c]);
        long long r = 0;
        switch(in->cmpop){
          case C_EQ:  r = (a==b); break;
          case C_NEQ: r = (a!=b); break;
          case C_LT:  r = (a<b); break;
          case C_LTE: r = (a<=b); break;
          case C_GT:  r = (a>b); break;
          case C_GTE: r = (a>=b); break;
          default: r = 0; break;
        }
        regs[in->a] = v_i64(r);
        ip++;
        break;
      }
      case I_FBIN: {
        double a = val_as_f64(regs[in->b]);
        double b = val_as_f64(regs[in->c]);
        double r = 0.0;
        switch(in->fop){
          case F_ADD: r = a + b; break;
          case F_SUB: r = a - b; break;
          case F_MUL: r = a * b; break;
          case F_DIV: r = b!=0.0 ? (a / b) : 0.0; break;
          default: r = 0.0; break;
        }
        regs[in->a] = v_f64(r);
        ip++;
        break;
      }
      case I_FCMP: {
        double a = val_as_f64(regs[in->b]);
        double b = val_as_f64(regs[in->c]);
        long long r = 0;
        switch(in->cmpop){
          case C_EQ:  r = (a==b); break;
          case C_NEQ: r = (a!=b); break;
          case C_LT:  r = (a<b); break;
          case C_LTE: r = (a<=b); break;
          case C_GT:  r = (a>b); break;
          case C_GTE: r = (a>=b); break;
          default: r = 0; break;
        }
        regs[in->a] = v_i64(r);
        ip++;
        break;
      }
      case I_I2F:
        regs[in->a] = v_f64((double)val_as_i64(regs[in->b]));
        ip++;
        break;
      case I_F2I:
        regs[in->a] = v_i64((long long)val_as_f64(regs[in->b]));
        ip++;
        break;
      case I_CALL: {
        int argc2 = in->argc;
        Value* av = NULL;
        if(argc2 > 0){
          av = (Value*)xmalloc(sizeof(Value)*(size_t)argc2);
          for(int i=0;i<argc2;i++){
            av[i] = regs[in->args[i]];
          }
        }
        Value r = irvm_call_by_name(vm, in->name, in->nlen, av, argc2);
        if(in->a >= 0) regs[in->a] = r;
        if(av) free(av);
        ip++;
        break;
      }
      case I_CALLPTR: {
        int argc2 = in->argc;
        Value* av = NULL;
        if(argc2 > 0){
          av = (Value*)xmalloc(sizeof(Value)*(size_t)argc2);
          for(int i=0;i<argc2;i++){
            av[i] = regs[in->args[i]];
          }
        }
        Value r = irvm_call_ptr(vm, regs[in->b], av, argc2);
        if(in->a >= 0) regs[in->a] = r;
        if(av) free(av);
        ip++;
        break;
      }
      case I_ALLOCA: {
        void* p = xmalloc((size_t)in->size);
        memset(p, 0, (size_t)in->size);
        regs[in->a] = v_ptr(p);
        mem_add(vm, p, (size_t)in->size);
        if(alloc_n == alloc_cap){
          alloc_cap = alloc_cap ? alloc_cap*2 : 16;
          allocs = (void**)xrealloc(allocs, sizeof(void*)*(size_t)alloc_cap);
        }
        allocs[alloc_n++] = p;
        ip++;
        break;
      }
      case I_LOAD: {
        void* p = val_as_ptr(regs[in->b]);
        regs[in->a] = mem_load(vm, p, in->size, in->is_unsigned);
        ip++;
        break;
      }
      case I_STORE: {
        void* p = val_as_ptr(regs[in->a]);
        mem_store(vm, p, in->size, regs[in->b]);
        ip++;
        break;
      }
      case I_GEP: {
        void* p = val_as_ptr(regs[in->b]);
        regs[in->a] = v_ptr((unsigned char*)p + in->imm);
        ip++;
        break;
      }
      case I_JMP:
        if(label_idx && in->a >=0 && in->a <= max_label && label_idx[in->a] >= 0) ip = label_idx[in->a];
        else ip++;
        break;
      case I_JZ: {
        long long v = val_as_i64(regs[in->a]);
        if(v==0 && label_idx && in->b >=0 && in->b <= max_label && label_idx[in->b] >= 0){
          ip = label_idx[in->b];
        } else {
          ip++;
        }
        break;
      }
      case I_RET:
        if(in->a >= 0) ret = regs[in->a];
        ip = fn->n;
        break;
      default:
        ip++;
        break;
    }
  }

  for(int i=0;i<alloc_n;i++){
    mem_remove(vm, allocs[i]);
    free(allocs[i]);
  }
  free(preg);
  free(allocs);
  free(label_idx);
  free(regs);
  return ret;
}

extern void stdlib_set_args(int argc, char** argv);

int ir_vm_run_file(const char* path, int debug, int argc, char** argv){
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

  IRVM vm;
  memset(&vm, 0, sizeof(vm));
  vm.M = M;
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

  IRFunc* main = find_func(M, "main", 4);
  if(!main) err_at2(path, src, 1, 1, "missing fn main()");

  Value out = irvm_call(&vm, main, NULL, 0);
  (void)out;

  // free globals
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

  ir_free(M);
  mg_free(&G);
  tenv_free(&tenv);
  return 0;
}
