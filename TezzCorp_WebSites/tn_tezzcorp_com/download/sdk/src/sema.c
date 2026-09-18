// src/sema.c
#include "sema.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int g_sema_freestanding = 0;
static int g_sema_async_zero = 0;
static const char* g_sema_target = "native";

void sema_set_freestanding(int on){ g_sema_freestanding = on ? 1 : 0; }
void sema_set_async_zero(int on){ g_sema_async_zero = on ? 1 : 0; }
void sema_set_target(const char* target){ g_sema_target = target ? target : "native"; }

static int name_eq(const char* a, int alen, const char* b, int blen){
  return alen==blen && strncmp(a,b,(size_t)alen)==0;
}

static const Attr* find_attr(const Attr* attrs, int n, const char* name, int len){
  if(!attrs || n <= 0) return NULL;
  for(int i=0;i<n;i++){
    if(attrs[i].len==len && strncmp(attrs[i].name, name, (size_t)len)==0){
      return &attrs[i];
    }
  }
  return NULL;
}

typedef enum { PTR_OWN_UNKNOWN=0, PTR_OWN_BORROWED, PTR_OWN_OWNED, PTR_OWN_MOVED } PtrOwn;

typedef struct {
  const char* name;
  int len;
  PtrOwn st;
} PtrVar;

typedef struct {
  PtrVar* vars;
  int n, cap;
} PtrScope;

typedef struct {
  PtrScope* scopes;
  int sn, scap;
} PtrEnv;

typedef enum { BORROW_NONE=0, BORROW_STATIC, BORROW_LOCAL } BorrowKind;

typedef struct {
  const char* name;
  int len;
  BorrowKind st;
} BorrowVar;

typedef struct {
  BorrowVar* vars;
  int n, cap;
} BorrowScope;

typedef struct {
  BorrowScope* scopes;
  int sn, scap;
} BorrowEnv;

static int expr_ptr_var_name(Expr* e, const char** out_name, int* out_len);
static PtrOwn penv_find(PtrEnv* E, const char* name, int len);
static PtrOwn ptr_state_peek(PtrEnv* penv, Expr* e);
static void penv_set(PtrEnv* E, const char* name, int len, PtrOwn st);
static BorrowKind borrow_merge(BorrowKind a, BorrowKind b);
static BorrowKind benv_find(BorrowEnv* E, const char* name, int len);
static int expr_base_name(Expr* e, const char** out_name, int* out_len);

/* -------------------------
 * Builtin signatures
 * ------------------------- */

typedef struct {
  const char* name;
  int nlen;
  int varargs;   // kept for future, but native backend uses fixed checks
  int argc;      // fixed argc when varargs=0
  Type* ret;
  Type* a0;
  Type* a1;
  Type* a2;
  Type* a3;
  Type* a4;
} BuiltinSig;

static Type* ty_ptr(TypeEnv* E, Type* elem){
  return type_ptr(E, elem);
}

/* -------------------------
 * Type helpers (used early)
 * ------------------------- */

static int is_i64(TypeEnv* E, Type* t){ return t==E->b.ty_i64 || (t && t->k==TY_I64); }
static int is_int(TypeEnv* E, Type* t){ (void)E; return type_is_int(t); }
static int is_u8 (TypeEnv* E, Type* t){ return t==E->b.ty_u8  || (t && t->k==TY_U8 ); }
static int is_f64(TypeEnv* E, Type* t){ return t==E->b.ty_f64 || (t && t->k==TY_F64); }
static int is_num(TypeEnv* E, Type* t){ (void)E; return type_is_numeric(t); }
static int is_ptr(Type* t){ return t && t->k==TY_PTR; }
static int is_array(Type* t){ return t && t->k==TY_ARRAY; }

static int kernel_sig_ok(TypeEnv* E, FnDecl* fn){
  if(!fn) return 0;
  if(fn->arity != 1) return 0;
  if(!fn->ptypes || !fn->ptypes[0]) return 0;
  Type* p = fn->ptypes[0];
  if(!p || p->k!=TY_PTR || !p->elem || p->elem->k!=TY_U8) return 0;
  return is_i64(E, fn->ret);
}

static int call_is_name(Expr* e, const char* name, int len){
  if(!e || e->k!=EX_CALL) return 0;
  Expr* c = e->call.callee;
  return c && c->k==EX_NAME && c->name.len==len && strncmp(c->name.name, name, (size_t)len)==0;
}

static int expr_has_call(Expr* e){
  if(!e) return 0;
  switch(e->k){
    case EX_CALL:
      if(call_is_name(e, "gpu_tid", 7)) return 0;
      return 1;
    case EX_BIN: return expr_has_call(e->bin.a) || expr_has_call(e->bin.b);
    case EX_UN: return expr_has_call(e->un.e);
    case EX_INDEX: return expr_has_call(e->index.base) || expr_has_call(e->index.idx);
    case EX_DOT: return expr_has_call(e->dot.base);
    case EX_ADDR: return expr_has_call(e->un.e);
    case EX_DEREF: return expr_has_call(e->un.e);
    case EX_ARRAY_LIT:
      for(int i=0;i<e->arr.n;i++) if(expr_has_call(e->arr.items[i])) return 1;
      return 0;
    case EX_STRUCT_LIT:
      for(int i=0;i<e->stlit.n;i++) if(expr_has_call(e->stlit.v[i])) return 1;
      return 0;
    case EX_CAST: return expr_has_call(e->cast.e);
    case EX_SIZEOF: case EX_ALIGNOF:
      return e->siz.is_type ? 0 : expr_has_call(e->siz.e);
    default: return 0;
  }
}

static int stmt_has_call(Stmt* s){
  if(!s) return 0;
  switch(s->k){
    case ST_BLOCK:
      for(int i=0;i<s->block.n;i++) if(stmt_has_call(s->block.items[i])) return 1;
      return 0;
    case ST_LET:
      return expr_has_call(s->let.init);
    case ST_ASSIGN:
      return expr_has_call(s->asg.lhs) || expr_has_call(s->asg.rhs);
    case ST_EXPR:
      return expr_has_call(s->expr.e);
    case ST_IF:
      return expr_has_call(s->iff.cond) || stmt_has_call(s->iff.thenb) || stmt_has_call(s->iff.elseb);
    case ST_WHILE:
      return expr_has_call(s->wh.cond) || stmt_has_call(s->wh.body);
    case ST_FOR:
      return stmt_has_call(s->fr.init) || expr_has_call(s->fr.cond) || stmt_has_call(s->fr.step) || stmt_has_call(s->fr.body);
    case ST_RET:
      return expr_has_call(s->ret.e);
    case ST_SWITCH:
      if(expr_has_call(s->sw.cond)) return 1;
      for(int i=0;i<s->sw.case_n;i++){
        if(expr_has_call(s->sw.cases[i].val)) return 1;
        if(stmt_has_call(s->sw.cases[i].body)) return 1;
      }
      return 0;
    case ST_UNSAFE:
      return stmt_has_call(s->uns.body);
    default:
      return 0;
  }
}

static int is_stdlib_module(Module* m){
  if(!m || !m->path) return 0;
  const char* p = m->path;
  return strstr(p, "/lib/") != NULL || strstr(p, "\\lib\\") != NULL ||
         strstr(p, "lib/") != NULL || strstr(p, "lib\\") != NULL ||
         strstr(p, "/tools/") != NULL || strstr(p, "\\tools\\") != NULL ||
         strstr(p, "tools/") != NULL || strstr(p, "tools\\") != NULL;
}

static int is_gpu_target(void){
  const char* t = g_sema_target ? g_sema_target : "";
  return strcmp(t, "cuda")==0 || strcmp(t, "metal")==0 ||
         strcmp(t, "vulkan")==0 || strcmp(t, "directml")==0 ||
         strcmp(t, "hlsl")==0 || strcmp(t, "dxil")==0;
}

static int is_forbidden_module_freestanding(Module* m){
  if(!m || !m->mname) return 0;
  const char* n = m->mname;
  int l = m->mlen;
  if(l==2 && strncmp(n,"io",2)==0) return 1;
  if(l==3 && strncmp(n,"net",3)==0) return 1;
  if(l==4 && strncmp(n,"time",4)==0) return 1;
  if(l==3 && strncmp(n,"gpu",3)==0) return 1;
  if(l==3 && strncmp(n,"npu",3)==0) return 1;
  if(l==3 && strncmp(n,"tls",3)==0) return 1;
  return 0;
}

static int builtin_allowed_freestanding(const char* name, int len){
  if(len==6 && strncmp(name,"malloc",6)==0) return 1;
  if(len==4 && strncmp(name,"free",4)==0) return 1;
  if(len==6 && strncmp(name,"memcpy",6)==0) return 1;
  if(len==3 && strncmp(name,"len",3)==0) return 1;
  if(len==6 && strncmp(name,"strcmp",6)==0) return 1;
  if(len==10 && strncmp(name,"bit_popcnt",10)==0) return 1;
  if(len==7 && strncmp(name,"bit_clz",7)==0) return 1;
  if(len==7 && strncmp(name,"bit_ctz",7)==0) return 1;
  if(len==9 && strncmp(name,"bit_bswap",9)==0) return 1;
  if(len==8 && strncmp(name,"bit_rotl",8)==0) return 1;
  if(len==8 && strncmp(name,"bit_rotr",8)==0) return 1;
  if(len==12 && strncmp(name,"cpu_has_sse2",12)==0) return 1;
  if(len==12 && strncmp(name,"cpu_has_avx2",12)==0) return 1;
  if(len==11 && strncmp(name,"cpu_has_fma",11)==0) return 1;
  if(len==12 && strncmp(name,"cpu_has_neon",12)==0) return 1;
  return 0;
}

static int is_unsafe_default(Module* m, FnDecl* f){
  if(f && f->is_unsafe) return 1;
  if(is_stdlib_module(m)) return 1;
  return 0;
}

static void require_unsafe(SrcLoc loc, int in_unsafe){
  if(!in_unsafe) err_at(loc, "unsafe operation requires unsafe block");
}

static void err_borrow_name(SrcLoc loc, const char* msg, const char* name, int len, const char* hint){
  char buf[256];
  if(name && len > 0){
    if(hint && hint[0]){
      snprintf(buf, sizeof(buf), "%s '%.*s' (%s)", msg, len, name, hint);
    } else {
      snprintf(buf, sizeof(buf), "%s '%.*s'", msg, len, name);
    }
    err_at(loc, buf);
  } else {
    if(hint && hint[0]){
      snprintf(buf, sizeof(buf), "%s (%s)", msg, hint);
      err_at(loc, buf);
    } else {
      err_at(loc, msg);
    }
  }
}

static void err_borrow_expr(SrcLoc loc, const char* msg, Expr* e, const char* hint){
  const char* nm = NULL;
  int nlen = 0;
  if(e && expr_base_name(e, &nm, &nlen)){
    err_borrow_name(loc, msg, nm, nlen, hint);
    return;
  }
  err_borrow_name(loc, msg, NULL, 0, hint);
}

static void err_name_at(SrcLoc loc, const char* prefix, const char* name, int len){
  char buf[256];
  if(name && len > 0){
    snprintf(buf, sizeof(buf), "%s '%.*s'", prefix, len, name);
    err_at(loc, buf);
  }
  err_at(loc, prefix);
}

static void err_unknown_name_expr(Expr* e){
  if(e && e->k==EX_NAME){
    err_name_at(e->loc, "unknown name", e->name.name, e->name.len);
  }
  err_at(e ? e->loc : (SrcLoc){0}, "expression has no type");
}

static void err_arity_at(SrcLoc loc, const char* context, const char* name, int len, int expected, int got){
  char buf[256];
  if(name && len > 0){
    snprintf(buf, sizeof(buf), "%s to '%.*s' (expected %d, got %d)", context, len, name, expected, got);
  } else {
    snprintf(buf, sizeof(buf), "%s (expected %d, got %d)", context, expected, got);
  }
  err_at(loc, buf);
}

static int is_ptr_to_u8(TypeEnv* E, Type* t){
  return t && t->k==TY_PTR && t->elem && is_u8(E, t->elem);
}

static int is_u8_array(TypeEnv* E, Type* t){
  (void)E;
  return t && t->k==TY_ARRAY && t->elem && t->elem->k==TY_U8;
}

static int expr_is_stringy(TypeEnv* E, Expr* e){
  if(!e) return 0;
  if(e->k==EX_STR) return 1;
  if(e->ty && is_ptr_to_u8(E, e->ty)) return 1;
  if(e->ty && is_u8_array(E, e->ty)) return 1;
  return 0;
}

static int module_imports_name(Module* m, const char* name, int len){
  if(!m || !name || len <= 0) return 0;
  for(int i=0;i<m->import_n;i++){
    const char* s = m->imports[i];
    if(!s) continue;
    int slen = (int)strlen(s);
    if(slen == len && strncmp(s, name, (size_t)len)==0) return 1;
    const char* base = s;
    for(const char* p=s; *p; p++){
      if(*p=='/' || *p=='\\') base = p + 1;
    }
    int blen = (int)strlen(base);
    if(blen >= 3 && base[blen-3]=='.' && base[blen-2]=='t' && base[blen-1]=='n'){
      blen -= 3;
    }
    if(blen == len && strncmp(base, name, (size_t)len)==0) return 1;
  }
  return 0;
}

static int fn_has_attr(FnDecl* f, const char* name, int len){
  if(!f) return 0;
  return find_attr(f->attrs, f->attr_n, name, len) != NULL;
}
static const Attr* fn_get_attr(FnDecl* f, const char* name, int len){
  if(!f) return NULL;
  return find_attr(f->attrs, f->attr_n, name, len);
}

static void validate_fn_attrs(FnDecl* fn){
  if(!fn) return;
  const Attr* abi = fn_get_attr(fn, "abi", 3);
  if(abi){
    if(!abi->arg || abi->arg_len <= 0) err_at(fn->loc, "@abi requires string argument");
    if(!fn->is_extern) err_at(fn->loc, "@abi only valid on extern fn");
  }
  if(fn_has_attr(fn, "inline", 6)){
    if(fn->is_extern) err_at(fn->loc, "@inline not allowed on extern fn");
  }
}

static void validate_decl_attrs(const Attr* attrs, int n, SrcLoc loc){
  if(!attrs || n <= 0) return;
  if(find_attr(attrs, n, "inline", 6)) err_at(loc, "@inline only valid on fn");
  if(find_attr(attrs, n, "abi", 3)) err_at(loc, "@abi only valid on extern fn");
  if(find_attr(attrs, n, "borrow", 6)) err_at(loc, "@borrow only valid on fn");
  if(find_attr(attrs, n, "mut", 3)) err_at(loc, "@mut only valid on fn");
}

static int abi_c_type_ok(Type* t, int in_struct){
  if(!t) return 0;
  switch(t->k){
    case TY_F64:
      return 1;
    case TY_VOID:
      return in_struct ? 0 : 1;
    case TY_PTR:
      return 1; // allow pointers (including fn pointers)
    case TY_ARRAY:
      return in_struct ? abi_c_type_ok(t->elem, 1) : 0;
    case TY_STRUCT:
      if(t->incomplete) return 0;
      for(int i=0;i<t->field_n;i++){
        if(!abi_c_type_ok(t->fields[i].ty, 1)) return 0;
      }
      return 1;
    case TY_FN:
      return 0;
    default:
      return type_is_int(t);
  }
}

static void validate_abi_fn(FnDecl* fn){
  if(!fn) return;
  int use_abi = fn->is_extern || fn_has_attr(fn, "abi", 3);
  if(!use_abi) return;

  if(fn->ret && fn->ret->k==TY_VOID){
    // ok
  } else if(!abi_c_type_ok(fn->ret, 0)){
    err_at(fn->loc, "extern/ABI fn has non-ABI-safe return type");
  }

  for(int i=0;i<fn->arity;i++){
    Type* pt = fn->ptypes[i];
    if(pt && pt->k==TY_VOID){
      err_at(fn->loc, "extern/ABI fn parameter cannot be void");
    }
    if(!abi_c_type_ok(pt, 0)){
      err_at(fn->loc, "extern/ABI fn parameter type not ABI-safe (use pointers/structs)");
    }
  }
}

static void apply_ptr_call_rules(PtrEnv* penv, FnDecl* f, Expr** args, int argc, int in_unsafe, SrcLoc loc){
  if(!penv || !f || !args || argc<=0) return;
  int borrow = fn_has_attr(f, "borrow", 6);
  int mut = fn_has_attr(f, "mut", 3);
  // default to borrowing for pointer args unless explicitly marked mut/borrow
  int move = 0;
  if(!borrow && !mut) borrow = 1;

  const char** seen_name = NULL;
  int* seen_len = NULL;
  int seen_n = 0;
    if(mut){
      seen_name = (const char**)malloc(sizeof(char*)*(size_t)argc);
      seen_len = (int*)malloc(sizeof(int)*(size_t)argc);
      if(!seen_name || !seen_len) die("out of memory");
    }

    for(int i=0;i<argc;i++){
      if(move){
        PtrOwn pst = ptr_state_peek(penv, args[i]);
        if(pst == PTR_OWN_BORROWED && !in_unsafe){
          err_borrow_expr(loc, "cannot move borrowed pointer", args[i], "use @borrow/@mut or move into an owned pointer");
        }
      }
      const char* nm = NULL;
      int nlen = 0;
      if(!expr_ptr_var_name(args[i], &nm, &nlen)) continue;

    if(mut){
      for(int j=0;j<seen_n;j++){
        if(seen_len[j]==nlen && strncmp(seen_name[j], nm, (size_t)nlen)==0){
          if(!in_unsafe) err_borrow_name(loc, "mut borrow requires unique pointer arguments", nm, nlen, "pass distinct pointers or use unsafe");
        }
      }
      seen_name[seen_n] = nm;
      seen_len[seen_n] = nlen;
      seen_n++;
    }

    if(move){
      PtrOwn st = penv_find(penv, nm, nlen);
      if(st == PTR_OWN_BORROWED && !in_unsafe){
        err_borrow_name(loc, "cannot move borrowed pointer", nm, nlen, "use @borrow/@mut or move into an owned pointer");
      }
      if(st == PTR_OWN_OWNED){
        penv_set(penv, nm, nlen, PTR_OWN_MOVED);
      }
    }
  }

  if(seen_name) free(seen_name);
  if(seen_len) free(seen_len);
}

static int expr_ptr_var_name(Expr* e, const char** out_name, int* out_len){
  if(!e) return 0;
  while(e && e->k==EX_CAST) e = e->cast.e;
  if(!e || e->k!=EX_NAME) return 0;
  if(!e->ty || e->ty->k!=TY_PTR) return 0;
  if(out_name) *out_name = e->name.name;
  if(out_len) *out_len = e->name.len;
  return 1;
}

static int expr_is_malloc_call(Expr* e){
  if(!e || e->k!=EX_CALL) return 0;
  if(e->call.callee && e->call.callee->k==EX_NAME){
    const char* n = e->call.callee->name.name;
    int len = e->call.callee->name.len;
    if(len==6 && strncmp(n,"malloc",6)==0) return 1;
  }
  return 0;
}

static PtrOwn ptr_state_peek(PtrEnv* penv, Expr* e){
  if(!e) return PTR_OWN_UNKNOWN;
  while(e && e->k==EX_CAST) e = e->cast.e;
  if(!e) return PTR_OWN_UNKNOWN;
  if(e->k==EX_ADDR || e->k==EX_STR) return PTR_OWN_BORROWED;
  if(e->k==EX_NAME && e->ty && e->ty->k==TY_ARRAY) return PTR_OWN_BORROWED;
  if(expr_is_malloc_call(e)) return PTR_OWN_OWNED;
  const char* nm = NULL;
  int nlen = 0;
  if(expr_ptr_var_name(e, &nm, &nlen)){
    return penv ? penv_find(penv, nm, nlen) : PTR_OWN_UNKNOWN;
  }
  return PTR_OWN_UNKNOWN;
}

static int expr_is_static_borrow(Expr* e){
  if(!e) return 0;
  while(e && e->k==EX_CAST) e = e->cast.e;
  if(!e) return 0;
  return e->k==EX_STR;
}

static BorrowKind borrow_kind_from_expr(BorrowEnv* benv, PtrEnv* penv, Expr* e){
  if(!e) return BORROW_NONE;
  while(e && e->k==EX_CAST) e = e->cast.e;
  if(!e) return BORROW_NONE;

  if(expr_is_static_borrow(e)) return BORROW_STATIC;

  if(e->ty && e->ty->k==TY_PTR){
    PtrOwn st = ptr_state_peek(penv, e);
    if(st == PTR_OWN_BORROWED) return BORROW_LOCAL;
  }

  switch(e->k){
    case EX_NAME:
      return benv ? benv_find(benv, e->name.name, e->name.len) : BORROW_NONE;
    case EX_DOT:
      return borrow_kind_from_expr(benv, penv, e->dot.base);
    case EX_INDEX:
      return borrow_kind_from_expr(benv, penv, e->index.base);
    case EX_ARRAY_LIT: {
      BorrowKind out = BORROW_NONE;
      for(int i=0;i<e->arr.n;i++){
        out = borrow_merge(out, borrow_kind_from_expr(benv, penv, e->arr.items[i]));
        if(out == BORROW_LOCAL) return out;
      }
      return out;
    }
    case EX_STRUCT_LIT: {
      BorrowKind out = BORROW_NONE;
      for(int i=0;i<e->stlit.n;i++){
        out = borrow_merge(out, borrow_kind_from_expr(benv, penv, e->stlit.v[i]));
        if(out == BORROW_LOCAL) return out;
      }
      return out;
    }
    default:
      return BORROW_NONE;
  }
}

static int expr_base_name(Expr* e, const char** out_name, int* out_len){
  if(!e) return 0;
  while(e && e->k==EX_CAST) e = e->cast.e;
  if(!e) return 0;
  if(e->k==EX_NAME){
    if(out_name) *out_name = e->name.name;
    if(out_len) *out_len = e->name.len;
    return 1;
  }
  if(e->k==EX_DOT) return expr_base_name(e->dot.base, out_name, out_len);
  if(e->k==EX_INDEX) return expr_base_name(e->index.base, out_name, out_len);
  return 0;
}

static PtrOwn ptr_state_from_expr(PtrEnv* penv, Expr* e){
  PtrOwn st = ptr_state_peek(penv, e);
  if(st == PTR_OWN_OWNED){
    const char* nm = NULL;
    int nlen = 0;
    if(expr_ptr_var_name(e, &nm, &nlen)){
      if(penv) penv_set(penv, nm, nlen, PTR_OWN_MOVED);
    }
  }
  return st;
}

static int flatten_mod_name(Expr* e, char** out, int* out_len){
  if(!e || !out || !out_len) return 0;
  if(e->k==EX_NAME){
    char* s = (char*)malloc((size_t)e->name.len + 1);
    if(!s) die("out of memory");
    memcpy(s, e->name.name, (size_t)e->name.len);
    s[e->name.len] = 0;
    *out = s;
    *out_len = e->name.len;
    return 1;
  }
  if(e->k==EX_DOT){
    char* left = NULL;
    int llen = 0;
    if(!flatten_mod_name(e->dot.base, &left, &llen)) return 0;
    int rlen = e->dot.memlen;
    char* s = (char*)malloc((size_t)llen + 1 + (size_t)rlen + 1);
    if(!s) die("out of memory");
    memcpy(s, left, (size_t)llen);
    s[llen] = '.';
    memcpy(s + llen + 1, e->dot.mem, (size_t)rlen);
    s[llen + 1 + rlen] = 0;
    free(left);
    *out = s;
    *out_len = llen + 1 + rlen;
    return 1;
  }
  return 0;
}

/* -------------------------
 * Builtin registry
 * ------------------------- */

static int is_builtin_name(const char* name, int len){
  static const struct { const char* s; int n; } b[] = {
    {"say",3},
    {"say_str",7},
    {"say_f",5},
    {"len",3},
    {"bit_popcnt",10},
    {"bit_clz",7},
    {"bit_ctz",7},
    {"bit_bswap",9},
    {"bit_rotl",8},
    {"bit_rotr",8},
    {"cpu_has_sse2",12},
    {"cpu_has_avx2",12},
    {"cpu_has_fma",11},
    {"cpu_has_neon",12},
    {"strcmp",6},
    {"memcpy",6},
    {"memmove",7},
    {"memcmp",6},
    {"malloc",6},
    {"free",4},
    {"input_line",10},
    {"input_i64",9},
    {"fopen",5},
    {"fclose",6},
    {"fflush",6},
    {"fread",5},
    {"fwrite",6},
    {"fseek",5},
    {"ftell",5},
    {"file_size",9},
    {"read_file",9},
    {"write_file",10},
    {"read_line",9},
    {"read_bytes",10},
    {"write_line",10},
    {"io_err",6},
    {"io_eof",6},
    {"stdin",5},
    {"stdout",6},
    {"stderr",6},
    {"get_cwd",7},
    {"chdir",5},
    {"mkdir",5},
    {"rmdir",5},
    {"remove",6},
    {"rename",6},
    {"path_exists",11},
    {"path_is_dir",11},
    {"path_join",9},
    {"path_sep",8},
    {"path_basename",13},
    {"path_dirname",12},
    {"list_dir",8},
    {"os_name",7},
    {"list_dir_recursive",18},
    {"glob",4},
    {"path_normalize",14},
    {"path_normalize_opts",19},
    {"time_now",8},
    {"time_now_ms",11},
    {"time_now_ns",11},
    {"date_now",8},
    {"date_now_utc",12},
    {"sleep_ms",8},
    {"async",5},
    {"await",5},
    {"async_workers",13},
    {"async_pending",13},
    {"net_af_inet",11},
    {"net_af_inet6",12},
    {"net_sock_stream",15},
    {"net_sock_dgram",14},
    {"net_ipproto_tcp",15},
    {"net_ipproto_udp",15},
    {"net_init",8},
    {"net_cleanup",11},
    {"net_socket",10},
    {"net_close",9},
    {"net_connect",11},
    {"net_bind",8},
    {"net_listen",10},
    {"net_accept",10},
    {"net_send",8},
    {"net_recv",8},
    {"net_set_blocking",16},
    {"net_set_timeout",15},
    {"net_last_error",14},
    {"net_resolve",11},
    {"tls_connect",11},
    {"tls_connect_ex",14},
    {"tls_send",8},
    {"tls_recv",8},
    {"tls_close",9},
    {"tls_last_error",14},
    {"tls_policy_reset",16},
    {"tls_policy_set_min",18},
    {"tls_policy_set_pin_sha256",25},
    {"tls_policy_set_handshake_timeout",32},
    {"tls_policy_get_min",18},
    {"tls_policy_get_handshake_timeout",32},
    {"tls_policy_pin_enabled",22},
    {"input",5},
    {"args_count",10},
    {"arg",3},
    {"proc_run",8},
    {"proc_out",8},
    {"http_download",13},
    {"gpu_launch_dxil_1d",18},
    {"npu_launch_dxil_1d",18},
    {"gpu_backend_name",16},
    {"gpu_supports_dxil",19},
    {"npu_backend_name",16},
    {"npu_supports_dxil",19},
    {"npu_model_load",14},
    {"npu_model_free",14},
    {"npu_model_run_f64_1d",20},
    {"gpu_tid",7},
    {"io_mmap_open",12},
    {"io_mmap_close",13},
    {"io_pipe_create",14},
    {"io_fd_read",10},
    {"io_fd_write",11},
    {"io_fd_close",11},
  };
  for(int i=0;i<(int)(sizeof(b)/sizeof(b[0]));i++){
    if(len==b[i].n && strncmp(name,b[i].s,(size_t)len)==0) return 1;
  }
  return 0;
}

static BuiltinSig builtin_sig(TypeEnv* E, const char* name, int len){
  Type* I   = E->b.ty_i64;
  Type* U8  = E->b.ty_u8;
  Type* PU8 = ty_ptr(E, U8);

  BuiltinSig s; memset(&s,0,sizeof(s));
  s.name = name; s.nlen=len;

  /* IMPORTANT:
   * Native backend has a C runtime say(i64) and say_str(*u8).
   * So here we keep say as "special lowered" in sema_call()
   * and do NOT rely on varargs in production.
   */
  if(len==3 && strncmp(name,"say",3)==0){
    s.varargs = 1;
    s.ret = I;
    return s;
  }

  if(len==7 && strncmp(name,"say_str",7)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }

  if(len==5 && strncmp(name,"say_f",5)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = E->b.ty_f64;
    return s;
  }

  if(len==3 && strncmp(name,"len",3)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }

  if(len==10 && strncmp(name,"bit_popcnt",10)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = I;
    return s;
  }

  if(len==7 && strncmp(name,"bit_clz",7)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = I;
    return s;
  }

  if(len==7 && strncmp(name,"bit_ctz",7)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = I;
    return s;
  }

  if(len==9 && strncmp(name,"bit_bswap",9)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = I;
    return s;
  }

  if(len==8 && strncmp(name,"bit_rotl",8)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = I; s.a0 = I; s.a1 = I;
    return s;
  }

  if(len==8 && strncmp(name,"bit_rotr",8)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = I; s.a0 = I; s.a1 = I;
    return s;
  }

  if(len==12 && strncmp(name,"cpu_has_sse2",12)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==12 && strncmp(name,"cpu_has_avx2",12)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==11 && strncmp(name,"cpu_has_fma",11)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==12 && strncmp(name,"cpu_has_neon",12)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==7 && strncmp(name,"gpu_tid",7)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==12 && strncmp(name,"io_mmap_open",12)==0){
    s.varargs = 0; s.argc = 3;
    s.ret = PU8; s.a0 = PU8; s.a1 = ty_ptr(E, I); s.a2 = ty_ptr(E, I);
    return s;
  }

  if(len==13 && strncmp(name,"io_mmap_close",13)==0){
    s.varargs = 0; s.argc = 3;
    s.ret = I; s.a0 = PU8; s.a1 = I; s.a2 = I;
    return s;
  }

  if(len==14 && strncmp(name,"io_pipe_create",14)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = I; s.a0 = ty_ptr(E, I); s.a1 = ty_ptr(E, I);
    return s;
  }

  if(len==10 && strncmp(name,"io_fd_read",10)==0){
    s.varargs = 0; s.argc = 3;
    s.ret = I; s.a0 = I; s.a1 = PU8; s.a2 = I;
    return s;
  }

  if(len==11 && strncmp(name,"io_fd_write",11)==0){
    s.varargs = 0; s.argc = 3;
    s.ret = I; s.a0 = I; s.a1 = PU8; s.a2 = I;
    return s;
  }

  if(len==11 && strncmp(name,"io_fd_close",11)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = I;
    return s;
  }

  if(len==6 && strncmp(name,"strcmp",6)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = I; s.a0 = PU8; s.a1 = PU8;
    return s;
  }

  if(len==6 && strncmp(name,"memcpy",6)==0){
    s.varargs = 0; s.argc = 3;
    // memcpy(dst:*u8, src:*u8, n:i64) -> *u8
    s.ret = PU8; s.a0 = PU8; s.a1 = PU8; s.a2 = I;
    return s;
  }
  if(len==7 && strncmp(name,"memmove",7)==0){
    s.varargs = 0; s.argc = 3;
    // memmove(dst:*u8, src:*u8, n:i64) -> *u8
    s.ret = PU8; s.a0 = PU8; s.a1 = PU8; s.a2 = I;
    return s;
  }
  if(len==6 && strncmp(name,"memcmp",6)==0){
    s.varargs = 0; s.argc = 3;
    // memcmp(a:*u8, b:*u8, n:i64) -> i64
    s.ret = I; s.a0 = PU8; s.a1 = PU8; s.a2 = I;
    return s;
  }

  if(len==6 && strncmp(name,"malloc",6)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = PU8; s.a0 = I;
    return s;
  }

  if(len==4 && strncmp(name,"free",4)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }

  if(len==10 && strncmp(name,"input_line",10)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = PU8;
    return s;
  }

  if(len==9 && strncmp(name,"input_i64",9)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==5 && strncmp(name,"fopen",5)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = PU8; s.a0 = PU8; s.a1 = PU8;
    return s;
  }

  if(len==6 && strncmp(name,"fclose",6)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }

  if(len==6 && strncmp(name,"fflush",6)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }

  if(len==5 && strncmp(name,"fread",5)==0){
    s.varargs = 0; s.argc = 4;
    s.ret = I; s.a0 = PU8; s.a1 = I; s.a2 = I; s.a3 = PU8;
    return s;
  }

  if(len==6 && strncmp(name,"fwrite",6)==0){
    s.varargs = 0; s.argc = 4;
    s.ret = I; s.a0 = PU8; s.a1 = I; s.a2 = I; s.a3 = PU8;
    return s;
  }

  if(len==5 && strncmp(name,"fseek",5)==0){
    s.varargs = 0; s.argc = 3;
    s.ret = I; s.a0 = PU8; s.a1 = I; s.a2 = I;
    return s;
  }

  if(len==5 && strncmp(name,"ftell",5)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }

  if(len==9 && strncmp(name,"file_size",9)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }

  if(len==9 && strncmp(name,"read_file",9)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = PU8; s.a0 = PU8;
    return s;
  }

  if(len==10 && strncmp(name,"write_file",10)==0){
    s.varargs = 0; s.argc = 3;
    s.ret = I; s.a0 = PU8; s.a1 = PU8; s.a2 = I;
    return s;
  }

  if(len==9 && strncmp(name,"read_line",9)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = PU8; s.a0 = PU8;
    return s;
  }

  if(len==10 && strncmp(name,"read_bytes",10)==0){
    s.varargs = 0; s.argc = 3;
    s.ret = I; s.a0 = PU8; s.a1 = PU8; s.a2 = I;
    return s;
  }

  if(len==10 && strncmp(name,"write_line",10)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = I; s.a0 = PU8; s.a1 = PU8;
    return s;
  }

  if(len==6 && strncmp(name,"io_err",6)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==6 && strncmp(name,"io_eof",6)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }

  if(len==5 && strncmp(name,"stdin",5)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = PU8;
    return s;
  }

  if(len==6 && strncmp(name,"stdout",6)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = PU8;
    return s;
  }

  if(len==6 && strncmp(name,"stderr",6)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = PU8;
    return s;
  }

  if(len==7 && strncmp(name,"get_cwd",7)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = PU8;
    return s;
  }

  if(len==5 && strncmp(name,"chdir",5)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }

  if(len==5 && strncmp(name,"mkdir",5)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }

  if(len==5 && strncmp(name,"rmdir",5)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }

  if(len==6 && strncmp(name,"remove",6)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }

  if(len==6 && strncmp(name,"rename",6)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = I; s.a0 = PU8; s.a1 = PU8;
    return s;
  }

  if(len==11 && strncmp(name,"path_exists",11)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }

  if(len==11 && strncmp(name,"path_is_dir",11)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }

  if(len==9 && strncmp(name,"path_join",9)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = PU8; s.a0 = PU8; s.a1 = PU8;
    return s;
  }

  if(len==8 && strncmp(name,"path_sep",8)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==13 && strncmp(name,"path_basename",13)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = PU8; s.a0 = PU8;
    return s;
  }

  if(len==12 && strncmp(name,"path_dirname",12)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = PU8; s.a0 = PU8;
    return s;
  }

  if(len==8 && strncmp(name,"list_dir",8)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = PU8; s.a0 = PU8;
    return s;
  }

  if(len==7 && strncmp(name,"os_name",7)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = PU8;
    return s;
  }

  if(len==18 && strncmp(name,"list_dir_recursive",18)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = PU8; s.a0 = PU8;
    return s;
  }

  if(len==4 && strncmp(name,"glob",4)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = PU8; s.a0 = PU8;
    return s;
  }

  if(len==14 && strncmp(name,"path_normalize",14)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = PU8; s.a0 = PU8;
    return s;
  }

  if(len==19 && strncmp(name,"path_normalize_opts",19)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = PU8; s.a0 = PU8; s.a1 = I;
    return s;
  }

  if(len==8 && strncmp(name,"time_now",8)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==11 && strncmp(name,"time_now_ms",11)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==11 && strncmp(name,"time_now_ns",11)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==8 && strncmp(name,"date_now",8)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = PU8;
    return s;
  }

  if(len==12 && strncmp(name,"date_now_utc",12)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = PU8;
    return s;
  }

  if(len==8 && strncmp(name,"sleep_ms",8)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = I;
    return s;
  }

  if(len==5 && strncmp(name,"async",5)==0){
    Type* fn0 = type_fn(E, I, NULL, 0);
    Type* pfn0 = type_ptr(E, fn0);
    s.varargs = 0; s.argc = 1;
    s.ret = g_sema_async_zero ? I : PU8; s.a0 = pfn0;
    return s;
  }

  if(len==5 && strncmp(name,"await",5)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = g_sema_async_zero ? I : PU8;
    return s;
  }

  if(len==13 && strncmp(name,"async_workers",13)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==13 && strncmp(name,"async_pending",13)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==11 && strncmp(name,"net_af_inet",11)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==12 && strncmp(name,"net_af_inet6",12)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==15 && strncmp(name,"net_sock_stream",15)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==14 && strncmp(name,"net_sock_dgram",14)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==15 && strncmp(name,"net_ipproto_tcp",15)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==15 && strncmp(name,"net_ipproto_udp",15)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==8 && strncmp(name,"net_init",8)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==11 && strncmp(name,"net_cleanup",11)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==10 && strncmp(name,"net_socket",10)==0){
    s.varargs = 0; s.argc = 3;
    s.ret = I; s.a0 = I; s.a1 = I; s.a2 = I;
    return s;
  }
  if(len==9 && strncmp(name,"net_close",9)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = I;
    return s;
  }
  if(len==11 && strncmp(name,"net_connect",11)==0){
    s.varargs = 0; s.argc = 3;
    s.ret = I; s.a0 = I; s.a1 = PU8; s.a2 = I;
    return s;
  }
  if(len==8 && strncmp(name,"net_bind",8)==0){
    s.varargs = 0; s.argc = 3;
    s.ret = I; s.a0 = I; s.a1 = PU8; s.a2 = I;
    return s;
  }
  if(len==10 && strncmp(name,"net_listen",10)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = I; s.a0 = I; s.a1 = I;
    return s;
  }
  if(len==10 && strncmp(name,"net_accept",10)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = I;
    return s;
  }
  if(len==8 && strncmp(name,"net_send",8)==0){
    s.varargs = 0; s.argc = 3;
    s.ret = I; s.a0 = I; s.a1 = PU8; s.a2 = I;
    return s;
  }
  if(len==8 && strncmp(name,"net_recv",8)==0){
    s.varargs = 0; s.argc = 3;
    s.ret = I; s.a0 = I; s.a1 = PU8; s.a2 = I;
    return s;
  }
  if(len==16 && strncmp(name,"net_set_blocking",16)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = I; s.a0 = I; s.a1 = I;
    return s;
  }
  if(len==15 && strncmp(name,"net_set_timeout",15)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = I; s.a0 = I; s.a1 = I;
    return s;
  }
  if(len==14 && strncmp(name,"net_last_error",14)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==11 && strncmp(name,"net_resolve",11)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = PU8; s.a0 = PU8; s.a1 = I;
    return s;
  }

  if(len==11 && strncmp(name,"tls_connect",11)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = I; s.a0 = PU8; s.a1 = I;
    return s;
  }
  if(len==14 && strncmp(name,"tls_connect_ex",14)==0){
    s.varargs = 0; s.argc = 5;
    s.ret = I; s.a0 = PU8; s.a1 = I; s.a2 = I; s.a3 = I; s.a4 = PU8;
    return s;
  }
  if(len==8 && strncmp(name,"tls_send",8)==0){
    s.varargs = 0; s.argc = 3;
    s.ret = I; s.a0 = I; s.a1 = PU8; s.a2 = I;
    return s;
  }
  if(len==8 && strncmp(name,"tls_recv",8)==0){
    s.varargs = 0; s.argc = 3;
    s.ret = I; s.a0 = I; s.a1 = PU8; s.a2 = I;
    return s;
  }
  if(len==9 && strncmp(name,"tls_close",9)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = I;
    return s;
  }
  if(len==14 && strncmp(name,"tls_last_error",14)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==16 && strncmp(name,"tls_policy_reset",16)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==18 && strncmp(name,"tls_policy_set_min",18)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = I;
    return s;
  }
  if(len==25 && strncmp(name,"tls_policy_set_pin_sha256",25)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }
  if(len==32 && strncmp(name,"tls_policy_set_handshake_timeout",32)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = I;
    return s;
  }
  if(len==18 && strncmp(name,"tls_policy_get_min",18)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==32 && strncmp(name,"tls_policy_get_handshake_timeout",32)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==22 && strncmp(name,"tls_policy_pin_enabled",22)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }

  if(len==18 && strncmp(name,"gpu_launch_dxil_1d",18)==0){
    s.varargs = 0; s.argc = 4;
    s.ret = I; s.a0 = PU8; s.a1 = I; s.a2 = I; s.a3 = PU8;
    return s;
  }
  if(len==18 && strncmp(name,"npu_launch_dxil_1d",18)==0){
    s.varargs = 0; s.argc = 4;
    s.ret = I; s.a0 = PU8; s.a1 = I; s.a2 = I; s.a3 = PU8;
    return s;
  }
  if(len==16 && strncmp(name,"gpu_backend_name",16)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = PU8;
    return s;
  }
  if(len==19 && strncmp(name,"gpu_supports_dxil",19)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==16 && strncmp(name,"npu_backend_name",16)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = PU8;
    return s;
  }
  if(len==19 && strncmp(name,"npu_supports_dxil",19)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==14 && strncmp(name,"npu_model_load",14)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }
  if(len==14 && strncmp(name,"npu_model_free",14)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = I;
    return s;
  }
  if(len==20 && strncmp(name,"npu_model_run_f64_1d",20)==0){
    s.varargs = 0; s.argc = 5;
    s.ret = I; s.a0 = I; s.a1 = ty_ptr(E, E->b.ty_f64); s.a2 = I; s.a3 = ty_ptr(E, E->b.ty_f64); s.a4 = I;
    return s;
  }

  if(len==10 && strncmp(name,"args_count",10)==0){
    s.varargs = 0; s.argc = 0;
    s.ret = I;
    return s;
  }
  if(len==3 && strncmp(name,"arg",3)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = PU8; s.a0 = I;
    return s;
  }
  if(len==8 && strncmp(name,"proc_run",8)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = I; s.a0 = PU8;
    return s;
  }
  if(len==8 && strncmp(name,"proc_out",8)==0){
    s.varargs = 0; s.argc = 1;
    s.ret = PU8; s.a0 = PU8;
    return s;
  }
  if(len==13 && strncmp(name,"http_download",13)==0){
    s.varargs = 0; s.argc = 2;
    s.ret = I; s.a0 = PU8; s.a1 = PU8;
    return s;
  }

  s.ret = I;
  return s;
}

/* -------------------------
 * Module / fn lookup
 * ------------------------- */

static FnDecl* find_fn_in_module(Module* M, const char* name, int len){
  for(int i=0;i<M->fn_n;i++){
    FnDecl* f = M->fns[i];
    if(name_eq(f->name, f->len, name, len)) return f;
  }
  return NULL;
}

static FnDecl* find_fn_any(ModuleGraph* G, Module* curM, const char* name, int len){
  FnDecl* f = find_fn_in_module(curM, name, len);
  if(f) return f;
  if(G){
    for(int i=0;i<G->mod_n;i++){
      Module* m = G->mods[i];
      if(m==curM) continue;
      f = find_fn_in_module(m, name, len);
      if(f) return f;
    }
  }
  return NULL;
}

static Module* find_module_by_mname(ModuleGraph* G, const char* mname, int mlen){
  for(int i=0;i<G->mod_n;i++){
    Module* M = G->mods[i];
    if(name_eq(M->mname, M->mlen, mname, mlen)) return M;
  }
  return NULL;
}

/* -------------------------
 * Typed local env (scopes)
 * ------------------------- */

typedef struct {
  const char* name;
  int len;
  Type* ty;
} VarTy;

typedef struct {
  VarTy* vars;
  int n, cap;
} TyScope;

typedef struct {
  TyScope* scopes;
  int sn, scap;
} TyEnv;

static void tenv_push(TyEnv* E){
  if(E->sn==E->scap){
    E->scap = E->scap? E->scap*2 : 16;
    E->scopes = (TyScope*)realloc(E->scopes, sizeof(TyScope)*(size_t)E->scap);
    if(!E->scopes) die("out of memory");
  }
  TyScope* S = &E->scopes[E->sn++];
  memset(S,0,sizeof(*S));
}
static void tenv_pop(TyEnv* E){
  if(E->sn<=0) return;
  TyScope* S = &E->scopes[--E->sn];
  free(S->vars);
  memset(S,0,sizeof(*S));
}
static void tenv_add_var(TyEnv* E, const char* name, int len, Type* ty, SrcLoc loc){
  if(E->sn<=0) tenv_push(E);
  TyScope* S = &E->scopes[E->sn-1];

  for(int i=0;i<S->n;i++){
    if(S->vars[i].len==len && strncmp(S->vars[i].name,name,(size_t)len)==0){
      err_at(loc, "duplicate variable name in same scope");
    }
  }

  if(S->n==S->cap){
    S->cap = S->cap? S->cap*2 : 32;
    S->vars = (VarTy*)realloc(S->vars, sizeof(VarTy)*(size_t)S->cap);
    if(!S->vars) die("out of memory");
  }
  S->vars[S->n++] = (VarTy){name,len,ty};
}
static Type* tenv_find_var(TyEnv* E, const char* name, int len){
  for(int si=E->sn-1; si>=0; si--){
    TyScope* S = &E->scopes[si];
    for(int i=S->n-1; i>=0; i--){
      if(S->vars[i].len==len && strncmp(S->vars[i].name,name,(size_t)len)==0){
        return S->vars[i].ty;
      }
    }
  }
  return NULL;
}

static int is_local_name(TyEnv* E, const char* name, int len){
  return tenv_find_var(E, name, len) != NULL;
}

static int expr_is_local_addr(TypeEnv* tenv, TyEnv* venv, Expr* e){
  if(!e) return 0;
  switch(e->k){
    case EX_ADDR: {
      Expr* b = e->un.e;
      if(!b) return 0;
      if(b->k==EX_NAME){
        if(is_local_name(venv, b->name.name, b->name.len)) return 1;
      }
      if(b->k==EX_INDEX && b->index.base && b->index.base->k==EX_NAME){
        if(is_local_name(venv, b->index.base->name.name, b->index.base->name.len)){
          Type* bt = b->index.base->ty;
          if(bt && bt->k==TY_ARRAY) return 1;
        }
      }
      if(b->k==EX_DOT && b->dot.base && b->dot.base->k==EX_NAME){
        if(is_local_name(venv, b->dot.base->name.name, b->dot.base->name.len)){
          Type* bt = b->dot.base->ty;
          if(bt && bt->k==TY_STRUCT) return 1;
        }
      }
      return 0;
    }
    case EX_NAME:
      if(is_local_name(venv, e->name.name, e->name.len)){
        if(e->ty && (e->ty->k==TY_ARRAY || e->ty->k==TY_STRUCT)) return 1;
      }
      return 0;
    case EX_CAST:
      return expr_is_local_addr(tenv, venv, e->cast.e);
    default:
      (void)tenv;
      return 0;
  }
}

/* -------------------------
 * File ownership checks (simple)
 * ------------------------- */
typedef enum { FILE_OWN_UNKNOWN=0, FILE_OWN_OWNED, FILE_OWN_BORROWED, FILE_OWN_CLOSED } FileOwn;

typedef struct {
  const char* name;
  int len;
  FileOwn st;
} OwnVar;

typedef struct {
  OwnVar* vars;
  int n, cap;
} OwnScope;

typedef struct {
  OwnScope* scopes;
  int sn, scap;
} OwnEnv;

static void oenv_push(OwnEnv* E){
  if(E->sn==E->scap){
    E->scap = E->scap? E->scap*2 : 16;
    E->scopes = (OwnScope*)realloc(E->scopes, sizeof(OwnScope)*(size_t)E->scap);
    if(!E->scopes) die("out of memory");
  }
  OwnScope* S = &E->scopes[E->sn++];
  memset(S,0,sizeof(*S));
}
static void oenv_pop(OwnEnv* E){
  if(E->sn<=0) return;
  OwnScope* S = &E->scopes[--E->sn];
  free(S->vars);
  memset(S,0,sizeof(*S));
}
static void oenv_add(OwnEnv* E, const char* name, int len, FileOwn st){
  if(E->sn<=0) oenv_push(E);
  OwnScope* S = &E->scopes[E->sn-1];
  if(S->n==S->cap){
    S->cap = S->cap? S->cap*2 : 16;
    S->vars = (OwnVar*)realloc(S->vars, sizeof(OwnVar)*(size_t)S->cap);
    if(!S->vars) die("out of memory");
  }
  S->vars[S->n++] = (OwnVar){name,len,st};
}
static FileOwn oenv_find(OwnEnv* E, const char* name, int len){
  for(int si=E->sn-1; si>=0; si--){
    OwnScope* S = &E->scopes[si];
    for(int i=S->n-1; i>=0; i--){
      if(S->vars[i].len==len && strncmp(S->vars[i].name,name,(size_t)len)==0){
        return S->vars[i].st;
      }
    }
  }
  return FILE_OWN_UNKNOWN;
}

static void penv_push(PtrEnv* E){
  if(E->sn==E->scap){
    E->scap = E->scap? E->scap*2 : 16;
    E->scopes = (PtrScope*)realloc(E->scopes, sizeof(PtrScope)*(size_t)E->scap);
    if(!E->scopes) die("out of memory");
  }
  PtrScope* S = &E->scopes[E->sn++];
  memset(S,0,sizeof(*S));
}
static void penv_pop(PtrEnv* E){
  if(E->sn<=0) return;
  PtrScope* S = &E->scopes[--E->sn];
  free(S->vars);
  memset(S,0,sizeof(*S));
}
static void penv_add(PtrEnv* E, const char* name, int len, PtrOwn st){
  if(E->sn<=0) penv_push(E);
  PtrScope* S = &E->scopes[E->sn-1];
  if(S->n==S->cap){
    S->cap = S->cap? S->cap*2 : 16;
    S->vars = (PtrVar*)realloc(S->vars, sizeof(PtrVar)*(size_t)S->cap);
    if(!S->vars) die("out of memory");
  }
  S->vars[S->n++] = (PtrVar){name,len,st};
}
static PtrOwn penv_find(PtrEnv* E, const char* name, int len){
  for(int si=E->sn-1; si>=0; si--){
    PtrScope* S = &E->scopes[si];
    for(int i=S->n-1; i>=0; i--){
      if(S->vars[i].len==len && strncmp(S->vars[i].name,name,(size_t)len)==0){
        return S->vars[i].st;
      }
    }
  }
  return PTR_OWN_UNKNOWN;
}
static void penv_set(PtrEnv* E, const char* name, int len, PtrOwn st){
  for(int si=E->sn-1; si>=0; si--){
    PtrScope* S = &E->scopes[si];
    for(int i=S->n-1; i>=0; i--){
      if(S->vars[i].len==len && strncmp(S->vars[i].name,name,(size_t)len)==0){
        S->vars[i].st = st;
        return;
      }
    }
  }
  penv_add(E, name, len, st);
}

static void benv_push(BorrowEnv* E){
  if(E->sn==E->scap){
    E->scap = E->scap? E->scap*2 : 16;
    E->scopes = (BorrowScope*)realloc(E->scopes, sizeof(BorrowScope)*(size_t)E->scap);
    if(!E->scopes) die("out of memory");
  }
  BorrowScope* S = &E->scopes[E->sn++];
  memset(S,0,sizeof(*S));
}
static void benv_pop(BorrowEnv* E){
  if(E->sn<=0) return;
  BorrowScope* S = &E->scopes[--E->sn];
  free(S->vars);
  memset(S,0,sizeof(*S));
}
static void benv_add(BorrowEnv* E, const char* name, int len, BorrowKind st){
  if(E->sn<=0) benv_push(E);
  BorrowScope* S = &E->scopes[E->sn-1];
  if(S->n==S->cap){
    S->cap = S->cap? S->cap*2 : 16;
    S->vars = (BorrowVar*)realloc(S->vars, sizeof(BorrowVar)*(size_t)S->cap);
    if(!S->vars) die("out of memory");
  }
  S->vars[S->n++] = (BorrowVar){name,len,st};
}
static BorrowKind benv_find(BorrowEnv* E, const char* name, int len){
  for(int si=E->sn-1; si>=0; si--){
    BorrowScope* S = &E->scopes[si];
    for(int i=S->n-1; i>=0; i--){
      if(S->vars[i].len==len && strncmp(S->vars[i].name,name,(size_t)len)==0){
        return S->vars[i].st;
      }
    }
  }
  return BORROW_NONE;
}
static void benv_set(BorrowEnv* E, const char* name, int len, BorrowKind st){
  for(int si=E->sn-1; si>=0; si--){
    BorrowScope* S = &E->scopes[si];
    for(int i=S->n-1; i>=0; i--){
      if(S->vars[i].len==len && strncmp(S->vars[i].name,name,(size_t)len)==0){
        S->vars[i].st = st;
        return;
      }
    }
  }
  benv_add(E, name, len, st);
}

static BorrowKind borrow_merge(BorrowKind a, BorrowKind b){
  if(a==BORROW_LOCAL || b==BORROW_LOCAL) return BORROW_LOCAL;
  if(a==BORROW_STATIC || b==BORROW_STATIC) return BORROW_STATIC;
  return BORROW_NONE;
}
static void oenv_set(OwnEnv* E, const char* name, int len, FileOwn st){
  for(int si=E->sn-1; si>=0; si--){
    OwnScope* S = &E->scopes[si];
    for(int i=S->n-1; i>=0; i--){
      if(S->vars[i].len==len && strncmp(S->vars[i].name,name,(size_t)len)==0){
        S->vars[i].st = st;
        return;
      }
    }
  }
}

static int is_file_struct(Type* t){
  return t && t->k==TY_STRUCT && t->sname && name_eq(t->sname, t->sname_len, "File", 4);
}
static int is_file_ptr(Type* t){
  return t && t->k==TY_PTR && is_file_struct(t->elem);
}

static int call_name(Expr* call, const char** out_name, int* out_len, const char** out_mod, int* out_mod_len){
  if(!call || call->k!=EX_CALL) return 0;
  Expr* cal = call->call.callee;
  if(cal->k==EX_NAME){
    *out_name = cal->name.name;
    *out_len = cal->name.len;
    *out_mod = NULL;
    *out_mod_len = 0;
    return 1;
  }
  if(cal->k==EX_DOT && cal->dot.base && cal->dot.base->k==EX_NAME){
    *out_name = cal->dot.mem;
    *out_len = cal->dot.memlen;
    *out_mod = cal->dot.base->name.name;
    *out_mod_len = cal->dot.base->name.len;
    return 1;
  }
  return 0;
}

static int is_io_module(const char* name, int len){
  return name && len==2 && strncmp(name, "io", 2)==0;
}

static FileOwn file_state_from_call(const char* name, int len, const char* mod, int mlen){
  if(mod && !is_io_module(mod, mlen)) return FILE_OWN_UNKNOWN;

  if(len==9 && strncmp(name, "file_open", 9)==0) return FILE_OWN_OWNED;
  if(len==6 && strncmp(name, "open_r", 6)==0) return FILE_OWN_OWNED;
  if(len==6 && strncmp(name, "open_w", 6)==0) return FILE_OWN_OWNED;
  if(len==6 && strncmp(name, "open_a", 6)==0) return FILE_OWN_OWNED;
  if(len==7 && strncmp(name, "open_rb", 7)==0) return FILE_OWN_OWNED;
  if(len==7 && strncmp(name, "open_wb", 7)==0) return FILE_OWN_OWNED;
  if(len==7 && strncmp(name, "open_ab", 7)==0) return FILE_OWN_OWNED;

  if(len==16 && strncmp(name, "file_from_handle", 16)==0) return FILE_OWN_BORROWED;
  if(len==10 && strncmp(name, "stdin_file", 10)==0) return FILE_OWN_BORROWED;
  if(len==11 && strncmp(name, "stdout_file", 11)==0) return FILE_OWN_BORROWED;
  if(len==11 && strncmp(name, "stderr_file", 11)==0) return FILE_OWN_BORROWED;

  return FILE_OWN_UNKNOWN;
}

static FileOwn file_state_from_expr(OwnEnv* E, Expr* e){
  if(!e) return FILE_OWN_UNKNOWN;
  if(e->k==EX_NAME){
    return oenv_find(E, e->name.name, e->name.len);
  }
  if(e->k==EX_CALL){
    const char* nm = NULL; int nlen = 0; const char* mod = NULL; int mlen = 0;
    if(call_name(e, &nm, &nlen, &mod, &mlen)){
      FileOwn st = file_state_from_call(nm, nlen, mod, mlen);
      if(st != FILE_OWN_UNKNOWN) return st;
    }
  }
  if(e->k==EX_CAST){
    return file_state_from_expr(E, e->cast.e);
  }
  return FILE_OWN_UNKNOWN;
}

static int is_file_close_call(Expr* call){
  const char* nm = NULL; int nlen = 0; const char* mod = NULL; int mlen = 0;
  if(!call_name(call, &nm, &nlen, &mod, &mlen)) return 0;
  if(nlen==10 && strncmp(nm, "file_close", 10)==0){
    if(!mod || is_io_module(mod, mlen)) return 1;
  }
  return 0;
}

static void own_check_expr(OwnEnv* E, Expr* e){
  if(!e) return;
  switch(e->k){
    case EX_BIN:
      own_check_expr(E, e->bin.a);
      own_check_expr(E, e->bin.b);
      return;
    case EX_UN:
      own_check_expr(E, e->un.e);
      return;
    case EX_CALL: {
      for(int i=0;i<e->call.argc;i++) own_check_expr(E, e->call.args[i]);
      if(is_file_close_call(e)){
        if(e->call.argc >= 1){
          Expr* arg = e->call.args[0];
          if(arg && is_file_ptr(arg->ty)){
            FileOwn st = file_state_from_expr(E, arg);
            if(st == FILE_OWN_BORROWED) err_at(e->loc, "file_close on non-owned File");
            if(st == FILE_OWN_CLOSED) err_at(e->loc, "file_close on already closed File");
            if(arg->k==EX_NAME && st != FILE_OWN_BORROWED){
              oenv_set(E, arg->name.name, arg->name.len, FILE_OWN_CLOSED);
            }
          }
        }
      }
      return;
    }
    case EX_INDEX:
      own_check_expr(E, e->index.base);
      own_check_expr(E, e->index.idx);
      return;
    case EX_DOT:
      own_check_expr(E, e->dot.base);
      return;
    case EX_ADDR:
    case EX_DEREF:
      own_check_expr(E, e->un.e);
      return;
    case EX_ARRAY_LIT:
      for(int i=0;i<e->arr.n;i++) own_check_expr(E, e->arr.items[i]);
      return;
    case EX_STRUCT_LIT:
      for(int i=0;i<e->stlit.n;i++) own_check_expr(E, e->stlit.v[i]);
      return;
    case EX_CAST:
      own_check_expr(E, e->cast.e);
      return;
    case EX_SIZEOF:
    case EX_ALIGNOF:
      if(!e->siz.is_type) own_check_expr(E, e->siz.e);
      return;
    default:
      return;
  }
}

static void own_check_stmt(TypeEnv* tenv, Module* M, FnDecl* curF, OwnEnv* E, Stmt* s);

static void own_check_block(TypeEnv* tenv, Module* M, FnDecl* curF, OwnEnv* E, Stmt* block){
  if(!block || block->k!=ST_BLOCK) return;
  oenv_push(E);
  for(int i=0;i<block->block.n;i++){
    own_check_stmt(tenv, M, curF, E, block->block.items[i]);
  }
  oenv_pop(E);
}

static void own_check_stmt(TypeEnv* tenv, Module* M, FnDecl* curF, OwnEnv* E, Stmt* s){
  (void)tenv; (void)M; (void)curF;
  if(!s) return;
  switch(s->k){
    case ST_BLOCK:
      own_check_block(tenv, M, curF, E, s);
      return;
    case ST_LET: {
      if(s->let.init) own_check_expr(E, s->let.init);
      if(is_file_ptr(s->let.ty)){
        FileOwn st = s->let.init ? file_state_from_expr(E, s->let.init) : FILE_OWN_UNKNOWN;
        oenv_add(E, s->let.name, s->let.len, st);
      }
      return;
    }
    case ST_ASSIGN: {
      if(s->asg.rhs) own_check_expr(E, s->asg.rhs);
      if(s->asg.op==TK_ASSIGN && s->asg.lhs && s->asg.lhs->k==EX_NAME && is_file_ptr(s->asg.lhs->ty)){
        FileOwn st = s->asg.rhs ? file_state_from_expr(E, s->asg.rhs) : FILE_OWN_UNKNOWN;
        oenv_set(E, s->asg.lhs->name.name, s->asg.lhs->name.len, st);
      }
      return;
    }
    case ST_EXPR:
      own_check_expr(E, s->expr.e);
      return;
    case ST_RET:
      if(s->ret.e) own_check_expr(E, s->ret.e);
      return;
    case ST_IF:
      own_check_expr(E, s->iff.cond);
      own_check_stmt(tenv, M, curF, E, s->iff.thenb);
      if(s->iff.elseb) own_check_stmt(tenv, M, curF, E, s->iff.elseb);
      return;
    case ST_WHILE:
      own_check_expr(E, s->wh.cond);
      own_check_stmt(tenv, M, curF, E, s->wh.body);
      return;
    case ST_FOR:
      oenv_push(E);
      if(s->fr.init) own_check_stmt(tenv, M, curF, E, s->fr.init);
      if(s->fr.cond) own_check_expr(E, s->fr.cond);
      if(s->fr.step) own_check_stmt(tenv, M, curF, E, s->fr.step);
      own_check_stmt(tenv, M, curF, E, s->fr.body);
      oenv_pop(E);
      return;
    case ST_SWITCH:
      own_check_expr(E, s->sw.cond);
      for(int i=0;i<s->sw.case_n;i++){
        SwitchCase* c = &s->sw.cases[i];
        if(c->val) own_check_expr(E, c->val);
        if(c->body) own_check_stmt(tenv, M, curF, E, c->body);
      }
      return;
    case ST_BREAK:
    case ST_CONTINUE:
    case ST_FALLTHROUGH:
      return;
    default:
      return;
  }
}

static void own_check_fn(TypeEnv* tenv, Module* M, FnDecl* fn){
  OwnEnv E; memset(&E,0,sizeof(E));
  oenv_push(&E);

  if(M){
    for(int i=0;i<M->global_n;i++){
      struct GlobalVar* g = &M->globals[i];
      if(is_file_ptr(g->ty)){
        oenv_add(&E, g->name, g->len, FILE_OWN_UNKNOWN);
      }
    }
  }

  for(int i=0;i<fn->arity;i++){
    if(is_file_ptr(fn->ptypes[i])){
      oenv_add(&E, fn->pnames[i], fn->plens[i], FILE_OWN_UNKNOWN);
    }
  }

  if(fn->body) own_check_stmt(tenv, M, fn, &E, fn->body);

  while(E.sn>0) oenv_pop(&E);
  free(E.scopes);
}

static int find_enum_const(ModuleGraph* G, Module* curM, const char* name, int len, long long* out){
  if(curM){
    for(int i=0;i<curM->econst_n;i++){
      if(curM->econsts[i].len==len && strncmp(curM->econsts[i].name,name,(size_t)len)==0){
        if(out) *out = curM->econsts[i].val;
        return 1;
      }
    }
  }
  if(G){
    for(int m=0;m<G->mod_n;m++){
      Module* M = G->mods[m];
      if(M==curM) continue;
      for(int i=0;i<M->econst_n;i++){
        if(M->econsts[i].len==len && strncmp(M->econsts[i].name,name,(size_t)len)==0){
          if(out) *out = M->econsts[i].val;
          return 1;
        }
      }
    }
  }
  return 0;
}

static void require_assignable(TypeEnv* E, SrcLoc loc, Type* dst, Expr* rhs);

/* -------------------------
 * Forward decls
 * ------------------------- */
static void sema_expr(ModuleGraph* G, TypeEnv* tenv, Module* curM, FnDecl* curF, TyEnv* venv, PtrEnv* penv, BorrowEnv* benv, Expr* e, int in_unsafe);
static void sema_stmt(ModuleGraph* G, TypeEnv* tenv, Module* curM, FnDecl* curF, TyEnv* venv, PtrEnv* penv, BorrowEnv* benv, Stmt* s, int in_switch, int in_unsafe);

/* -------------------------
 * Lvalue checks
 * ------------------------- */

static int is_lvalue(Expr* e){
  if(!e) return 0;
  switch(e->k){
    case EX_NAME:  return 1;
    case EX_INDEX: return 1;
    case EX_DOT:   return 1;
    case EX_DEREF: return 1;
    default: return 0;
  }
}

/* -------------------------
 * Expr typing
 * ------------------------- */

static void type_bin(TypeEnv* tenv, Expr* e){
  if(e->bin.op==OP_EQ||e->bin.op==OP_NEQ||e->bin.op==OP_LT||e->bin.op==OP_LTE||e->bin.op==OP_GT||e->bin.op==OP_GTE||
     e->bin.op==OP_LAND||e->bin.op==OP_LOR){
    e->ty = tenv->b.ty_i64;
    return;
  }
  if((e->bin.a && (is_ptr(e->bin.a->ty) || is_array(e->bin.a->ty))) ||
     (e->bin.b && (is_ptr(e->bin.b->ty) || is_array(e->bin.b->ty)))){
    if(e->bin.op==OP_ADD || e->bin.op==OP_SUB){
      // ptr +/- int -> ptr, ptr - ptr -> int
      if(e->bin.a && (is_ptr(e->bin.a->ty) || is_array(e->bin.a->ty)) &&
         e->bin.b && (is_ptr(e->bin.b->ty) || is_array(e->bin.b->ty)) &&
         e->bin.op==OP_SUB){
        e->ty = tenv->b.ty_i64;
      } else {
        // pick pointer side
        Type* pt = (is_ptr(e->bin.a->ty) || is_array(e->bin.a->ty)) ? e->bin.a->ty : e->bin.b->ty;
        if(pt && pt->k==TY_ARRAY) pt = type_ptr(tenv, pt->elem);
        e->ty = pt;
      }
      return;
    }
  }
  if((e->bin.a && is_f64(tenv, e->bin.a->ty)) || (e->bin.b && is_f64(tenv, e->bin.b->ty))){
    e->ty = tenv->b.ty_f64;
    return;
  }
  e->ty = tenv->b.ty_i64;
}

static void type_un(TypeEnv* tenv, Expr* e){
  if(e->un.e && is_f64(tenv, e->un.e->ty)) e->ty = tenv->b.ty_f64;
  else e->ty = tenv->b.ty_i64;
}

static void sema_call(ModuleGraph* G, TypeEnv* tenv, Module* curM, FnDecl* curF, TyEnv* venv, PtrEnv* penv, BorrowEnv* benv, Expr* call, int in_unsafe){
  Expr* cal = call->call.callee;

  // First type args
  for(int i=0;i<call->call.argc;i++){
    sema_expr(G, tenv, curM, curF, venv, penv, benv, call->call.args[i], in_unsafe);
  }

  // Case 1: direct name call
  if(cal->k==EX_NAME){
    const char* nm = cal->name.name;
    int nlen = cal->name.len;

    Type* vt = tenv_find_var(venv, nm, nlen);
    if(vt && vt->k==TY_PTR && vt->elem && vt->elem->k==TY_FN){
      Type* fnt = vt->elem;
      if(call->call.argc != fnt->fparam_n) err_at(call->loc, "wrong number of arguments in fn pointer call");
      for(int i=0;i<fnt->fparam_n;i++){
        require_assignable(tenv, call->call.args[i]->loc, fnt->fparams[i], call->call.args[i]);
      }
      call->ty = fnt->fret;
      return;
    }

    // builtin
    if(is_builtin_name(nm, nlen)){
      if(g_sema_freestanding && !builtin_allowed_freestanding(nm, nlen)){
        err_at(call->loc, "builtin not allowed in freestanding mode");
      }
      if(nlen==7 && strncmp(nm,"gpu_tid",7)==0){
        if(!curF || !curF->is_kernel) err_at(call->loc, "gpu_tid is only allowed inside kernel fn");
      }
      if((nlen==6 && strncmp(nm,"malloc",6)==0) || (nlen==4 && strncmp(nm,"free",4)==0)){
        require_unsafe(call->loc, in_unsafe);
      }
      if(nlen==5 && strncmp(nm,"say_f",5)==0){
        // treat say_f as say (multi-arg formatter handles floats)
        cal->name.name = "say";
        cal->name.len  = 3;
        nm = cal->name.name;
        nlen = cal->name.len;
      }
      if(nlen==5 && strncmp(nm,"input",5)==0){
        // builtin input([prompt:str[, mode:int]]) -> f64
        if(call->call.argc > 2) err_at(call->loc, "builtin: wrong number of arguments");
        Type* PU8 = ty_ptr(tenv, tenv->b.ty_u8);
        if(call->call.argc >= 1){
          require_assignable(tenv, call->call.args[0]->loc, PU8, call->call.args[0]);
        }
        if(call->call.argc == 2){
          require_assignable(tenv, call->call.args[1]->loc, tenv->b.ty_i64, call->call.args[1]);
        }
        call->ty = tenv->b.ty_f64;
        return;
      }
      /* ------------------------------------------------------------
       * PRODUCTION LOWERING FOR say:
       *   - say;           => say(0)
       *   - say "hello";   => say_str("hello")
       *   - say buf;       where buf is [u8;N] => say_str(buf)
       *   - say n;         => say(n) (i64)
       *   - say a,b,c;     => say_multi(vals,tags,argc) in native
       * ------------------------------------------------------------ */
      if(nlen==3 && strncmp(nm,"say",3)==0){
        // say; -> say(0)
        if(call->call.argc == 0){
          Expr* z = (Expr*)malloc(sizeof(Expr));
          if(!z) die("out of memory");
          memset(z,0,sizeof(*z));
          z->k = EX_NUM;
          z->loc = call->loc;
          z->num = 0;
          z->ty = tenv->b.ty_i64;

          call->call.args = (Expr**)malloc(sizeof(Expr*));
          if(!call->call.args) die("out of memory");
          call->call.args[0] = z;
          call->call.argc = 1;
        }

        // stringy -> say_str
        if(call->call.argc==1 && expr_is_stringy(tenv, call->call.args[0])){
          cal->name.name = "say_str";
          cal->name.len  = 7;

          BuiltinSig ss = builtin_sig(tenv, "say_str", 7);
          require_assignable(tenv, call->call.args[0]->loc, ss.a0, call->call.args[0]);
          call->ty = ss.ret;
          return;
        }

        // allow numbers + strings (native helper will format)
        if(call->call.argc >= 1){
          for(int i=0;i<call->call.argc;i++){
            Expr* a = call->call.args[i];
            if(expr_is_stringy(tenv, a)) continue;
            if(a && is_f64(tenv, a->ty)) continue;
            require_assignable(tenv, a->loc, tenv->b.ty_i64, a);
          }
          call->ty = tenv->b.ty_i64;
          return;
        }

        // otherwise: say(i64)
        BuiltinSig si; memset(&si,0,sizeof(si));
        si.varargs = 0;
        si.argc = 1;
        si.ret = tenv->b.ty_i64;
        si.a0  = tenv->b.ty_i64;

        require_assignable(tenv, call->call.args[0]->loc, si.a0, call->call.args[0]);
        call->ty = si.ret;
        return;
      }

      // Normal builtin checking
      BuiltinSig s = builtin_sig(tenv, nm, nlen);

      if(!s.varargs){
        if(call->call.argc != s.argc) err_at(call->loc, "builtin: wrong number of arguments");
        if(s.argc>=1) require_assignable(tenv, call->call.args[0]->loc, s.a0, call->call.args[0]);
        if(s.argc>=2) require_assignable(tenv, call->call.args[1]->loc, s.a1, call->call.args[1]);
        if(s.argc>=3) require_assignable(tenv, call->call.args[2]->loc, s.a2, call->call.args[2]);
        if(s.argc>=4) require_assignable(tenv, call->call.args[3]->loc, s.a3, call->call.args[3]);
        if(s.argc>=5) require_assignable(tenv, call->call.args[4]->loc, s.a4, call->call.args[4]);
      }

        if(penv && nlen==4 && strncmp(nm,"free",4)==0 && call->call.argc==1){
          const char* pn = NULL; int pl = 0;
          if(expr_ptr_var_name(call->call.args[0], &pn, &pl)){
            PtrOwn st = penv_find(penv, pn, pl);
            if(st == PTR_OWN_BORROWED && !in_unsafe){
              err_borrow_name(call->loc, "cannot free borrowed pointer", pn, pl, "only owned pointers can be freed");
            }
            penv_set(penv, pn, pl, PTR_OWN_MOVED);
          }
        }
      call->ty = s.ret;
      return;
    }

    // local fn
    FnDecl* f = find_fn_in_module(curM, nm, nlen);
    if(!f){
      // std prelude: if std is imported, allow unqualified calls to std.*
      Module* stdm = find_module_by_mname(G, "std", 3);
      if(stdm && module_imports_name(curM, "std", 3)){
        FnDecl* sf = find_fn_in_module(stdm, nm, nlen);
        if(sf){
          Expr* base = (Expr*)malloc(sizeof(Expr));
          Expr* dot = (Expr*)malloc(sizeof(Expr));
          if(!base || !dot) die("out of memory");
          memset(base, 0, sizeof(*base));
          memset(dot, 0, sizeof(*dot));
          base->k = EX_NAME;
          base->loc = call->loc;
          base->name.name = "std";
          base->name.len = 3;
          dot->k = EX_DOT;
          dot->loc = call->loc;
          dot->dot.base = base;
          dot->dot.mem = nm;
          dot->dot.memlen = nlen;
          call->call.callee = dot;
          f = sf;
        }
      }
    }
    if(!f) err_name_at(call->loc, "unknown function", nm, nlen);
    if(f->is_unsafe) require_unsafe(call->loc, in_unsafe);

    if(call->call.argc != f->arity) err_arity_at(call->loc, "wrong number of arguments in call", nm, nlen, f->arity, call->call.argc);
    for(int i=0;i<f->arity;i++){
      require_assignable(tenv, call->call.args[i]->loc, f->ptypes[i], call->call.args[i]);
    }
    if(fn_has_attr(f, "deprecated", 10)){
      const Attr* ad = fn_get_attr(f, "deprecated", 10);
      if(ad && ad->arg && ad->arg_len>0){
        char msg[256];
        snprintf(msg, sizeof(msg), "deprecated: %.*s", ad->arg_len, ad->arg);
        warn_at(call->loc, msg);
      } else {
        warn_at(call->loc, "deprecated function");
      }
    }
      apply_ptr_call_rules(penv, f, call->call.args, call->call.argc, in_unsafe, call->loc);
      for(int i=0;i<call->call.argc;i++){
        Expr* a = call->call.args[i];
        if(!a || !a->ty) continue;
        if(a->ty->k==TY_STRUCT || a->ty->k==TY_ARRAY){
          BorrowKind bk = borrow_kind_from_expr(benv, penv, a);
          if(bk == BORROW_LOCAL && !in_unsafe){
            err_borrow_expr(a->loc, "cannot pass borrowed aggregate by value", a, "borrowed values cannot escape");
          }
        }
      }
      call->ty = f->ret;
      return;
  }

  // Case 2: module.fn call
  if(cal->k==EX_DOT){
    Expr* base = cal->dot.base;
    char* modnm = NULL;
    int modlen = 0;
    if(!flatten_mod_name(base, &modnm, &modlen)){
      err_at(call->loc, "call target must be name or module.name");
    }

    const char* fnm = cal->dot.mem;
    int fnlen = cal->dot.memlen;

    Module* target = find_module_by_mname(G, modnm, modlen);
    if(!target){
      char msg[256];
      snprintf(msg, sizeof(msg), "unknown module '%.*s' in call", modlen, modnm);
      free(modnm);
      err_at(call->loc, msg);
    }

    FnDecl* f = find_fn_in_module(target, fnm, fnlen);
    if(!f){
      char msg[256];
      snprintf(msg, sizeof(msg), "unknown function '%.*s' in module '%.*s'", fnlen, fnm, modlen, modnm);
      free(modnm);
      err_at(call->loc, msg);
    }
    if(f->is_unsafe) require_unsafe(call->loc, in_unsafe);

    if(call->call.argc != f->arity) err_arity_at(call->loc, "wrong number of arguments in module call", fnm, fnlen, f->arity, call->call.argc);
    for(int i=0;i<f->arity;i++){
      require_assignable(tenv, call->call.args[i]->loc, f->ptypes[i], call->call.args[i]);
    }
    if(fn_has_attr(f, "deprecated", 10)){
      const Attr* ad = fn_get_attr(f, "deprecated", 10);
      if(ad && ad->arg && ad->arg_len>0){
        char msg[256];
        snprintf(msg, sizeof(msg), "deprecated: %.*s", ad->arg_len, ad->arg);
        warn_at(call->loc, msg);
      } else {
        warn_at(call->loc, "deprecated function");
      }
    }
      apply_ptr_call_rules(penv, f, call->call.args, call->call.argc, in_unsafe, call->loc);
      for(int i=0;i<call->call.argc;i++){
        Expr* a = call->call.args[i];
        if(!a || !a->ty) continue;
        if(a->ty->k==TY_STRUCT || a->ty->k==TY_ARRAY){
          BorrowKind bk = borrow_kind_from_expr(benv, penv, a);
          if(bk == BORROW_LOCAL && !in_unsafe){
            err_borrow_expr(a->loc, "cannot pass borrowed aggregate by value", a, "borrowed values cannot escape");
          }
        }
      }
      free(modnm);
      call->ty = f->ret;
      return;
  }

  // Case 3: fn pointer call
    sema_expr(G, tenv, curM, curF, venv, penv, benv, cal, in_unsafe);
  if(cal->ty && cal->ty->k==TY_PTR && cal->ty->elem && cal->ty->elem->k==TY_FN){
    Type* fnt = cal->ty->elem;
    if(call->call.argc != fnt->fparam_n) err_at(call->loc, "wrong number of arguments in fn pointer call");
    for(int i=0;i<fnt->fparam_n;i++){
      require_assignable(tenv, call->call.args[i]->loc, fnt->fparams[i], call->call.args[i]);
    }
    call->ty = fnt->fret;
    return;
  }

  err_at(call->loc, "call target must be name, module.name, or fn pointer");
}

static void sema_expr(ModuleGraph* G, TypeEnv* tenv, Module* curM, FnDecl* curF, TyEnv* venv, PtrEnv* penv, BorrowEnv* benv, Expr* e, int in_unsafe){
  (void)curF;
  if(!e) return;

  switch(e->k){
    case EX_NUM:
      e->ty = tenv->b.ty_i64;
      return;

    case EX_FLOAT:
      e->ty = tenv->b.ty_f64;
      return;

    case EX_STR:
      e->ty = type_ptr(tenv, tenv->b.ty_u8);
      return;

    case EX_NAME: {
      Type* t = tenv_find_var(venv, e->name.name, e->name.len);
      if(t){
        e->ty = t;
        if(penv && t->k==TY_PTR){
          PtrOwn st = penv_find(penv, e->name.name, e->name.len);
          if(st==PTR_OWN_MOVED && !in_unsafe){
            err_borrow_name(e->loc, "use of moved pointer", e->name.name, e->name.len, "reassign before use or wrap in unsafe");
          }
        }
        return;
      }
      long long v = 0;
      if(find_enum_const(G, curM, e->name.name, e->name.len, &v)){
        e->k = EX_NUM;
        e->num = v;
        e->ty = tenv->b.ty_i64;
        return;
      }
      FnDecl* f = find_fn_any(G, curM, e->name.name, e->name.len);
      if(f && f->fty){
        e->ty = type_ptr(tenv, f->fty);
        return;
      }
      e->ty = NULL;
      return;
    }

    case EX_BIN:
    sema_expr(G, tenv, curM, curF, venv, penv, benv, e->bin.a, in_unsafe);
    sema_expr(G, tenv, curM, curF, venv, penv, benv, e->bin.b, in_unsafe);
      if(e->bin.op==OP_BAND||e->bin.op==OP_BOR||e->bin.op==OP_BXOR||e->bin.op==OP_SHL||e->bin.op==OP_SHR){
        if(e->bin.a && e->bin.a->ty && !is_int(tenv, e->bin.a->ty)) err_at(e->bin.a->loc, "bitwise op expects integer");
        if(e->bin.b && e->bin.b->ty && !is_int(tenv, e->bin.b->ty)) err_at(e->bin.b->loc, "bitwise op expects integer");
      } else if(e->bin.op==OP_ADD || e->bin.op==OP_SUB){
        Type* ta = e->bin.a ? e->bin.a->ty : NULL;
        Type* tb = e->bin.b ? e->bin.b->ty : NULL;
        int pa = ta && (is_ptr(ta) || is_array(ta));
        int pb = tb && (is_ptr(tb) || is_array(tb));
        if(pa || pb){
          require_unsafe(e->loc, in_unsafe);
          // ptr +/- int or ptr - ptr
          if(pa && pb){
            if(e->bin.op!=OP_SUB) err_at(e->loc, "pointer + pointer invalid");
          } else {
            Type* ti = pa ? tb : ta;
            if(ti && !is_int(tenv, ti)) err_at(e->loc, "pointer arithmetic requires integer index");
          }
        } else {
          if(ta && !is_num(tenv, ta)) err_at(e->bin.a->loc, "binary op expects number");
          if(tb && !is_num(tenv, tb)) err_at(e->bin.b->loc, "binary op expects number");
        }
      } else if(e->bin.op==OP_EQ || e->bin.op==OP_NEQ){
        Type* ta = e->bin.a ? e->bin.a->ty : NULL;
        Type* tb = e->bin.b ? e->bin.b->ty : NULL;
        if((ta && is_ptr(ta)) || (tb && is_ptr(tb))){
          // allow pointer equality
        } else {
          if(ta && !is_num(tenv, ta)) err_at(e->bin.a->loc, "binary op expects number");
          if(tb && !is_num(tenv, tb)) err_at(e->bin.b->loc, "binary op expects number");
        }
      } else {
        if(e->bin.a && e->bin.a->ty && !is_num(tenv, e->bin.a->ty)) err_at(e->bin.a->loc, "binary op expects number");
        if(e->bin.b && e->bin.b->ty && !is_num(tenv, e->bin.b->ty)) err_at(e->bin.b->loc, "binary op expects number");
      }
      type_bin(tenv, e);
      return;

    case EX_UN:
    sema_expr(G, tenv, curM, curF, venv, penv, benv, e->un.e, in_unsafe);
      if(e->un.op==UN_BNOT || e->un.op==UN_LNOT){
        if(e->un.e && e->un.e->ty && !is_int(tenv, e->un.e->ty)) err_at(e->un.e->loc, "unary op expects integer");
      } else {
        if(e->un.e && e->un.e->ty && !is_num(tenv, e->un.e->ty)) err_at(e->un.e->loc, "unary op expects number");
      }
      type_un(tenv, e);
      return;

    case EX_CAST: {
    sema_expr(G, tenv, curM, curF, venv, penv, benv, e->cast.e, in_unsafe);
      if(!e->cast.to) err_at(e->loc, "cast target has no type");
      if(!e->cast.e || !e->cast.e->ty) err_at(e->loc, "cast expression has no type");
      Type* from = e->cast.e->ty;
      Type* to = e->cast.to;
      if((from && from->k==TY_PTR) || (to && to->k==TY_PTR)){
        require_unsafe(e->loc, in_unsafe);
      }
      int ok = 0;
      if(type_eq(from, to)) ok = 1;
      else if(is_num(tenv, from) && is_num(tenv, to)) ok = 1;
      else if(from->k==TY_PTR && to->k==TY_PTR) ok = 1;
      else if(from->k==TY_PTR && is_int(tenv, to)) ok = 1;
      else if(is_int(tenv, from) && to->k==TY_PTR) ok = 1;
      if(!ok) err_at(e->loc, "invalid cast");
      e->ty = to;
      return;
    }

    case EX_SIZEOF:
    case EX_ALIGNOF: {
      long long out = 0;
      if(e->siz.is_type){
        if(!e->siz.ty) err_at(e->loc, "sizeof/alignof needs type");
        out = (e->k==EX_SIZEOF) ? type_size(e->siz.ty) : type_align(e->siz.ty);
      } else {
        sema_expr(G, tenv, curM, curF, venv, penv, benv, e->siz.e, in_unsafe);
        if(!e->siz.e || !e->siz.e->ty) err_at(e->loc, "sizeof/alignof needs typed expression");
        out = (e->k==EX_SIZEOF) ? type_size(e->siz.e->ty) : type_align(e->siz.e->ty);
      }
      e->k = EX_NUM;
      e->num = out;
      e->ty = tenv->b.ty_i64;
      return;
    }

    case EX_ADDR:
      require_unsafe(e->loc, in_unsafe);
    sema_expr(G, tenv, curM, curF, venv, penv, benv, e->un.e, in_unsafe);
      if(!is_lvalue(e->un.e)) err_at(e->loc, "address-of (&) requires lvalue");
      if(!e->un.e || !e->un.e->ty) err_at(e->loc, "cannot take address of untyped expression");
      e->ty = type_ptr(tenv, e->un.e->ty);
      return;

    case EX_DEREF:
      require_unsafe(e->loc, in_unsafe);
    sema_expr(G, tenv, curM, curF, venv, penv, benv, e->un.e, in_unsafe);
      if(!e->un.e || !e->un.e->ty || e->un.e->ty->k!=TY_PTR) err_at(e->loc, "deref (*) expects pointer");
      e->ty = e->un.e->ty->elem;
      return;

    case EX_INDEX:
    sema_expr(G, tenv, curM, curF, venv, penv, benv, e->index.base, in_unsafe);
    sema_expr(G, tenv, curM, curF, venv, penv, benv, e->index.idx, in_unsafe);
      if(e->index.idx && e->index.idx->ty && !is_int(tenv, e->index.idx->ty)) err_at(e->index.idx->loc, "index must be integer");

      if(!e->index.base || !e->index.base->ty) err_at(e->loc, "indexing base has no type");
      if(e->index.base->ty->k==TY_ARRAY){ e->ty = e->index.base->ty->elem; return; }
      if(e->index.base->ty->k==TY_PTR)  { require_unsafe(e->loc, in_unsafe); e->ty = e->index.base->ty->elem; return; }
      err_at(e->loc, "indexing requires array or pointer");
      return;

    case EX_DOT: {
    sema_expr(G, tenv, curM, curF, venv, penv, benv, e->dot.base, in_unsafe);

      if(!e->dot.base || !e->dot.base->ty){
        e->ty = NULL; // could be module.fn (handled in call)
        return;
      }

      Type* bt = e->dot.base->ty;
      if(bt->k!=TY_STRUCT) err_at(e->loc, "dot access requires struct type");
      Field* f = type_find_field(bt, e->dot.mem, e->dot.memlen);
      if(!f) err_at(e->loc, "unknown struct field");
      e->ty = f->ty;
      return;
    }

    case EX_CALL:
    sema_call(G, tenv, curM, curF, venv, penv, benv, e, in_unsafe);
      return;

    case EX_ARRAY_LIT: {
      if(e->arr.n<=0) err_at(e->loc, "cannot infer type of empty array literal");
    for(int i=0;i<e->arr.n;i++) sema_expr(G, tenv, curM, curF, venv, penv, benv, e->arr.items[i], in_unsafe);

      Type* et = e->arr.items[0]->ty;
      if(!et) err_at(e->arr.items[0]->loc, "array literal element has no type");
      for(int i=0;i<e->arr.n;i++){
        if(!e->arr.items[i]->ty) err_at(e->arr.items[i]->loc, "array literal element has no type");
        if(!type_eq(et, e->arr.items[i]->ty)) err_at(e->arr.items[i]->loc, "array literal elements must have same type");
      }
      e->ty = type_array(tenv, et, (long long)e->arr.n);
      return;
    }

    case EX_STRUCT_LIT: {
      Type* st = tenv_find_struct(tenv, e->stlit.tname, e->stlit.tlen);
      if(!st) err_at(e->loc, "unknown struct type in struct literal");
      if(st->k!=TY_STRUCT) err_at(e->loc, "struct literal target is not a struct");

      for(int i=0;i<e->stlit.n;i++){
        Field* f = type_find_field(st, e->stlit.f[i], e->stlit.flen[i]);
        if(!f) err_at(e->loc, "unknown field in struct literal");
        sema_expr(G, tenv, curM, curF, venv, penv, benv, e->stlit.v[i], in_unsafe);
        require_assignable(tenv, e->stlit.v[i]->loc, f->ty, e->stlit.v[i]);
      }
      e->ty = st;
      return;
    }

    default:
      err_at(e->loc, "sema: unsupported expression kind");
  }
}

/* -------------------------
 * Assignment compatibility
 * ------------------------- */

static int is_string_to_u8_array_ok(TypeEnv* E, Type* dst, Expr* rhs){
  (void)E;
  if(!dst || !rhs) return 0;
  if(dst->k!=TY_ARRAY) return 0;
  if(!dst->elem || dst->elem->k!=TY_U8) return 0;
  if(rhs->k!=EX_STR) return 0;
  return 1;
}

static void require_assignable(TypeEnv* E, SrcLoc loc, Type* dst, Expr* rhs){
  if(!dst) err_at(loc, "assignment target has no type");
  if(!rhs) err_at(loc, "missing rhs expression");
  if(!rhs->ty){
    if(rhs->k==EX_NAME){
      err_unknown_name_expr(rhs);
    }
    err_at(loc, "rhs has no type");
  }

  // "hello" -> [u8;N]
  if(is_string_to_u8_array_ok(E, dst, rhs)) return;

  // exact match
  if(type_eq(dst, rhs->ty)) return;

  // Integer widths are assignment-compatible; runtime stores truncate to the destination width.
  if(is_int(E, dst) && is_int(E, rhs->ty)) return;

  // allow pointer-to-pointer assignment (C-style)
  if(dst->k==TY_PTR && rhs->ty && rhs->ty->k==TY_PTR) return;

  // int -> float
  if(is_f64(E, dst) && is_num(E, rhs->ty)) return;

  // EX_STR (*u8) into *u8
  if(rhs->k==EX_STR && is_ptr_to_u8(E, dst)) return;

  // [T;N] -> *T (array-to-pointer decay)
  if(dst->k==TY_PTR && rhs->ty && rhs->ty->k==TY_ARRAY &&
     rhs->ty->elem && dst->elem && type_eq(dst->elem, rhs->ty->elem)){
    return;
  }

  err_at(loc, "type mismatch in assignment/call argument");
}

/* -------------------------
 * Stmt typing
 * ------------------------- */

static void sema_block(ModuleGraph* G, TypeEnv* tenv, Module* curM, FnDecl* curF, TyEnv* venv, PtrEnv* penv, BorrowEnv* benv, Stmt* block, int in_switch, int in_unsafe){
  if(!block || block->k!=ST_BLOCK) return;
  tenv_push(venv);
  if(penv) penv_push(penv);
  if(benv) benv_push(benv);
  for(int i=0;i<block->block.n;i++){
    sema_stmt(G, tenv, curM, curF, venv, penv, benv, block->block.items[i], in_switch, in_unsafe);
  }
  if(benv) benv_pop(benv);
  if(penv) penv_pop(penv);
  tenv_pop(venv);
}

static void sema_stmt(ModuleGraph* G, TypeEnv* tenv, Module* curM, FnDecl* curF, TyEnv* venv, PtrEnv* penv, BorrowEnv* benv, Stmt* s, int in_switch, int in_unsafe){
  if(!s) return;

  switch(s->k){
    case ST_BLOCK:
      sema_block(G, tenv, curM, curF, venv, penv, benv, s, in_switch, in_unsafe);
      return;

    case ST_LET: {
      if(s->let.init) sema_expr(G, tenv, curM, curF, venv, penv, benv, s->let.init, in_unsafe);

      Type* vt = s->let.ty;

      if(!vt){
        if(!s->let.init) err_at(s->loc, "let without type needs initializer");
        if(!s->let.init->ty){
          if(s->let.init->k==EX_NAME) err_unknown_name_expr(s->let.init);
          err_at(s->let.init->loc, "cannot infer let type (init has no type)");
        }
        vt = s->let.init->ty;
        s->let.ty = vt;
      }

      if(s->let.is_const && !s->let.init) err_at(s->loc, "const let requires initializer");

      if(s->let.init){
        require_assignable(tenv, s->let.init->loc, vt, s->let.init);
      }

      tenv_add_var(venv, s->let.name, s->let.len, vt, s->loc);
      if(penv && vt && vt->k==TY_PTR){
        PtrOwn st = PTR_OWN_UNKNOWN;
        if(s->let.init) st = ptr_state_from_expr(penv, s->let.init);
        penv_add(penv, s->let.name, s->let.len, st);
      }
      if(benv && vt && (vt->k==TY_STRUCT || vt->k==TY_ARRAY)){
        BorrowKind bk = s->let.init ? borrow_kind_from_expr(benv, penv, s->let.init) : BORROW_NONE;
        benv_add(benv, s->let.name, s->let.len, bk);
      }
      return;
    }

    case ST_ASSIGN: {
      if(s->asg.op==TK_ASSIGN && s->asg.lhs && s->asg.lhs->k==EX_NAME){
        const char* nm = s->asg.lhs->name.name;
        int nlen = s->asg.lhs->name.len;
        Type* existing = tenv_find_var(venv, nm, nlen);
        if(!existing){
          if(s->asg.rhs) sema_expr(G, tenv, curM, curF, venv, penv, benv, s->asg.rhs, in_unsafe);
          if(!s->asg.rhs || !s->asg.rhs->ty){
            if(s->asg.rhs && s->asg.rhs->k==EX_NAME) err_unknown_name_expr(s->asg.rhs);
            err_at(s->loc, "cannot infer type for implicit variable");
          }
          s->k = ST_LET;
          s->let.name = nm;
          s->let.len = nlen;
          s->let.ty = s->asg.rhs->ty;
          s->let.init = s->asg.rhs;
          tenv_add_var(venv, nm, nlen, s->let.ty, s->loc);
          if(penv && s->let.ty && s->let.ty->k==TY_PTR){
            PtrOwn st = ptr_state_from_expr(penv, s->let.init);
            penv_add(penv, nm, nlen, st);
          }
          if(benv && s->let.ty && (s->let.ty->k==TY_STRUCT || s->let.ty->k==TY_ARRAY)){
            BorrowKind bk = borrow_kind_from_expr(benv, penv, s->let.init);
            benv_add(benv, nm, nlen, bk);
          }
          return;
        }
      }

      if(s->asg.lhs) sema_expr(G, tenv, curM, curF, venv, penv, benv, s->asg.lhs, in_unsafe);

      if(!s->asg.lhs || !s->asg.lhs->ty) err_at(s->loc, "assignment lhs has no type");
      if(!is_lvalue(s->asg.lhs)) err_at(s->asg.lhs->loc, "assignment requires lvalue");

      TokenKind op = (TokenKind)s->asg.op;

      if(op==TK_PLUSPLUS || op==TK_MINUSMINUS){
        if(!is_int(tenv, s->asg.lhs->ty)) err_at(s->asg.lhs->loc, "++/-- requires integer lvalue");
        return;
      }

      if(s->asg.rhs) sema_expr(G, tenv, curM, curF, venv, penv, benv, s->asg.rhs, in_unsafe);
        if(op==TK_ASSIGN){
          require_assignable(tenv, s->asg.rhs->loc, s->asg.lhs->ty, s->asg.rhs);
          if(s->asg.lhs && s->asg.lhs->k==EX_NAME && s->asg.rhs && s->asg.rhs->ty &&
             s->asg.rhs->ty->k==TY_PTR){
            const char* nm = s->asg.lhs->name.name;
            int nlen = s->asg.lhs->name.len;
            if(!tenv_find_var(venv, nm, nlen) && expr_is_local_addr(tenv, venv, s->asg.rhs)){
              require_unsafe(s->asg.rhs->loc, in_unsafe);
            }
            if(penv){
              PtrOwn st = ptr_state_peek(penv, s->asg.rhs);
          if(st == PTR_OWN_BORROWED && !in_unsafe){
            if(!tenv_find_var(venv, nm, nlen) && !expr_is_static_borrow(s->asg.rhs)){
              err_borrow_expr(s->asg.rhs->loc, "cannot assign borrowed pointer to global", s->asg.rhs, "borrowed values cannot escape");
            }
          }
            }
          }
          if(penv && s->asg.lhs && s->asg.lhs->k==EX_NAME && s->asg.lhs->ty && s->asg.lhs->ty->k==TY_PTR){
            const char* nm = s->asg.lhs->name.name;
            int nlen = s->asg.lhs->name.len;
            PtrOwn st = ptr_state_from_expr(penv, s->asg.rhs);
            penv_set(penv, nm, nlen, st);
          }
          if(benv && s->asg.lhs && s->asg.lhs->k==EX_NAME && s->asg.lhs->ty &&
             (s->asg.lhs->ty->k==TY_STRUCT || s->asg.lhs->ty->k==TY_ARRAY)){
            const char* nm = s->asg.lhs->name.name;
            int nlen = s->asg.lhs->name.len;
            BorrowKind bk = borrow_kind_from_expr(benv, penv, s->asg.rhs);
            benv_set(benv, nm, nlen, bk);
          }
          if(benv && s->asg.lhs && s->asg.lhs->k!=EX_NAME && s->asg.lhs->ty &&
             (s->asg.lhs->ty->k==TY_STRUCT || s->asg.lhs->ty->k==TY_ARRAY)){
            const char* nm = NULL; int nlen = 0;
            if(expr_base_name(s->asg.lhs, &nm, &nlen)){
              BorrowKind cur = benv_find(benv, nm, nlen);
              BorrowKind rhsbk = borrow_kind_from_expr(benv, penv, s->asg.rhs);
              benv_set(benv, nm, nlen, borrow_merge(cur, rhsbk));
            }
          }
        return;
      }

      if(!is_int(tenv, s->asg.lhs->ty)) err_at(s->asg.lhs->loc, "compound assignment expects integer lvalue");
      if(!s->asg.rhs || !s->asg.rhs->ty || !is_int(tenv, s->asg.rhs->ty)) err_at(s->loc, "compound assignment rhs must be integer");
      return;
    }

    case ST_EXPR:
      sema_expr(G, tenv, curM, curF, venv, penv, benv, s->expr.e, in_unsafe);
      return;

    case ST_RET:
      if(s->ret.e) sema_expr(G, tenv, curM, curF, venv, penv, benv, s->ret.e, in_unsafe);
        if(curF && s->ret.e){
          require_assignable(tenv, s->ret.e->loc, curF->ret, s->ret.e);
          if(curF->ret && curF->ret->k==TY_PTR && expr_is_local_addr(tenv, venv, s->ret.e)){
            require_unsafe(s->ret.e->loc, in_unsafe);
          }
          if(curF->ret && curF->ret->k==TY_PTR && penv){
            PtrOwn st = ptr_state_peek(penv, s->ret.e);
            if(st == PTR_OWN_BORROWED && !in_unsafe && !expr_is_static_borrow(s->ret.e)){
              err_borrow_expr(s->ret.e->loc, "cannot return borrowed pointer", s->ret.e, "return an owned pointer or use unsafe");
            }
          }
          if(curF->ret && (curF->ret->k==TY_STRUCT || curF->ret->k==TY_ARRAY) && benv){
            BorrowKind bk = borrow_kind_from_expr(benv, penv, s->ret.e);
            if(bk == BORROW_LOCAL && !in_unsafe){
              err_borrow_expr(s->ret.e->loc, "cannot return borrowed aggregate", s->ret.e, "borrowed values cannot escape");
            }
          }
        }
        return;

    case ST_IF:
      sema_expr(G, tenv, curM, curF, venv, penv, benv, s->iff.cond, in_unsafe);
      if(s->iff.cond && s->iff.cond->ty && !is_int(tenv, s->iff.cond->ty)) err_at(s->iff.cond->loc, "if condition must be integer");
      sema_stmt(G, tenv, curM, curF, venv, penv, benv, s->iff.thenb, in_switch, in_unsafe);
      if(s->iff.elseb) sema_stmt(G, tenv, curM, curF, venv, penv, benv, s->iff.elseb, in_switch, in_unsafe);
      return;

    case ST_WHILE:
      sema_expr(G, tenv, curM, curF, venv, penv, benv, s->wh.cond, in_unsafe);
      if(s->wh.cond && s->wh.cond->ty && !is_int(tenv, s->wh.cond->ty)) err_at(s->wh.cond->loc, "while condition must be integer");
      sema_stmt(G, tenv, curM, curF, venv, penv, benv, s->wh.body, in_switch, in_unsafe);
      return;

    case ST_FOR:
      tenv_push(venv);
      if(penv) penv_push(penv);
      if(benv) benv_push(benv);
      if(s->fr.init) sema_stmt(G, tenv, curM, curF, venv, penv, benv, s->fr.init, in_switch, in_unsafe);
      if(s->fr.cond){
        sema_expr(G, tenv, curM, curF, venv, penv, benv, s->fr.cond, in_unsafe);
        if(s->fr.cond->ty && !is_int(tenv, s->fr.cond->ty)) err_at(s->fr.cond->loc, "for condition must be integer");
      }
      if(s->fr.step) sema_stmt(G, tenv, curM, curF, venv, penv, benv, s->fr.step, in_switch, in_unsafe);
      sema_stmt(G, tenv, curM, curF, venv, penv, benv, s->fr.body, in_switch, in_unsafe);
      if(benv) benv_pop(benv);
      if(penv) penv_pop(penv);
      tenv_pop(venv);
      return;

    case ST_SWITCH: {
      sema_expr(G, tenv, curM, curF, venv, penv, benv, s->sw.cond, in_unsafe);
      if(s->sw.cond && s->sw.cond->ty && !is_int(tenv, s->sw.cond->ty)) err_at(s->sw.cond->loc, "switch condition must be integer");
      int seen_default = 0;
      for(int i=0;i<s->sw.case_n;i++){
        SwitchCase* c = &s->sw.cases[i];
        if(c->val){
          sema_expr(G, tenv, curM, curF, venv, penv, benv, c->val, in_unsafe);
          if(c->val->ty && !is_int(tenv, c->val->ty)) err_at(c->val->loc, "case value must be integer");
        } else {
          if(seen_default) err_at(s->loc, "duplicate default case");
          seen_default = 1;
        }
        if(c->body) sema_stmt(G, tenv, curM, curF, venv, penv, benv, c->body, 1, in_unsafe);
      }
      return;
    }

    case ST_BREAK:
    case ST_CONTINUE:
      return;

    case ST_FALLTHROUGH:
      if(!in_switch) err_at(s->loc, "fallthrough only allowed in switch");
      return;
    case ST_UNSAFE:
      sema_stmt(G, tenv, curM, curF, venv, penv, benv, s->uns.body, in_switch, 1);
      return;

    default:
      err_at(s->loc, "sema: unsupported statement kind");
  }
}

/* -------------------------
 * Module checks
 * ------------------------- */

static void check_duplicate_fns(Module* M){
  for(int i=0;i<M->fn_n;i++){
    FnDecl* a = M->fns[i];
    for(int j=i+1;j<M->fn_n;j++){
      FnDecl* b = M->fns[j];
      if(name_eq(a->name,a->len,b->name,b->len)){
        err_at(a->loc, "duplicate function name in module");
      }
    }
  }
}

static void sema_globals(ModuleGraph* G, TypeEnv* tenv, Module* M){
  (void)G;
  for(int i=0;i<M->global_n;i++){
    struct GlobalVar* g = &M->globals[i];
    if(g->init){
      sema_expr(G, tenv, M, NULL, NULL, NULL, NULL, g->init, is_stdlib_module(M));
      if(!g->init->ty) err_at(g->init->loc, "global initializer has no type");
      require_assignable(tenv, g->init->loc, g->ty, g->init);
    }
  }
}

static void sema_fn(ModuleGraph* G, TypeEnv* tenv, Module* M, FnDecl* fn){
  validate_fn_attrs(fn);
  validate_abi_fn(fn);
  TyEnv venv; memset(&venv,0,sizeof(venv));
  PtrEnv penv; memset(&penv,0,sizeof(penv));
  BorrowEnv benv; memset(&benv,0,sizeof(benv));
  tenv_push(&venv);
  penv_push(&penv);
  benv_push(&benv);

  for(int i=0;i<M->global_n;i++){
    struct GlobalVar* g = &M->globals[i];
    tenv_add_var(&venv, g->name, g->len, g->ty, fn->loc);
  }

  for(int i=0;i<fn->arity;i++){
    tenv_add_var(&venv, fn->pnames[i], fn->plens[i], fn->ptypes[i], fn->loc);
    if(penv.scopes && fn->ptypes[i] && fn->ptypes[i]->k==TY_PTR){
      int fn_borrow = fn_has_attr(fn, "borrow", 6);
      int fn_mut = fn_has_attr(fn, "mut", 3);
      PtrOwn st = (fn_borrow || fn_mut) ? PTR_OWN_BORROWED : PTR_OWN_OWNED;
      penv_add(&penv, fn->pnames[i], fn->plens[i], st);
    }
  }

  if(!fn->is_extern){
    int unsafe_ctx = is_unsafe_default(M, fn);
    sema_stmt(G, tenv, M, fn, &venv, &penv, &benv, fn->body, 0, unsafe_ctx);
    own_check_fn(tenv, M, fn);
  }

  while(venv.sn>0) tenv_pop(&venv);
  free(venv.scopes);
  while(penv.sn>0) penv_pop(&penv);
  free(penv.scopes);
  while(benv.sn>0) benv_pop(&benv);
  free(benv.scopes);
}

void sema_check_module(ModuleGraph* G, TypeEnv* tenv, Module* M){
  check_duplicate_fns(M);
  for(int i=0;i<M->typedef_n;i++){
    TypedefDecl* td = M->typedefs[i];
    if(td) validate_decl_attrs(td->attrs, td->attr_n, td->loc);
  }
  for(int i=0;i<M->enum_n;i++){
    EnumDecl* ed = M->enums[i];
    if(ed) validate_decl_attrs(ed->attrs, ed->attr_n, ed->loc);
  }
  for(int i=0;i<M->struct_n;i++){
    StructDecl* st = M->structs[i];
    if(st) validate_decl_attrs(st->attrs, st->attr_n, st->loc);
  }
  for(int i=0;i<M->global_n;i++){
    struct GlobalVar* g = &M->globals[i];
    if(g){
      SrcLoc loc;
      if(g->init) loc = g->init->loc;
      else { loc.path = M->path; loc.src = M->src; loc.line = 1; loc.col = 1; }
      validate_decl_attrs(g->attrs, g->attr_n, loc);
    }
  }
  sema_globals(G, tenv, M);
  for(int i=0;i<M->fn_n;i++){
    sema_fn(G, tenv, M, M->fns[i]);
  }
}

void sema_check_graph(ModuleGraph* G, TypeEnv* tenv, Module* entry_mod, int require_main){
  for(int i=0;i<tenv->count;i++){
    Type* t = tenv->items[i];
    if(t->k==TY_STRUCT && t->incomplete){
      err_at(t->decl_loc, "incomplete struct type");
    }
  }

  for(int i=0;i<G->mod_n;i++){
    Module* M = G->mods[i];
    if(g_sema_freestanding && is_forbidden_module_freestanding(M)){
      err_at2(M->path, M->src, 1, 1, "module not allowed in freestanding mode");
    }
    sema_check_module(G, tenv, M);
    for(int fi=0; fi<M->fn_n; fi++){
      FnDecl* fn = M->fns[fi];
      if(fn && fn->is_kernel && !is_gpu_target()){
        err_at(fn->loc, "kernel fn requires GPU target (cuda/metal/vulkan/directml)");
      }
      if(fn && fn->is_kernel){
        if(!kernel_sig_ok(tenv, fn)){
          err_at(fn->loc, "kernel fn must be fn(*char)->int");
        }
        if(stmt_has_call(fn->body)){
          err_at(fn->loc, "kernel fn cannot call other functions");
        }
      }
    }
  }

  if(entry_mod){
    int found=0;
    for(int i=0;i<G->mod_n;i++) if(G->mods[i]==entry_mod) { found=1; break; }
    if(!found) sema_check_module(G, tenv, entry_mod);
  }

  if(require_main){
    FnDecl* mainf = find_fn_in_module(entry_mod, "main", 4);
    if(!mainf){
      err_at2(entry_mod->path, entry_mod->src, 1, 1, "missing fn main()");
    }
  }
}

