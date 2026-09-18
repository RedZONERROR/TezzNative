// src/ir.c  (IR v4 aligned: memory IR + array/struct/index/dot + rodata strings)
#include "ir.h"
#include "util.h"
#include "sema.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void* xmalloc(size_t n){ void* p=malloc(n); if(!p) die("out of memory"); return p; }
static void* xrealloc(void* p, size_t n){ void* q=realloc(p,n); if(!q) die("out of memory"); return q; }

static int g_ir_repro = 0;
static int g_ir_async_zero = 0;
static int g_ir_vectorize = 0;
void ir_set_repro(int on){ g_ir_repro = on ? 1 : 0; }
void ir_set_async_zero(int on){ g_ir_async_zero = on ? 1 : 0; }
void ir_set_vectorize(int on){ g_ir_vectorize = on ? 1 : 0; }

/* -------------------------
 * helpers
 * ------------------------- */
static void ir_emit(IRFunc* F, IRIns in){
  if(F->n==F->cap){
    F->cap = F->cap? F->cap*2 : 256;
    F->ins = (IRIns*)xrealloc(F->ins, sizeof(IRIns)*(size_t)F->cap);
  }
  F->ins[F->n++] = in;
}

// --- forward decls for helpers used early ---
static int new_reg(IRFunc* F);
static int ir_gep_imm(IRFunc* F, int base, int off);
static void ir_store(IRFunc* F, int addr, int val, int size);

static void ir_memzero_bytes(IRFunc* F, int addr, int nbytes){
  int z = new_reg(F);
  IRIns iz; memset(&iz,0,sizeof(iz));
  iz.op = I_ICONST; iz.a = z; iz.imm = 0;
  ir_emit(F, iz);

  for(int i=0;i<nbytes;i++){
    int p = ir_gep_imm(F, addr, i);
    ir_store(F, p, z, 1);
  }
}

static int new_reg(IRFunc* F){
  int r = F->next_reg++;
  if(r > F->max_reg) F->max_reg = r;
  return r;
}
static int new_label(IRFunc* F){ return F->next_label++; }

static int env_find(IRFunc* F, const char* name, int len){
  for(int i=F->vcount-1; i>=0; i--){
    if(F->vlen[i]==len && strncmp(F->vname[i], name, (size_t)len)==0) return i;
  }
  return -1;
}

static int flatten_dot_name(Expr* e, char** out, int* out_len){
  if(!e || !out || !out_len) return 0;
  if(e->k==EX_NAME){
    char* s = (char*)xmalloc((size_t)e->name.len + 1);
    memcpy(s, e->name.name, (size_t)e->name.len);
    s[e->name.len] = 0;
    *out = s;
    *out_len = e->name.len;
    return 1;
  }
  if(e->k==EX_DOT){
    char* left = NULL;
    int llen = 0;
    if(!flatten_dot_name(e->dot.base, &left, &llen)) return 0;
    int rlen = e->dot.memlen;
    char* s = (char*)xmalloc((size_t)llen + 1 + (size_t)rlen + 1);
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

static int expr_is_name(Expr* e, const char* name, int len){
  return e && e->k==EX_NAME && e->name.len==len && strncmp(e->name.name, name, (size_t)len)==0;
}

static int fn_has_attr_name(FnDecl* fn, const char* name, int len){
  if(!fn || !fn->attrs || fn->attr_n<=0) return 0;
  for(int i=0;i<fn->attr_n;i++){
    if(fn->attrs[i].len==len && strncmp(fn->attrs[i].name, name, (size_t)len)==0){
      return 1;
    }
  }
  return 0;
}

static int get_for_index_init(Stmt* init, const char** out_name, int* out_len){
  if(!init) return 0;
  if(init->k==ST_LET){
    if(!init->let.init || init->let.init->k!=EX_NUM || init->let.init->num!=0) return 0;
    *out_name = init->let.name;
    *out_len = init->let.len;
    return 1;
  }
  if(init->k==ST_ASSIGN && init->asg.op==TK_ASSIGN){
    if(!init->asg.lhs || init->asg.lhs->k!=EX_NAME) return 0;
    if(!init->asg.rhs || init->asg.rhs->k!=EX_NUM || init->asg.rhs->num!=0) return 0;
    *out_name = init->asg.lhs->name.name;
    *out_len = init->asg.lhs->name.len;
    return 1;
  }
  return 0;
}

static int get_for_limit_expr(Expr* cond, const char* name, int len, Expr** out_expr){
  if(!cond || cond->k!=EX_BIN) return 0;
  if(cond->bin.op!=OP_LT) return 0;
  if(!expr_is_name(cond->bin.a, name, len)) return 0;
  if(!cond->bin.b) return 0;
  if(cond->bin.b->k!=EX_NUM && cond->bin.b->k!=EX_NAME) return 0;
  *out_expr = cond->bin.b;
  return 1;
}

static int stmt_writes_name(Stmt* s, const char* name, int len){
  if(!s) return 0;
  switch(s->k){
    case ST_BLOCK:
      for(int i=0;i<s->block.n;i++) if(stmt_writes_name(s->block.items[i], name, len)) return 1;
      return 0;
    case ST_LET:
      return s->let.len==len && strncmp(s->let.name, name, (size_t)len)==0;
    case ST_ASSIGN:
      if(s->asg.lhs && s->asg.lhs->k==EX_NAME){
        if(s->asg.lhs->name.len==len && strncmp(s->asg.lhs->name.name, name, (size_t)len)==0) return 1;
      }
      return 0;
    case ST_IF:
      return stmt_writes_name(s->iff.thenb, name, len) || stmt_writes_name(s->iff.elseb, name, len);
    case ST_WHILE:
      return stmt_writes_name(s->wh.body, name, len);
    case ST_FOR:
      return stmt_writes_name(s->fr.init, name, len) || stmt_writes_name(s->fr.step, name, len) || stmt_writes_name(s->fr.body, name, len);
    case ST_SWITCH:
      for(int i=0;i<s->sw.case_n;i++) if(stmt_writes_name(s->sw.cases[i].body, name, len)) return 1;
      return 0;
    case ST_UNSAFE:
      return stmt_writes_name(s->uns.body, name, len);
    default:
      return 0;
  }
}

static int stmt_has_break_or_continue(Stmt* s){
  if(!s) return 0;
  switch(s->k){
    case ST_BREAK:
    case ST_CONTINUE:
      return 1;
    case ST_BLOCK:
      for(int i=0;i<s->block.n;i++) if(stmt_has_break_or_continue(s->block.items[i])) return 1;
      return 0;
    case ST_IF:
      return stmt_has_break_or_continue(s->iff.thenb) || stmt_has_break_or_continue(s->iff.elseb);
    case ST_WHILE:
      return stmt_has_break_or_continue(s->wh.body);
    case ST_FOR:
      return stmt_has_break_or_continue(s->fr.init) || stmt_has_break_or_continue(s->fr.step) || stmt_has_break_or_continue(s->fr.body);
    case ST_SWITCH:
      for(int i=0;i<s->sw.case_n;i++) if(stmt_has_break_or_continue(s->sw.cases[i].body)) return 1;
      return 0;
    case ST_UNSAFE:
      return stmt_has_break_or_continue(s->uns.body);
    default:
      return 0;
  }
}

static int step_is_inc1(Stmt* step, const char* name, int len){
  if(!step) return 0;
  if(step->k==ST_ASSIGN && expr_is_name(step->asg.lhs, name, len)){
    if(step->asg.op==TK_PLUSEQ){
      return step->asg.rhs && step->asg.rhs->k==EX_NUM && step->asg.rhs->num==1;
    }
    if(step->asg.op==TK_ASSIGN && step->asg.rhs && step->asg.rhs->k==EX_BIN){
      Expr* rhs = step->asg.rhs;
      if(rhs->bin.op!=OP_ADD) return 0;
      if(!expr_is_name(rhs->bin.a, name, len)) return 0;
      return rhs->bin.b && rhs->bin.b->k==EX_NUM && rhs->bin.b->num==1;
    }
  }
  return 0;
}

static int idx_stride(Expr* idx, const char* name, int len, long long* out_stride){
  if(expr_is_name(idx, name, len)){
    *out_stride = 1;
    return 1;
  }
  if(idx && idx->k==EX_BIN && idx->bin.op==OP_MUL){
    if(expr_is_name(idx->bin.a, name, len) && idx->bin.b && idx->bin.b->k==EX_NUM){
      *out_stride = idx->bin.b->num;
      return 1;
    }
    if(expr_is_name(idx->bin.b, name, len) && idx->bin.a && idx->bin.a->k==EX_NUM){
      *out_stride = idx->bin.a->num;
      return 1;
    }
  }
  return 0;
}

static int match_vector_body(Stmt* body, const char* name, int len, Expr** out_lhs, Expr** out_a, Expr** out_b, int* out_op, long long* out_stride){
  if(!body) return 0;
  if(body->k!=ST_ASSIGN) return 0;
  if(body->asg.op!=TK_ASSIGN) return 0;
  if(!body->asg.lhs || body->asg.lhs->k!=EX_INDEX) return 0;
  Expr* lhs = body->asg.lhs;
  long long stride = 0;
  if(!lhs->index.idx || !idx_stride(lhs->index.idx, name, len, &stride)) return 0;
  if(stride <= 0) return 0;
  if(!body->asg.rhs || body->asg.rhs->k!=EX_BIN) return 0;
  Expr* rhs = body->asg.rhs;
  if(rhs->bin.op!=OP_ADD && rhs->bin.op!=OP_SUB && rhs->bin.op!=OP_MUL) return 0;
  if(!rhs->bin.a || rhs->bin.a->k!=EX_INDEX) return 0;
  if(!rhs->bin.b || rhs->bin.b->k!=EX_INDEX) return 0;
  long long stride_a = 0;
  long long stride_b = 0;
  if(!idx_stride(rhs->bin.a->index.idx, name, len, &stride_a)) return 0;
  if(!idx_stride(rhs->bin.b->index.idx, name, len, &stride_b)) return 0;
  if(stride_a != stride || stride_b != stride) return 0;
  *out_lhs = lhs;
  *out_a = rhs->bin.a;
  *out_b = rhs->bin.b;
  *out_op = (int)rhs->bin.op;
  *out_stride = stride;
  return 1;
}

static void env_add(IRFunc* F, const char* name, int len, int addr_reg, Type* ty){
  if(F->vcount==F->vcap){
    F->vcap = F->vcap? F->vcap*2 : 64;
    F->vname=(const char**)xrealloc(F->vname,sizeof(char*)*(size_t)F->vcap);
    F->vlen =(int*)xrealloc(F->vlen, sizeof(int)*(size_t)F->vcap);
    F->vaddr=(int*)xrealloc(F->vaddr,sizeof(int)*(size_t)F->vcap);
    F->vty  =(Type**)xrealloc(F->vty,  sizeof(Type*)*(size_t)F->vcap);
  }
  F->vname[F->vcount]=name;
  F->vlen [F->vcount]=len;
  F->vaddr[F->vcount]=addr_reg;
  F->vty  [F->vcount]=ty;
  F->vcount++;
}

static void loop_push(IRFunc* F, int l_break, int l_continue){
  if(F->lcount==F->lcap){
    F->lcap = F->lcap? F->lcap*2 : 16;
    F->brk  = (int*)xrealloc(F->brk,  sizeof(int)*(size_t)F->lcap);
    F->cont = (int*)xrealloc(F->cont, sizeof(int)*(size_t)F->lcap);
  }
  F->brk[F->lcount]  = l_break;
  F->cont[F->lcount] = l_continue;
  F->lcount++;
}
static void loop_pop(IRFunc* F){ if(F->lcount>0) F->lcount--; }
static int loop_break(IRFunc* F){ return (F->lcount>0)? F->brk[F->lcount-1] : -1; }
static int loop_continue(IRFunc* F){ return (F->lcount>0)? F->cont[F->lcount-1] : -1; }

/* -------------------------
 * BinOp mapping
 * ------------------------- */
static IRBin binop_to_bin(BinOp op){
  switch(op){
    case OP_ADD: return B_ADD;
    case OP_SUB: return B_SUB;
    case OP_MUL: return B_MUL;
    case OP_DIV: return B_DIV;
    case OP_MOD: return B_MOD;
    case OP_BAND: return B_AND;
    case OP_BOR:  return B_OR;
    case OP_BXOR: return B_XOR;
    case OP_SHL:  return B_SHL;
    case OP_SHR:  return B_SHR;
    case OP_LAND: return B_AND; // v4: no short-circuit yet
    case OP_LOR:  return B_OR;
    default: return B_ADD;
  }
}
static int binop_is_cmp(BinOp op){
  switch(op){
    case OP_EQ: case OP_NEQ:
    case OP_LT: case OP_LTE:
    case OP_GT: case OP_GTE:
      return 1;
    default:
      return 0;
  }
}

static int is_f64(Type* t){ return t && t->k==TY_F64; }
static int is_i64_ty(Type* t){ return t && t->k==TY_I64; }
static int is_ptr(Type* t){ return t && t->k==TY_PTR; }
static int is_array(Type* t){ return t && t->k==TY_ARRAY; }
static int expr_is_stringy_ir(Expr* e){
  if(!e) return 0;
  if(e->k==EX_STR) return 1;
  if(e->ty && e->ty->k==TY_PTR && e->ty->elem && e->ty->elem->k==TY_U8) return 1;
  if(e->ty && e->ty->k==TY_ARRAY && e->ty->elem && e->ty->elem->k==TY_U8) return 1;
  return 0;
}

typedef struct {
  Module* cur;
  Module* root;
} IRGenCtx;

static int mod_has_fn(Module* M, const char* name, int len){
  if(!M) return 0;
  for(int i=0;i<M->fn_n;i++){
    FnDecl* f = M->fns[i];
    if(f->is_extern) continue;
    if(f->len==len && strncmp(f->name,name,(size_t)len)==0) return 1;
  }
  return 0;
}

static int mod_has_global(Module* M, const char* name, int len){
  if(!M) return 0;
  for(int i=0;i<M->global_n;i++){
    struct GlobalVar* g = &M->globals[i];
    if(g->is_extern) continue;
    if(g->len==len && strncmp(g->name,name,(size_t)len)==0) return 1;
  }
  return 0;
}

static char* mod_prefix_name(Module* M, const char* name, int len, int* out_len){
  if(!M || !M->mname) return NULL;
  int ml = M->mlen;
  int n = ml + 1 + len;
  char* out = (char*)xmalloc((size_t)n + 1);
  memcpy(out, M->mname, (size_t)ml);
  out[ml] = '.';
  memcpy(out + ml + 1, name, (size_t)len);
  out[n] = 0;
  if(out_len) *out_len = n;
  return out;
}
static IRCmp binop_to_cmp(BinOp op){
  switch(op){
    case OP_EQ:  return C_EQ;
    case OP_NEQ: return C_NEQ;
    case OP_LT:  return C_LT;
    case OP_LTE: return C_LTE;
    case OP_GT:  return C_GT;
    case OP_GTE: return C_GTE;
    default:     return C_EQ;
  }
}

/* -------------------------
 * String pool
 * ------------------------- */
static int str_eq(const char* a, int alen, const char* b, int blen){
  return alen==blen && memcmp(a,b,(size_t)alen)==0;
}
static int ir_intern_cstr(IRModule* M, const char* s){
  int len = (int)strlen(s);
  for(int i=0;i<M->str_n;i++){
    if(str_eq(M->strs[i].bytes, M->strs[i].len, s, len)) return i;
  }
  if(M->str_n==M->str_cap){
    M->str_cap = M->str_cap? M->str_cap*2 : 32;
    M->strs = (IRString*)xrealloc(M->strs, sizeof(IRString)*(size_t)M->str_cap);
  }
  char* copy = (char*)xmalloc((size_t)len + 1);
  memcpy(copy, s, (size_t)len+1);
  M->strs[M->str_n] = (IRString){copy, len};
  return M->str_n++;
}

/* -------------------------
 * Forward decls
 * ------------------------- */
static int  gen_expr(IRModule* M, TypeEnv* tenv, IRFunc* F, IRGenCtx* ctx, Expr* e, Type* expect);
static int  gen_lvalue(IRModule* M, TypeEnv* tenv, IRFunc* F, IRGenCtx* ctx, Expr* e, Type** out_ty);
static void gen_stmt(IRModule* M, TypeEnv* tenv, IRFunc* F, IRGenCtx* ctx, Stmt* s);

/* -------------------------
 * Memory helpers
 * ------------------------- */
static int ir_alloca(IRFunc* F, int size, int align){
  int dst=new_reg(F);
  IRIns in; memset(&in,0,sizeof(in));
  in.op=I_ALLOCA; in.a=dst; in.size=size; in.align=align;
  ir_emit(F,in);
  return dst;
}
static void ir_store(IRFunc* F, int addr, int val, int size){
  IRIns in; memset(&in,0,sizeof(in));
  in.op=I_STORE; in.a=addr; in.b=val; in.size=size;
  ir_emit(F,in);
}
static int ir_load(IRFunc* F, int addr, Type* ty){
  int dst=new_reg(F);
  IRIns in; memset(&in,0,sizeof(in));
  in.op=I_LOAD;
  in.a=dst;
  in.b=addr;
  in.size=ty ? type_size(ty) : 8;
  in.is_unsigned=type_is_unsigned_int(ty);
  ir_emit(F,in);
  return dst;
}
static int ir_int_width_cast(IRFunc* F, int v, Type* to){
  int bits = type_int_bits(to);
  if(bits <= 0 || bits >= 64) return v;
  if(type_is_unsigned_int(to)){
    int mask = new_reg(F);
    IRIns ic; memset(&ic,0,sizeof(ic));
    ic.op=I_ICONST; ic.a=mask; ic.imm=(1LL << bits) - 1LL;
    ir_emit(F,ic);
    int dst = new_reg(F);
    IRIns bin; memset(&bin,0,sizeof(bin));
    bin.op=I_BIN; bin.a=dst; bin.b=v; bin.c=mask; bin.binop=B_AND;
    ir_emit(F,bin);
    return dst;
  }

  int sh = 64 - bits;
  int sr = new_reg(F);
  IRIns ic; memset(&ic,0,sizeof(ic));
  ic.op=I_ICONST; ic.a=sr; ic.imm=sh;
  ir_emit(F,ic);

  int left = new_reg(F);
  IRIns shl; memset(&shl,0,sizeof(shl));
  shl.op=I_BIN; shl.a=left; shl.b=v; shl.c=sr; shl.binop=B_SHL;
  ir_emit(F,shl);

  int dst = new_reg(F);
  IRIns shr; memset(&shr,0,sizeof(shr));
  shr.op=I_BIN; shr.a=dst; shr.b=left; shr.c=sr; shr.binop=B_SHR;
  ir_emit(F,shr);
  return dst;
}
static int ir_gep_imm(IRFunc* F, int base, int off){
  int dst=new_reg(F);
  IRIns in; memset(&in,0,sizeof(in));
  in.op=I_GEP; in.a=dst; in.b=base; in.imm=off;
  ir_emit(F,in);
  return dst;
}

/* -------------------------
 * Calls
 * ------------------------- */
static int gen_call(IRModule* M, TypeEnv* tenv, IRFunc* F, IRGenCtx* ctx, Expr* e){
  const char* name = NULL;
  int nlen = 0;
  int callee_reg = -1;
  int name_owned = 0;

  if(e->call.callee->k==EX_NAME){
    int idx = env_find(F, e->call.callee->name.name, e->call.callee->name.len);
    if(idx>=0){
      Type* vty = F->vty[idx];
      if(vty && vty->k==TY_PTR && vty->elem && vty->elem->k==TY_FN){
        callee_reg = gen_expr(M, tenv, F, ctx, e->call.callee, NULL);
      } else {
        name = e->call.callee->name.name;
        nlen = e->call.callee->name.len;
      }
    } else {
      name = e->call.callee->name.name;
      nlen = e->call.callee->name.len;
    }
  } else if(e->call.callee->k==EX_DOT){
    char* mod = NULL;
    int modlen = 0;
    if(flatten_dot_name(e->call.callee->dot.base, &mod, &modlen)){
      Str sb; sb_init(&sb);
      sb_printf(&sb, "%.*s.%.*s", modlen, mod, e->call.callee->dot.memlen, e->call.callee->dot.mem);
      name = sb.data;
      nlen = sb.len;
      name_owned = 1;
      free(mod);
    } else {
      // Fall back to name-only if we can't flatten.
      name = e->call.callee->dot.mem;
      nlen = e->call.callee->dot.memlen;
    }
  } else {
    callee_reg = gen_expr(M, tenv, F, ctx, e->call.callee, NULL);
  }

  if(callee_reg < 0 && name && ctx && ctx->cur && ctx->root && ctx->cur != ctx->root){
    if(mod_has_fn(ctx->cur, name, nlen)){
      char* full = mod_prefix_name(ctx->cur, name, nlen, &nlen);
      if(full){
        name = full;
        name_owned = 1;
      }
    }
  }

  if(g_ir_async_zero && callee_reg < 0 && name && e->call.callee->k==EX_NAME){
    if(nlen==5 && strncmp(name,"async",5)==0){
      int callee = gen_expr(M, tenv, F, ctx, e->call.args[0], NULL);
      int dst = new_reg(F);
      IRIns in; memset(&in,0,sizeof(in));
      in.op = I_CALLPTR; in.a = dst; in.b = callee;
      in.args = NULL; in.argc = 0;
      ir_emit(F, in);
      return dst;
    }
    if(nlen==5 && strncmp(name,"await",5)==0){
      int v = gen_expr(M, tenv, F, ctx, e->call.args[0], NULL);
      return v;
    }
  }

  if(callee_reg < 0 && name && nlen==5 && strncmp(name,"input",5)==0){
    int argc = e->call.argc;
    if(argc > 2) goto call_direct;
    int* args = NULL;
    if(argc >= 1){
      args = (int*)xmalloc(sizeof(int) * (size_t)argc);
      int pr = gen_expr(M, tenv, F, ctx, e->call.args[0], NULL);
      args[0] = pr;
      if(argc == 2){
        args[1] = gen_expr(M, tenv, F, ctx, e->call.args[1], NULL);
      }
    }
    int dst = new_reg(F);
    IRIns r; memset(&r,0,sizeof(r));
    r.op = I_CALL; r.a = dst;
    r.name = "input"; r.nlen = 5; r.args = args; r.argc = argc;
    ir_emit(F, r);
    return dst;
  }

  if(callee_reg < 0 && name && nlen==3 && strncmp(name,"say",3)==0){
    int argc = e->call.argc;
    int use_multi = (argc > 1);
    if(argc == 1){
      Expr* a = e->call.args[0];
      if(a && (expr_is_stringy_ir(a) || is_f64(a->ty))) use_multi = 1;
    }
    if(!use_multi) goto call_direct;

    // use say_multi helper (vals, tags, argc)
    int vals = ir_alloca(F, argc * 8, 8);
    int tags = ir_alloca(F, argc * 8, 8);
    for(int i=0;i<argc;i++){
      Expr* a = e->call.args[i];
      int v = gen_expr(M, tenv, F, ctx, a, NULL);
      int pv = ir_gep_imm(F, vals, i * 8);
      ir_store(F, pv, v, 8);

      int tagv = new_reg(F);
      IRIns ic; memset(&ic,0,sizeof(ic));
      if(expr_is_stringy_ir(a)) ic.imm = 2;
      else if(a && is_f64(a->ty)) ic.imm = 1;
      else ic.imm = 0;
      ic.op = I_ICONST; ic.a = tagv;
      ir_emit(F, ic);
      int pt = ir_gep_imm(F, tags, i * 8);
      ir_store(F, pt, tagv, 8);
    }
    int cargc = new_reg(F);
    IRIns ic; memset(&ic,0,sizeof(ic));
    ic.op = I_ICONST; ic.a = cargc; ic.imm = argc;
    ir_emit(F, ic);

    int* callargs = (int*)xmalloc(sizeof(int)*3);
    callargs[0] = vals;
    callargs[1] = tags;
    callargs[2] = cargc;
    int dst = new_reg(F);
    IRIns call; memset(&call,0,sizeof(call));
    call.op = I_CALL; call.a = dst;
    call.name = "say_multi"; call.nlen = 9;
    call.args = callargs; call.argc = 3;
    ir_emit(F, call);
    return dst;
  }

call_direct:
{
  int argc = e->call.argc;
  int* args = NULL;
  if(argc){
    args = (int*)xmalloc(sizeof(int)*(size_t)argc);
    for(int i=0;i<argc;i++){
      args[i] = gen_expr(M, tenv, F, ctx, e->call.args[i], NULL);
    }
  }

  int dst=new_reg(F);
  IRIns in; memset(&in,0,sizeof(in));
  if(callee_reg>=0){
    in.op=I_CALLPTR; in.a=dst; in.b=callee_reg;
    in.args=args; in.argc=argc;
  } else {
    in.op=I_CALL; in.a=dst;
    in.name=name; in.nlen=nlen; in.name_owned=name_owned;
    in.args=args; in.argc=argc;
  }
  ir_emit(F,in);
  return dst;
}
}

/* -------------------------
 * LVALUE: returns address register
 * ------------------------- */
static int gen_lvalue(IRModule* M, TypeEnv* tenv, IRFunc* F, IRGenCtx* ctx, Expr* e, Type** out_ty){
  if(!e) err_at2("?", "?", 1,1, "IR: null lvalue");

  switch(e->k){
    case EX_NAME: {
      int idx = env_find(F, e->name.name, e->name.len);
      if(idx>=0){
        if(out_ty) *out_ty = F->vty[idx];
        return F->vaddr[idx];
      }
      // global: address of symbol
      int r=new_reg(F);
      IRIns in; memset(&in,0,sizeof(in));
      in.op=I_ADDRSYM; in.a=r;
      const char* nm = e->name.name;
      int nlen = e->name.len;
      int owned = 0;
      if(ctx && ctx->cur && ctx->root && ctx->cur != ctx->root &&
         (mod_has_global(ctx->cur, nm, nlen) || mod_has_fn(ctx->cur, nm, nlen))){
        char* full = mod_prefix_name(ctx->cur, nm, nlen, &nlen);
        if(full){
          nm = full;
          owned = 1;
        }
      }
      in.name=nm; in.nlen=nlen; in.name_owned=owned;
      ir_emit(F,in);
      if(out_ty){
        if(ctx && ctx->cur){
          for(int i=0;i<ctx->cur->global_n;i++){
            struct GlobalVar* g = &ctx->cur->globals[i];
            if(g->len==e->name.len && strncmp(g->name, e->name.name, (size_t)g->len)==0){
              *out_ty = g->ty;
              break;
            }
          }
        } else {
          for(int i=0;i<M->global_n;i++){
            IRGlobal* g=&M->globals[i];
            if(g->len==e->name.len && strncmp(g->name,e->name.name,(size_t)g->len)==0){
              *out_ty = g->ty;
              break;
            }
          }
        }
      }
      return r;
    }

    case EX_DEREF: {
      int addr = gen_expr(M, tenv, F, ctx, e->un.e, NULL);
      if(out_ty && e->un.e && e->un.e->ty && e->un.e->ty->k==TY_PTR) *out_ty = e->un.e->ty->elem;
      return addr;
    }

    case EX_INDEX: {
      int base = gen_expr(M, tenv, F, ctx, e->index.base, NULL);
      Type* base_ty = e->index.base ? e->index.base->ty : NULL;

      Type* elem = NULL;
      if(base_ty && base_ty->k==TY_ARRAY) elem = base_ty->elem;
      else if(base_ty && base_ty->k==TY_PTR) elem = base_ty->elem;
      else elem = tenv->b.ty_i64;

      int esz = type_size(elem);

      int idxr = gen_expr(M, tenv, F, ctx, e->index.idx, tenv->b.ty_i64);

      int cs = new_reg(F);
      IRIns ic; memset(&ic,0,sizeof(ic));
      ic.op=I_ICONST; ic.a=cs; ic.imm=esz;
      ir_emit(F,ic);

      int mulr = new_reg(F);
      IRIns mul; memset(&mul,0,sizeof(mul));
      mul.op=I_BIN; mul.a=mulr; mul.b=idxr; mul.c=cs; mul.binop=B_MUL;
      ir_emit(F,mul);

      int addr = new_reg(F);
      IRIns add; memset(&add,0,sizeof(add));
      add.op=I_BIN; add.a=addr; add.b=base; add.c=mulr; add.binop=B_ADD;
      ir_emit(F,add);

      if(out_ty) *out_ty = elem;
      return addr;
    }

    case EX_DOT: {
      if(!e->dot.base || !e->dot.base->ty || e->dot.base->ty->k!=TY_STRUCT){
        err_at(e->loc, "IR: dot lvalue requires struct base");
      }
      Type* st = e->dot.base->ty;
      Field* f = type_find_field(st, e->dot.mem, e->dot.memlen);
      if(!f) err_at(e->loc, "IR: unknown struct field");

      Type* dummy=NULL;
      int base_addr = gen_lvalue(M, tenv, F, ctx, e->dot.base, &dummy);
      int addr = ir_gep_imm(F, base_addr, f->off);
      if(out_ty) *out_ty = f->ty;
      return addr;
    }

    default:
      err_at(e->loc, "IR: unsupported lvalue");
      return 0;
  }
}

/* -------------------------
 * EXPR (rvalue)
 * ------------------------- */
static int gen_expr(IRModule* M, TypeEnv* tenv, IRFunc* F, IRGenCtx* ctx, Expr* e, Type* expect){
  if(!e) err_at2("?", "?", 1,1, "IR: null expr");

  switch(e->k){
    case EX_NUM: {
      int r=new_reg(F);
      IRIns in; memset(&in,0,sizeof(in));
      in.op=I_ICONST; in.a=r; in.imm=e->num;
      ir_emit(F,in);
      return r;
    }

    case EX_FLOAT: {
      int r=new_reg(F);
      IRIns in; memset(&in,0,sizeof(in));
      in.op=I_FCONST; in.a=r;
      double x=e->f;
      long long bits=0;
      memcpy(&bits, &x, sizeof(double));
      in.immf = bits;
      ir_emit(F,in);
      return r;
    }

    case EX_STR: {
      // if expected is [u8;N], materialize stack array with 0 terminator
      if(expect && expect->k==TY_ARRAY && expect->elem && expect->elem->k==TY_U8){
        int n = (int)expect->len;
        int addr = ir_alloca(F, n, 1);

        int sl = (int)strlen(e->str);
        int w = (n>0) ? (sl < (n-1) ? sl : (n-1)) : 0;

        for(int i=0;i<w;i++){
          int ch=new_reg(F);
          IRIns ic; memset(&ic,0,sizeof(ic));
          ic.op=I_ICONST; ic.a=ch; ic.imm=(unsigned char)e->str[i];
          ir_emit(F,ic);

          int p = ir_gep_imm(F, addr, i);
          ir_store(F, p, ch, 1);
        }

        if(n>0){
          int z=new_reg(F);
          IRIns iz; memset(&iz,0,sizeof(iz));
          iz.op=I_ICONST; iz.a=z; iz.imm=0;
          ir_emit(F,iz);

          int term_off = (w < n) ? w : (n-1);
          int pz = ir_gep_imm(F, addr, term_off);
          ir_store(F, pz, z, 1);
        }

        return addr;
      }

      int sid = ir_intern_cstr(M, e->str);
      int r=new_reg(F);
      IRIns in; memset(&in,0,sizeof(in));
      in.op=I_SCONST; in.a=r; in.sid=sid;
      ir_emit(F,in);
      return r;
    }

    case EX_NAME: {
      int idx = env_find(F, e->name.name, e->name.len);
      if(idx<0){
        // function/global symbol -> address
        Type* gty = NULL;
        if(ctx && ctx->cur){
          for(int i=0;i<ctx->cur->global_n;i++){
            struct GlobalVar* g = &ctx->cur->globals[i];
            if(g->len==e->name.len && strncmp(g->name, e->name.name, (size_t)g->len)==0){
              gty = g->ty;
              break;
            }
          }
        }
        const char* nm = e->name.name;
        int nlen = e->name.len;
        int owned = 0;
        if(ctx && ctx->cur && ctx->root && ctx->cur != ctx->root &&
           (mod_has_global(ctx->cur, nm, nlen) || mod_has_fn(ctx->cur, nm, nlen))){
          char* full = mod_prefix_name(ctx->cur, nm, nlen, &nlen);
          if(full){
            nm = full;
            owned = 1;
          }
        }
        int addr=new_reg(F);
        IRIns in; memset(&in,0,sizeof(in));
        in.op=I_ADDRSYM; in.a=addr;
        in.name=nm; in.nlen=nlen; in.name_owned=owned;
        ir_emit(F,in);
        if(e->ty && e->ty->k==TY_PTR && e->ty->elem && e->ty->elem->k==TY_FN){
          return addr;
        }
        if(gty && (gty->k==TY_ARRAY || gty->k==TY_STRUCT)){
          return addr;
        }
        if(e->ty && (e->ty->k==TY_ARRAY || e->ty->k==TY_STRUCT)){
          return addr;
        }
        return ir_load(F, addr, gty ? gty : tenv->b.ty_i64);
      }
      int addr = F->vaddr[idx];
      Type* ty  = F->vty[idx];

      if(ty && (ty->k==TY_ARRAY || ty->k==TY_STRUCT)){
        return addr; // aggregates are addresses
      }
      return ir_load(F, addr, ty ? ty : tenv->b.ty_i64);
    }

    case EX_BIN: {
      Type* ta = e->bin.a ? e->bin.a->ty : NULL;
      Type* tb = e->bin.b ? e->bin.b->ty : NULL;
      int pa = ta && (is_ptr(ta) || is_array(ta));
      int pb = tb && (is_ptr(tb) || is_array(tb));

      if((e->bin.op==OP_ADD || e->bin.op==OP_SUB) && (pa || pb)){
        // pointer arithmetic
        Expr* pexpr = pa ? e->bin.a : e->bin.b;
        Expr* iexpr = pa ? e->bin.b : e->bin.a;
        Type* pt = pexpr->ty;
        Type* et = NULL;
        if(pt && pt->k==TY_PTR) et = pt->elem;
        if(pt && pt->k==TY_ARRAY) et = pt->elem;
        int esz = et ? type_size(et) : 1;

        int base = gen_expr(M, tenv, F, ctx, pexpr, NULL);
        int idx  = gen_expr(M, tenv, F, ctx, iexpr, tenv->b.ty_i64);

        if(pa && pb && e->bin.op==OP_SUB){
          int dst = new_reg(F);
          IRIns sub; memset(&sub,0,sizeof(sub));
          sub.op=I_BIN; sub.a=dst; sub.b=base; sub.c=idx; sub.binop=B_SUB;
          ir_emit(F,sub);
          if(esz > 1){
            int c = new_reg(F);
            IRIns ic; memset(&ic,0,sizeof(ic));
            ic.op=I_ICONST; ic.a=c; ic.imm=esz;
            ir_emit(F,ic);
            int q = new_reg(F);
            IRIns div; memset(&div,0,sizeof(div));
            div.op=I_BIN; div.a=q; div.b=dst; div.c=c; div.binop=B_DIV;
            ir_emit(F,div);
            return q;
          }
          return dst;
        }

        int scale = idx;
        if(esz != 1){
          int c = new_reg(F);
          IRIns ic; memset(&ic,0,sizeof(ic));
          ic.op=I_ICONST; ic.a=c; ic.imm=esz;
          ir_emit(F,ic);
          int mulr = new_reg(F);
          IRIns mul; memset(&mul,0,sizeof(mul));
          mul.op=I_BIN; mul.a=mulr; mul.b=idx; mul.c=c; mul.binop=B_MUL;
          ir_emit(F,mul);
          scale = mulr;
        }

        int dst = new_reg(F);
        IRIns bin; memset(&bin,0,sizeof(bin));
        bin.op=I_BIN; bin.a=dst; bin.b=base; bin.c=scale;
        bin.binop = (e->bin.op==OP_ADD) ? B_ADD : B_SUB;
        ir_emit(F,bin);
        return dst;
      }

      int use_f = (e->bin.a && is_f64(e->bin.a->ty)) || (e->bin.b && is_f64(e->bin.b->ty));
      int a = gen_expr(M, tenv, F, ctx, e->bin.a, use_f ? tenv->b.ty_f64 : tenv->b.ty_i64);
      int b = gen_expr(M, tenv, F, ctx, e->bin.b, use_f ? tenv->b.ty_f64 : tenv->b.ty_i64);
      int dst=new_reg(F);
      IRIns in; memset(&in,0,sizeof(in));
      if(use_f){
        if(binop_is_cmp(e->bin.op)){
          in.op=I_FCMP; in.a=dst; in.b=a; in.c=b; in.cmpop=binop_to_cmp(e->bin.op);
        } else {
          in.op=I_FBIN; in.a=dst; in.b=a; in.c=b;
          switch(e->bin.op){
            case OP_ADD: in.fop=F_ADD; break;
            case OP_SUB: in.fop=F_SUB; break;
            case OP_MUL: in.fop=F_MUL; break;
            case OP_DIV: in.fop=F_DIV; break;
            default: err_at(e->loc, "IR: unsupported float binop"); break;
          }
        }
      } else {
        if(binop_is_cmp(e->bin.op)){
          in.op=I_CMP; in.a=dst; in.b=a; in.c=b; in.cmpop=binop_to_cmp(e->bin.op);
        } else {
          in.op=I_BIN; in.a=dst; in.b=a; in.c=b; in.binop=binop_to_bin(e->bin.op);
        }
      }
      ir_emit(F,in);
      return dst;
    }

    case EX_CAST: {
      int v = gen_expr(M, tenv, F, ctx, e->cast.e, NULL);
      Type* from = e->cast.e ? e->cast.e->ty : NULL;
      Type* to = e->cast.to;
      if(from && to && type_is_int(from) && type_is_int(to)){
        return ir_int_width_cast(F, v, to);
      }
      if(from && to && is_f64(from) && !is_f64(to)){
        int dst = new_reg(F);
        IRIns in; memset(&in,0,sizeof(in));
        in.op=I_F2I; in.a=dst; in.b=v;
        ir_emit(F,in);
        return dst;
      }
      if(from && to && !is_f64(from) && is_f64(to)){
        int dst = new_reg(F);
        IRIns in; memset(&in,0,sizeof(in));
        in.op=I_I2F; in.a=dst; in.b=v;
        ir_emit(F,in);
        return dst;
      }
      return v;
    }

    case EX_CALL:
      return gen_call(M, tenv, F, ctx, e);

    case EX_ADDR: {
      Type* lty=NULL;
      int addr = gen_lvalue(M, tenv, F, ctx, e->un.e, &lty);
      return addr;
    }

    case EX_DEREF: {
      int addr = gen_expr(M, tenv, F, ctx, e->un.e, NULL);
      Type* ty = (e->un.e && e->un.e->ty && e->un.e->ty->k==TY_PTR) ? e->un.e->ty->elem : tenv->b.ty_i64;
      return ir_load(F, addr, ty);
    }

    case EX_INDEX: {
      Type* elty=NULL;
      int addr = gen_lvalue(M, tenv, F, ctx, e, &elty);
      return ir_load(F, addr, elty ? elty : tenv->b.ty_i64);
    }

    case EX_DOT: {
      Type* fty=NULL;
      int addr = gen_lvalue(M, tenv, F, ctx, e, &fty);
      return ir_load(F, addr, fty ? fty : tenv->b.ty_i64);
    }

    case EX_ARRAY_LIT: {
      Type* aty = expect ? expect : e->ty;
      if(!aty || aty->k!=TY_ARRAY) err_at(e->loc, "IR: array literal needs array type");

      Type* elem = aty->elem;
      int esz = type_size(elem);
      int n = (int)aty->len;

      int addr = ir_alloca(F, type_size(aty), type_align(aty));

      int kmax = e->arr.n < n ? e->arr.n : n;
      for(int i=0;i<kmax;i++){
        int v = gen_expr(M, tenv, F, ctx, e->arr.items[i], elem);
        int p = ir_gep_imm(F, addr, i*esz);
        ir_store(F, p, v, esz);
      }
      return addr;
    }

    case EX_STRUCT_LIT: {
      Type* sty = expect ? expect : e->ty;
      if(!sty || sty->k!=TY_STRUCT) err_at(e->loc, "IR: struct literal needs struct type");

      int sz = type_size(sty);
      int addr = ir_alloca(F, sz, type_align(sty));

      // zero init: safe for ANY size (byte loop)
      ir_memzero_bytes(F, addr, sz);

      for(int i=0;i<e->stlit.n;i++){
        Field* f = type_find_field(sty, e->stlit.f[i], e->stlit.flen[i]);
        if(!f) err_at(e->loc, "IR: unknown field in struct literal");

        int v = gen_expr(M, tenv, F, ctx, e->stlit.v[i], f->ty);
        int p = ir_gep_imm(F, addr, f->off);
        ir_store(F, p, v, type_size(f->ty));
      }
      return addr;
    }

    default:
      err_at(e->loc, "IR: unsupported expression (v4)");
      return 0;
  }
}

/* -------------------------
 * Statements
 * ------------------------- */
static void gen_stmt(IRModule* M, TypeEnv* tenv, IRFunc* F, IRGenCtx* ctx, Stmt* s){
  switch(s->k){
    case ST_BLOCK: {
      int saved = F->vcount;
      F->scope_depth++;

      for(int i=0;i<s->block.n;i++){
        gen_stmt(M, tenv, F, ctx, s->block.items[i]);
      }

      F->scope_depth--;
      if(F->scope_depth > 0){
        F->vcount = saved;
      }
      return;
    }

    case ST_LET: {
      Type* ty = s->let.ty ? s->let.ty : (s->let.init ? s->let.init->ty : tenv->b.ty_i64);
      if(!ty) ty = tenv->b.ty_i64;

      int addr = ir_alloca(F, type_size(ty), type_align(ty));
      env_add(F, s->let.name, s->let.len, addr, ty);

      if(s->let.init){
        if(ty->k==TY_ARRAY || ty->k==TY_STRUCT){
          int src = gen_expr(M, tenv, F, ctx, s->let.init, ty);
          if(src != addr){
            int szr=new_reg(F);
            IRIns ic; memset(&ic,0,sizeof(ic));
            ic.op=I_ICONST; ic.a=szr; ic.imm=type_size(ty);
            ir_emit(F,ic);

            int* callargs=(int*)xmalloc(sizeof(int)*3);
            callargs[0]=addr; callargs[1]=src; callargs[2]=szr;

            int dst=new_reg(F);
            IRIns call; memset(&call,0,sizeof(call));
            call.op=I_CALL; call.a=dst;
            call.name="memcpy"; call.nlen=6;
            call.args=callargs; call.argc=3;
            ir_emit(F,call);
          }
        } else {
          int v = gen_expr(M, tenv, F, ctx, s->let.init, ty);
          ir_store(F, addr, v, type_size(ty));
        }
      } else {
        int sz = type_size(ty);
        if(ty->k==TY_ARRAY || ty->k==TY_STRUCT){
          ir_memzero_bytes(F, addr, sz);
        } else {
          int z=new_reg(F);
          IRIns ic; memset(&ic,0,sizeof(ic));
          ic.op=I_ICONST; ic.a=z; ic.imm=0;
          ir_emit(F,ic);
          ir_store(F, addr, z, sz);
        }
      }
      return;
    }

    case ST_ASSIGN: {
      Type* lty=NULL;
      int laddr = gen_lvalue(M, tenv, F, ctx, s->asg.lhs,&lty);
      int lsz = type_size(lty ? lty : tenv->b.ty_i64);

      if(s->asg.op==TK_ASSIGN){
        int v = gen_expr(M, tenv, F, ctx, s->asg.rhs, lty);
        ir_store(F, laddr, v, lsz);
        return;
      }

      int cur = ir_load(F, laddr, lty ? lty : tenv->b.ty_i64);
      int rhs = gen_expr(M, tenv, F, ctx, s->asg.rhs, lty);
      int dst=new_reg(F);

      IRBin bop=B_ADD;
      switch(s->asg.op){
        case TK_PLUSEQ: bop=B_ADD; break;
        case TK_MINUSEQ: bop=B_SUB; break;
        case TK_STAREQ: bop=B_MUL; break;
        case TK_SLASHEQ: bop=B_DIV; break;
        case TK_PERCENTEQ: bop=B_MOD; break;
        case TK_ANDEQ: bop=B_AND; break;
        case TK_OREQ: bop=B_OR; break;
        case TK_XOREQ: bop=B_XOR; break;
        case TK_SHLEQ: bop=B_SHL; break;
        case TK_SHREQ: bop=B_SHR; break;
        default: err_at(s->loc,"IR: unsupported compound assign"); break;
      }

      IRIns bin; memset(&bin,0,sizeof(bin));
      bin.op=I_BIN; bin.a=dst; bin.b=cur; bin.c=rhs; bin.binop=bop;
      ir_emit(F,bin);

      ir_store(F, laddr, dst, lsz);
      return;
    }

    case ST_EXPR:
      (void)gen_expr(M, tenv, F, ctx, s->expr.e,NULL);
      return;
    case ST_UNSAFE:
      gen_stmt(M, tenv, F, ctx, s->uns.body);
      return;

    case ST_RET: {
      int r;
      if(s->ret.e){
        r = gen_expr(M, tenv, F, ctx, s->ret.e, NULL);
      } else {
        r = new_reg(F);
        IRIns ic; memset(&ic,0,sizeof(ic));
        ic.op=I_ICONST; ic.a=r; ic.imm=0;
        ir_emit(F,ic);
      }
      IRIns in; memset(&in,0,sizeof(in));
      in.op=I_RET; in.a=r;
      ir_emit(F,in);
      return;
    }

    case ST_IF: {
      int l_else=new_label(F);
      int l_end=new_label(F);

      int c = gen_expr(M, tenv, F, ctx, s->iff.cond, tenv->b.ty_i64);
      IRIns jz; memset(&jz,0,sizeof(jz));
      jz.op=I_JZ; jz.a=c; jz.b=l_else;
      ir_emit(F,jz);

      gen_stmt(M, tenv, F, ctx, s->iff.thenb);

      IRIns jmp; memset(&jmp,0,sizeof(jmp));
      jmp.op=I_JMP; jmp.a=l_end;
      ir_emit(F,jmp);

      IRIns lab; memset(&lab,0,sizeof(lab));
      lab.op=I_LABEL; lab.a=l_else;
      ir_emit(F,lab);

      if(s->iff.elseb) gen_stmt(M, tenv, F, ctx, s->iff.elseb);

      IRIns lab2; memset(&lab2,0,sizeof(lab2));
      lab2.op=I_LABEL; lab2.a=l_end;
      ir_emit(F,lab2);
      return;
    }

    case ST_WHILE: {
      int l_head=new_label(F);
      int l_end =new_label(F);

      loop_push(F, l_end, l_head);

      IRIns lab; memset(&lab,0,sizeof(lab));
      lab.op=I_LABEL; lab.a=l_head; ir_emit(F,lab);

      int c = gen_expr(M, tenv, F, ctx, s->wh.cond, tenv->b.ty_i64);
      IRIns jz; memset(&jz,0,sizeof(jz));
      jz.op=I_JZ; jz.a=c; jz.b=l_end; ir_emit(F,jz);

      gen_stmt(M, tenv, F, ctx, s->wh.body);

      IRIns jmp; memset(&jmp,0,sizeof(jmp));
      jmp.op=I_JMP; jmp.a=l_head; ir_emit(F,jmp);

      IRIns le; memset(&le,0,sizeof(le));
      le.op=I_LABEL; le.a=l_end; ir_emit(F,le);

      loop_pop(F);
      return;
    }

    case ST_FOR: {
      if(g_ir_vectorize){
        const char* iname = NULL;
        int ilen = 0;
        Expr* lim_expr = NULL;
        Expr *lhs = NULL, *ra = NULL, *rb = NULL;
        int bop = 0;
        long long stride = 1;
        Stmt* body = s->fr.body;
        if(body && body->k==ST_BLOCK && body->block.n==1){
          body = body->block.items[0];
        }
        if(get_for_index_init(s->fr.init, &iname, &ilen) &&
           step_is_inc1(s->fr.step, iname, ilen) &&
           get_for_limit_expr(s->fr.cond, iname, ilen, &lim_expr) &&
           match_vector_body(body, iname, ilen, &lhs, &ra, &rb, &bop, &stride)){
          if(lim_expr->k==EX_NAME && stmt_writes_name(body, lim_expr->name.name, lim_expr->name.len)){
            // limit mutated inside loop -> skip vectorize
          } else {
            Type* elty = lhs && lhs->ty ? lhs->ty : tenv->b.ty_i64;
            int is_float = is_f64(elty);
            int is_int = is_i64_ty(elty);
            int use_stride = (stride != 1);
            const char* vfn = NULL;
            int vfn_len = 0;
            if(is_float){
              if(bop==OP_ADD){
                if(use_stride){ vfn = "simd.v4f_add_strided"; vfn_len = 20; }
                else { vfn = "simd.v4f_add"; vfn_len = 12; }
              } else if(bop==OP_SUB){
                if(use_stride){ vfn = "simd.v4f_sub_strided"; vfn_len = 20; }
                else { vfn = "simd.v4f_sub"; vfn_len = 12; }
              } else if(bop==OP_MUL){
                if(use_stride){ vfn = "simd.v4f_mul_strided"; vfn_len = 20; }
                else { vfn = "simd.v4f_mul"; vfn_len = 12; }
              }
            } else if(is_int){
              if(bop==OP_ADD){
                if(use_stride){ vfn = "simd.v4i_add_strided"; vfn_len = 20; }
                else { vfn = "simd.v4i_add"; vfn_len = 12; }
              } else if(bop==OP_MUL){
                if(use_stride){ vfn = "simd.v4i_mul_strided"; vfn_len = 20; }
                else { vfn = "simd.v4i_mul"; vfn_len = 12; }
              }
            }
            if(vfn){
              int saved = F->vcount;
              int l_vec=new_label(F);
              int l_rem=new_label(F);
              int l_end=new_label(F);

              if(s->fr.init) gen_stmt(M, tenv, F, ctx, s->fr.init);
              loop_push(F, l_end, l_rem);

              int lim = gen_expr(M, tenv, F, ctx, lim_expr, tenv->b.ty_i64);
              int mask = new_reg(F);
              IRIns im; memset(&im,0,sizeof(im));
              im.op=I_ICONST; im.a=mask; im.imm=-4;
              ir_emit(F,im);
              int vec_lim = new_reg(F);
              IRIns ia; memset(&ia,0,sizeof(ia));
              ia.op=I_BIN; ia.a=vec_lim; ia.b=lim; ia.c=mask; ia.binop=B_AND;
              ir_emit(F,ia);

              IRIns lv; memset(&lv,0,sizeof(lv));
              lv.op=I_LABEL; lv.a=l_vec; ir_emit(F,lv);

              int idx = env_find(F, iname, ilen);
              int ivec = (idx>=0) ? ir_load(F, F->vaddr[idx], tenv->b.ty_i64) : -1;
              if(ivec < 0) ivec = new_reg(F);
              int cmpv = new_reg(F);
              IRIns cv; memset(&cv,0,sizeof(cv));
              cv.op=I_CMP; cv.a=cmpv; cv.b=ivec; cv.c=vec_lim; cv.cmpop=C_LT;
              ir_emit(F,cv);
              IRIns jv; memset(&jv,0,sizeof(jv));
              jv.op=I_JZ; jv.a=cmpv; jv.b=l_rem; ir_emit(F,jv);

              int outp = gen_lvalue(M, tenv, F, ctx, lhs, NULL);
              int ap = gen_lvalue(M, tenv, F, ctx, ra, NULL);
              int bp = gen_lvalue(M, tenv, F, ctx, rb, NULL);

              int argc = use_stride ? 4 : 3;
              int* callargs = (int*)xmalloc(sizeof(int)* (size_t)argc);
              callargs[0]=outp; callargs[1]=ap; callargs[2]=bp;
              if(use_stride){
                int sreg = new_reg(F);
                IRIns is; memset(&is,0,sizeof(is));
                is.op = I_ICONST; is.a = sreg; is.imm = stride;
                ir_emit(F,is);
                callargs[3]=sreg;
              }

              int dst=new_reg(F);
              IRIns call; memset(&call,0,sizeof(call));
              call.op=I_CALL; call.a=dst;
              call.name=vfn; call.nlen=vfn_len;
              call.args=callargs; call.argc=argc;
              ir_emit(F,call);

              if(idx >= 0){
                int addr = F->vaddr[idx];
                int cur = ir_load(F, addr, tenv->b.ty_i64);
                int c4 = new_reg(F);
                IRIns ic; memset(&ic,0,sizeof(ic));
                ic.op=I_ICONST; ic.a=c4; ic.imm=4;
                ir_emit(F,ic);
                int add = new_reg(F);
                IRIns ib; memset(&ib,0,sizeof(ib));
                ib.op=I_BIN; ib.a=add; ib.b=cur; ib.c=c4; ib.binop=B_ADD;
                ir_emit(F,ib);
                ir_store(F, addr, add, 8);
              }

              IRIns jmpv; memset(&jmpv,0,sizeof(jmpv));
              jmpv.op=I_JMP; jmpv.a=l_vec; ir_emit(F,jmpv);

              IRIns lr; memset(&lr,0,sizeof(lr));
              lr.op=I_LABEL; lr.a=l_rem; ir_emit(F,lr);

              int irem = (idx>=0) ? ir_load(F, F->vaddr[idx], tenv->b.ty_i64) : -1;
              if(irem < 0) irem = new_reg(F);
              int cmpr = new_reg(F);
              IRIns cr; memset(&cr,0,sizeof(cr));
              cr.op=I_CMP; cr.a=cmpr; cr.b=irem; cr.c=lim; cr.cmpop=C_LT;
              ir_emit(F,cr);
              IRIns jre; memset(&jre,0,sizeof(jre));
              jre.op=I_JZ; jre.a=cmpr; jre.b=l_end; ir_emit(F,jre);

              gen_stmt(M, tenv, F, ctx, body);

              if(idx >= 0){
                int addr = F->vaddr[idx];
                int cur = ir_load(F, addr, tenv->b.ty_i64);
                int c1 = new_reg(F);
                IRIns ic; memset(&ic,0,sizeof(ic));
                ic.op=I_ICONST; ic.a=c1; ic.imm=1;
                ir_emit(F,ic);
                int add = new_reg(F);
                IRIns ib; memset(&ib,0,sizeof(ib));
                ib.op=I_BIN; ib.a=add; ib.b=cur; ib.c=c1; ib.binop=B_ADD;
                ir_emit(F,ib);
                ir_store(F, addr, add, 8);
              }

              IRIns jmpr; memset(&jmpr,0,sizeof(jmpr));
              jmpr.op=I_JMP; jmpr.a=l_rem; ir_emit(F,jmpr);

              IRIns le; memset(&le,0,sizeof(le));
              le.op=I_LABEL; le.a=l_end; ir_emit(F,le);

              loop_pop(F);
              F->vcount = saved;
              return;
            }
          }
        }
      }
      if(g_ir_vectorize){
        const char* iname2 = NULL;
        int ilen2 = 0;
        Expr* lim_expr2 = NULL;
        if(get_for_index_init(s->fr.init, &iname2, &ilen2) &&
           step_is_inc1(s->fr.step, iname2, ilen2) &&
           get_for_limit_expr(s->fr.cond, iname2, ilen2, &lim_expr2) &&
           !stmt_has_break_or_continue(s->fr.body)){
          if(lim_expr2->k==EX_NAME && stmt_writes_name(s->fr.body, lim_expr2->name.name, lim_expr2->name.len)){
            // limit mutated inside loop -> skip unroll
          } else {
            int saved = F->vcount;
            int l_unroll = new_label(F);
            int l_rem = new_label(F);
            int l_end = new_label(F);

            if(s->fr.init) gen_stmt(M, tenv, F, ctx, s->fr.init);
            loop_push(F, l_end, l_rem);

            int lim = gen_expr(M, tenv, F, ctx, lim_expr2, tenv->b.ty_i64);
            int mask = new_reg(F);
            IRIns im; memset(&im,0,sizeof(im));
            im.op = I_ICONST; im.a = mask; im.imm = -4;
            ir_emit(F, im);
            int unroll_lim = new_reg(F);
            IRIns ia; memset(&ia,0,sizeof(ia));
            ia.op = I_BIN; ia.a = unroll_lim; ia.b = lim; ia.c = mask; ia.binop = B_AND;
            ir_emit(F, ia);

            IRIns lu; memset(&lu,0,sizeof(lu));
            lu.op = I_LABEL; lu.a = l_unroll; ir_emit(F, lu);

            int idx = env_find(F, iname2, ilen2);
            int ivec = (idx>=0) ? ir_load(F, F->vaddr[idx], tenv->b.ty_i64) : -1;
            if(ivec < 0) ivec = new_reg(F);
            int cmpu = new_reg(F);
            IRIns cu; memset(&cu,0,sizeof(cu));
            cu.op=I_CMP; cu.a=cmpu; cu.b=ivec; cu.c=unroll_lim; cu.cmpop=C_LT;
            ir_emit(F, cu);
            IRIns ju; memset(&ju,0,sizeof(ju));
            ju.op=I_JZ; ju.a=cmpu; ju.b=l_rem; ir_emit(F, ju);

            for(int u=0; u<4; u++){
              gen_stmt(M, tenv, F, ctx, s->fr.body);
              if(s->fr.step) gen_stmt(M, tenv, F, ctx, s->fr.step);
            }

            IRIns jmpu; memset(&jmpu,0,sizeof(jmpu));
            jmpu.op=I_JMP; jmpu.a=l_unroll; ir_emit(F,jmpu);

            IRIns lr; memset(&lr,0,sizeof(lr));
            lr.op=I_LABEL; lr.a=l_rem; ir_emit(F,lr);

            int irem = (idx>=0) ? ir_load(F, F->vaddr[idx], tenv->b.ty_i64) : -1;
            if(irem < 0) irem = new_reg(F);
            int cmpr = new_reg(F);
            IRIns cr; memset(&cr,0,sizeof(cr));
            cr.op=I_CMP; cr.a=cmpr; cr.b=irem; cr.c=lim; cr.cmpop=C_LT;
            ir_emit(F,cr);
            IRIns jre; memset(&jre,0,sizeof(jre));
            jre.op=I_JZ; jre.a=cmpr; jre.b=l_end; ir_emit(F,jre);

            gen_stmt(M, tenv, F, ctx, s->fr.body);
            if(s->fr.step) gen_stmt(M, tenv, F, ctx, s->fr.step);

            IRIns jmpr; memset(&jmpr,0,sizeof(jmpr));
            jmpr.op=I_JMP; jmpr.a=l_rem; ir_emit(F,jmpr);

            IRIns le; memset(&le,0,sizeof(le));
            le.op=I_LABEL; le.a=l_end; ir_emit(F,le);

            loop_pop(F);
            F->vcount = saved;
            return;
          }
        }
      }

      int saved = F->vcount;

      int l_head=new_label(F);
      int l_step=new_label(F);
      int l_end =new_label(F);

      if(s->fr.init) gen_stmt(M, tenv, F, ctx, s->fr.init);

      loop_push(F, l_end, l_step);

      IRIns lh; memset(&lh,0,sizeof(lh));
      lh.op=I_LABEL; lh.a=l_head; ir_emit(F,lh);

      if(s->fr.cond){
        int c = gen_expr(M, tenv, F, ctx, s->fr.cond, tenv->b.ty_i64);
        IRIns jz; memset(&jz,0,sizeof(jz));
        jz.op=I_JZ; jz.a=c; jz.b=l_end; ir_emit(F,jz);
      }

      gen_stmt(M, tenv, F, ctx, s->fr.body);

      IRIns ls; memset(&ls,0,sizeof(ls));
      ls.op=I_LABEL; ls.a=l_step; ir_emit(F,ls);

      if(s->fr.step) gen_stmt(M, tenv, F, ctx, s->fr.step);

      IRIns jmp; memset(&jmp,0,sizeof(jmp));
      jmp.op=I_JMP; jmp.a=l_head; ir_emit(F,jmp);

      IRIns le; memset(&le,0,sizeof(le));
      le.op=I_LABEL; le.a=l_end; ir_emit(F,le);

      loop_pop(F);
      F->vcount = saved;
      return;
    }

    case ST_SWITCH: {
      int l_end = new_label(F);
      int n = s->sw.case_n;
      int* labels = (int*)xmalloc(sizeof(int)*(size_t)n);
      int l_default = l_end;

      for(int i=0;i<n;i++){
        labels[i] = new_label(F);
        if(s->sw.cases[i].val==NULL) l_default = labels[i];
      }

      int cond = gen_expr(M, tenv, F, ctx, s->sw.cond, tenv->b.ty_i64);

      for(int i=0;i<n;i++){
        if(s->sw.cases[i].val==NULL) continue;
        int v = gen_expr(M, tenv, F, ctx, s->sw.cases[i].val, tenv->b.ty_i64);
        int cmp = new_reg(F);
        IRIns in; memset(&in,0,sizeof(in));
        in.op=I_CMP; in.a=cmp; in.b=cond; in.c=v; in.cmpop=C_EQ;
        ir_emit(F,in);

        int l_next = new_label(F);
        IRIns jz; memset(&jz,0,sizeof(jz));
        jz.op=I_JZ; jz.a=cmp; jz.b=l_next;
        ir_emit(F,jz);

        IRIns jmp; memset(&jmp,0,sizeof(jmp));
        jmp.op=I_JMP; jmp.a=labels[i];
        ir_emit(F,jmp);

        IRIns lab; memset(&lab,0,sizeof(lab));
        lab.op=I_LABEL; lab.a=l_next;
        ir_emit(F,lab);
      }

      IRIns jdef; memset(&jdef,0,sizeof(jdef));
      jdef.op=I_JMP; jdef.a=l_default;
      ir_emit(F,jdef);

      loop_push(F, l_end, -1);
      for(int i=0;i<n;i++){
        IRIns lab; memset(&lab,0,sizeof(lab));
        lab.op=I_LABEL; lab.a=labels[i];
        ir_emit(F,lab);

        int fall = 0;
        Stmt* body = s->sw.cases[i].body;
        if(body && body->k==ST_BLOCK && body->block.n>0){
          Stmt* last = body->block.items[body->block.n-1];
          if(last && last->k==ST_FALLTHROUGH) fall = 1;
        }

        if(body && body->k==ST_BLOCK && body->block.n>0 && fall){
          for(int j=0;j<body->block.n-1;j++){
            gen_stmt(M, tenv, F, ctx, body->block.items[j]);
          }
        } else {
          gen_stmt(M, tenv, F, ctx, body);
        }

        IRIns jmp; memset(&jmp,0,sizeof(jmp));
        jmp.op=I_JMP; jmp.a=(fall && i+1<n) ? labels[i+1] : l_end;
        ir_emit(F,jmp);
      }
      loop_pop(F);

      IRIns lend; memset(&lend,0,sizeof(lend));
      lend.op=I_LABEL; lend.a=l_end;
      ir_emit(F,lend);

      free(labels);
      return;
    }

    case ST_BREAK: {
      int l = loop_break(F);
      if(l<0) err_at(s->loc, "IR: break outside loop");
      IRIns j; memset(&j,0,sizeof(j));
      j.op=I_JMP; j.a=l;
      ir_emit(F,j);
      return;
    }

    case ST_CONTINUE: {
      int l = loop_continue(F);
      if(l<0) err_at(s->loc, "IR: continue outside loop");
      IRIns j; memset(&j,0,sizeof(j));
      j.op=I_JMP; j.a=l;
      ir_emit(F,j);
      return;
    }

    case ST_FALLTHROUGH:
      err_at(s->loc, "IR: fallthrough only valid in switch case");
      return;

    default:
      err_at(s->loc, "IR: unsupported statement (v4)");
      return;
  }
}

/* -------------------------
 * Build module
 * ------------------------- */
static void ir_add_func(IRModule* M, TypeEnv* tenv, Module* cur, Module* root, FnDecl* fn){
  if(fn->is_extern) return;
  if(M->fn_n==M->fn_cap){
    M->fn_cap = M->fn_cap? M->fn_cap*2 : 64;
    M->fns = (IRFunc*)xrealloc(M->fns, sizeof(IRFunc)*(size_t)M->fn_cap);
  }

  IRFunc* F = &M->fns[M->fn_n++];
  memset(F,0,sizeof(*F));
  const char* nm = fn->name;
  int nlen = fn->len;
  int owned = 0;
  if(cur && root && cur != root){
    char* full = mod_prefix_name(cur, nm, nlen, &nlen);
    if(full){
      nm = full;
      owned = 1;
    }
  }
  F->name = nm;
  F->len  = nlen;
  F->name_owned = owned;
  F->next_reg=0;
  F->max_reg=-1;
  F->param_count = fn->arity;
  F->param_regs = NULL;
  F->param_regs_n = 0;
  F->is_extern = fn->is_extern;
  F->is_kernel = fn->is_kernel;
  F->force_inline = fn_has_attr_name(fn, "inline", 6);
  if(fn->arity > 0){
    F->param_regs = (int*)xmalloc(sizeof(int)*(size_t)fn->arity);
  }

  // Params: create incoming regs r0,r2,.. and store them into allocas
  for(int i=0;i<fn->arity;i++){
    Type* pty = fn->ptypes[i] ? fn->ptypes[i] : tenv->b.ty_i64;

    int preg = new_reg(F); // incoming value reg (ABI will map these)
    if(F->param_regs) F->param_regs[F->param_regs_n++] = preg;
    int addr = ir_alloca(F, type_size(pty), type_align(pty));
    ir_store(F, addr, preg, type_size(pty));
    env_add(F, fn->pnames[i], fn->plens[i], addr, pty);
  }

  int entry=new_label(F);
  IRIns lab; memset(&lab,0,sizeof(lab));
  lab.op=I_LABEL; lab.a=entry;
  ir_emit(F,lab);

  IRGenCtx gctx;
  gctx.cur = cur;
  gctx.root = root ? root : cur;
  gen_stmt(M, tenv, F, &gctx, fn->body);

  if(F->n==0 || F->ins[F->n-1].op!=I_RET){
    int z=new_reg(F);
    IRIns ic; memset(&ic,0,sizeof(ic));
    ic.op=I_ICONST; ic.a=z; ic.imm=0;
    ir_emit(F,ic);

    IRIns rt; memset(&rt,0,sizeof(rt));
    rt.op=I_RET; rt.a=z;
    ir_emit(F,rt);
  }
}

IRModule* ir_build_from_module(Module* root, TypeEnv* tenv){
  IRModule* M=(IRModule*)xmalloc(sizeof(IRModule));
  memset(M,0,sizeof(*M));

  int* gidx = NULL;
  if(g_ir_repro && root->global_n > 1){
    gidx = (int*)xmalloc(sizeof(int)*(size_t)root->global_n);
    for(int i=0;i<root->global_n;i++) gidx[i]=i;
    for(int i=0;i<root->global_n;i++){
      for(int j=i+1;j<root->global_n;j++){
        struct GlobalVar* a = &root->globals[gidx[i]];
        struct GlobalVar* b = &root->globals[gidx[j]];
        int cmp = strncmp(a->name, b->name, (size_t)((a->len<b->len)?a->len:b->len));
        if(cmp==0) cmp = a->len - b->len;
        if(cmp > 0){
          int t = gidx[i]; gidx[i]=gidx[j]; gidx[j]=t;
        }
      }
    }
  }

  for(int i=0;i<root->global_n;i++){
    int idx = gidx ? gidx[i] : i;
    struct GlobalVar* g = &root->globals[idx];
    IRGlobal ig; memset(&ig,0,sizeof(ig));
    ig.name = g->name; ig.len = g->len; ig.name_owned = 0; ig.ty = g->ty;
    ig.size = type_size(g->ty);
    ig.align = type_align(g->ty);
    ig.is_extern = g->is_extern;
    ig.is_static = g->is_static;
    ig.has_init = (g->init!=NULL);
    if(g->init && g->init->k==EX_NUM){
      ig.init_int = g->init->num;
    } else if(g->init && g->init->k==EX_FLOAT){
      double x = g->init->f;
      memcpy(&ig.init_fbits, &x, sizeof(double));
    }
    M->globals=(IRGlobal*)xrealloc(M->globals,sizeof(IRGlobal)*(size_t)(M->global_n+1));
    M->globals[M->global_n++] = ig;
  }
  if(gidx) free(gidx);

  int* fidx = NULL;
  if(g_ir_repro && root->fn_n > 1){
    fidx = (int*)xmalloc(sizeof(int)*(size_t)root->fn_n);
    for(int i=0;i<root->fn_n;i++) fidx[i]=i;
    for(int i=0;i<root->fn_n;i++){
      for(int j=i+1;j<root->fn_n;j++){
        FnDecl* a = root->fns[fidx[i]];
        FnDecl* b = root->fns[fidx[j]];
        int cmp = strncmp(a->name, b->name, (size_t)((a->len<b->len)?a->len:b->len));
        if(cmp==0) cmp = a->len - b->len;
        if(cmp > 0){
          int t = fidx[i]; fidx[i]=fidx[j]; fidx[j]=t;
        }
      }
    }
  }
  for(int i=0;i<root->fn_n;i++){
    int idx = fidx ? fidx[i] : i;
    ir_add_func(M, tenv, root, root, root->fns[idx]);
  }
  if(fidx) free(fidx);
  return M;
}

IRModule* ir_build_from_graph(struct ModuleGraph* G, TypeEnv* tenv, Module* root){
  IRModule* M=(IRModule*)xmalloc(sizeof(IRModule));
  memset(M,0,sizeof(*M));

  Module** mods = G->mods;
  Module** sorted = NULL;
  int mod_n = G->mod_n;
  if(g_ir_repro && mod_n > 1){
    sorted = (Module**)xmalloc(sizeof(Module*)*(size_t)mod_n);
    for(int i=0;i<mod_n;i++) sorted[i] = G->mods[i];
    for(int i=0;i<mod_n;i++){
      for(int j=i+1;j<mod_n;j++){
        Module* a = sorted[i];
        Module* b = sorted[j];
        int cmp = 0;
        if(a==root && b!=root) cmp = -1;
        else if(b==root && a!=root) cmp = 1;
        else {
          const char* an = a->mname ? a->mname : a->path;
          const char* bn = b->mname ? b->mname : b->path;
          if(!an) an="";
          if(!bn) bn="";
          cmp = strcmp(an, bn);
        }
        if(cmp > 0){
          Module* tmp = sorted[i];
          sorted[i] = sorted[j];
          sorted[j] = tmp;
        }
      }
    }
    mods = sorted;
  }

  for(int mi=0; mi<mod_n; mi++){
    Module* mod = mods[mi];
    int* idxs = NULL;
    int gn = mod->global_n;
    if(g_ir_repro && gn > 1){
      idxs = (int*)xmalloc(sizeof(int)*(size_t)gn);
      for(int i=0;i<gn;i++) idxs[i]=i;
      for(int i=0;i<gn;i++){
        for(int j=i+1;j<gn;j++){
          struct GlobalVar* a = &mod->globals[idxs[i]];
          struct GlobalVar* b = &mod->globals[idxs[j]];
          int cmp = strncmp(a->name, b->name, (size_t)((a->len<b->len)?a->len:b->len));
          if(cmp==0) cmp = a->len - b->len;
          if(cmp > 0){
            int t = idxs[i]; idxs[i]=idxs[j]; idxs[j]=t;
          }
        }
      }
    }
    for(int gi=0; gi<gn; gi++){
      int idx = idxs ? idxs[gi] : gi;
      struct GlobalVar* g = &mod->globals[idx];
      IRGlobal ig; memset(&ig,0,sizeof(ig));
      const char* nm = g->name;
      int nlen = g->len;
      int owned = 0;
      if(root && mod != root && !g->is_extern){
        char* full = mod_prefix_name(mod, nm, nlen, &nlen);
        if(full){
          nm = full;
          owned = 1;
        }
      }
      ig.name = nm; ig.len = nlen; ig.name_owned = owned; ig.ty = g->ty;
      ig.size = type_size(g->ty);
      ig.align = type_align(g->ty);
      ig.is_extern = g->is_extern;
      ig.is_static = g->is_static;
      ig.has_init = (g->init!=NULL);
      if(g->init && g->init->k==EX_NUM){
        ig.init_int = g->init->num;
      } else if(g->init && g->init->k==EX_FLOAT){
        double x = g->init->f;
        memcpy(&ig.init_fbits, &x, sizeof(double));
      }
      M->globals=(IRGlobal*)xrealloc(M->globals,sizeof(IRGlobal)*(size_t)(M->global_n+1));
      M->globals[M->global_n++] = ig;
    }
    if(idxs) free(idxs);
  }

  // add every function from every loaded module
  for(int mi=0; mi<mod_n; mi++){
    Module* mod = mods[mi];
    int fn = mod->fn_n;
    int* idxs = NULL;
    if(g_ir_repro && fn > 1){
      idxs = (int*)xmalloc(sizeof(int)*(size_t)fn);
      for(int i=0;i<fn;i++) idxs[i]=i;
      for(int i=0;i<fn;i++){
        for(int j=i+1;j<fn;j++){
          FnDecl* a = mod->fns[idxs[i]];
          FnDecl* b = mod->fns[idxs[j]];
          int cmp = strncmp(a->name, b->name, (size_t)((a->len<b->len)?a->len:b->len));
          if(cmp==0) cmp = a->len - b->len;
          if(cmp > 0){
            int t = idxs[i]; idxs[i]=idxs[j]; idxs[j]=t;
          }
        }
      }
    }
    for(int i=0;i<fn;i++){
      int idx = idxs ? idxs[i] : i;
      ir_add_func(M, tenv, mod, root, mod->fns[idx]);
    }
    if(idxs) free(idxs);
  }

  if(sorted) free(sorted);
  return M;
}

/* -------------------------
 * Dump
 * ------------------------- */
static const char* bin_name(IRBin o){
  switch(o){
    case B_ADD: return "add";
    case B_SUB: return "sub";
    case B_MUL: return "mul";
    case B_DIV: return "div";
    case B_MOD: return "mod";
    case B_AND: return "and";
    case B_OR:  return "or";
    case B_XOR: return "xor";
    case B_SHL: return "shl";
    case B_SHR: return "shr";
    default: return "bin";
  }
}
static const char* cmp_name(IRCmp o){
  switch(o){
    case C_EQ: return "eq";
    case C_NEQ: return "neq";
    case C_LT: return "lt";
    case C_LTE: return "lte";
    case C_GT: return "gt";
    case C_GTE: return "gte";
    default: return "cmp";
  }
}

void ir_dump(IRModule* M){
  if(M->str_n){
    printf("rodata strings:\n");
    for(int i=0;i<M->str_n;i++){
      printf("  S%d = \"%s\"\n", i, M->strs[i].bytes);
    }
    printf("\n");
  }

  for(int fi=0; fi<M->fn_n; fi++){
    IRFunc* F=&M->fns[fi];
    int regs = (F->max_reg>=0 ? (F->max_reg+1) : 0);
    printf("func %.*s (regs=%d)\n", F->len, F->name, regs);

    for(int i=0;i<F->n;i++){
      IRIns* in=&F->ins[i];
      switch(in->op){
        case I_LABEL:  printf("L%d:\n", in->a); break;
        case I_ICONST: printf("  iconst  r%d = %lld\n", in->a, in->imm); break;
        case I_FCONST: {
          double x=0.0;
          memcpy(&x, &in->immf, sizeof(double));
          printf("  fconst  r%d = %g\n", in->a, x);
          break;
        }
        case I_ADDRSYM: printf("  addrsym r%d = %.*s\n", in->a, in->nlen, in->name); break;
        case I_SCONST: printf("  sconst  r%d = S%d\n", in->a, in->sid); break;
        case I_MOV:    printf("  mov     r%d = r%d\n", in->a, in->b); break;
        case I_BIN:    printf("  %-6s r%d = r%d, r%d\n", bin_name(in->binop), in->a, in->b, in->c); break;
        case I_CMP:    printf("  %-6s r%d = r%d, r%d\n", cmp_name(in->cmpop), in->a, in->b, in->c); break;
        case I_FBIN:   printf("  fbin    r%d = r%d, r%d\n", in->a, in->b, in->c); break;
        case I_FCMP:   printf("  fcmp    r%d = r%d, r%d\n", in->a, in->b, in->c); break;
        case I_I2F:    printf("  i2f     r%d = r%d\n", in->a, in->b); break;
        case I_F2I:    printf("  f2i     r%d = r%d\n", in->a, in->b); break;
        case I_CALL: {
          printf("  call    r%d = %.*s(", in->a, in->nlen, in->name);
          for(int k=0;k<in->argc;k++){ if(k) printf(", "); printf("r%d", in->args[k]); }
          printf(")\n");
          break;
        }
        case I_CALLPTR: {
          printf("  callptr r%d = r%d(", in->a, in->b);
          for(int k=0;k<in->argc;k++){ if(k) printf(", "); printf("r%d", in->args[k]); }
          printf(")\n");
          break;
        }
        case I_ALLOCA: printf("  alloca  r%d = (size=%d, align=%d)\n", in->a, in->size, in->align); break;
        case I_LOAD:   printf("  load    r%d = [r%d] (size=%d,%s)\n", in->a, in->b, in->size, in->is_unsigned ? "zext" : "sext"); break;
        case I_STORE:  printf("  store   [r%d] = r%d (size=%d)\n", in->a, in->b, in->size); break;
        case I_GEP:    printf("  gep     r%d = r%d + %lld\n", in->a, in->b, in->imm); break;
        case I_JMP:    printf("  jmp     L%d\n", in->a); break;
        case I_JZ:     printf("  jz      r%d -> L%d\n", in->a, in->b); break;
        case I_RET:    printf("  ret     r%d\n", in->a); break;
        default:       printf("  <unknown>\n"); break;
      }
    }
    printf("\n");
  }
}

/* -------------------------
 * Optimizer (your v1: block-local)
 * ------------------------- */
// (keep your optimizer exactly as you pasted — it already works with the public IR types)
static int is_bin_pure(IRBin o){ (void)o; return 1; }

static int eval_bin(IRBin o, long long a, long long b, long long* out){
  switch(o){
    case B_ADD: *out = a + b; return 1;
    case B_SUB: *out = a - b; return 1;
    case B_MUL: *out = a * b; return 1;
    case B_DIV: if(b==0) return 0; *out = a / b; return 1;
    case B_MOD: if(b==0) return 0; *out = a % b; return 1;
    case B_AND: *out = a & b; return 1;
    case B_OR:  *out = a | b; return 1;
    case B_XOR: *out = a ^ b; return 1;
    case B_SHL: *out = a << b; return 1;
    case B_SHR: *out = a >> b; return 1;
    default: return 0;
  }
}
static int eval_cmp(IRCmp o, long long a, long long b){
  switch(o){
    case C_EQ:  return a==b;
    case C_NEQ: return a!=b;
    case C_LT:  return a<b;
    case C_LTE: return a<=b;
    case C_GT:  return a>b;
    case C_GTE: return a>=b;
    default:    return 0;
  }
}
static void clear_consts(char* is_c, long long* cv, int n){
  (void)cv;
  for(int i=0;i<n;i++) is_c[i]=0;
}
static void peephole_remove_trivial(IRFunc* F){
  int w=0;
  for(int i=0;i<F->n;i++){
    IRIns in = F->ins[i];
    /* Remove self-copy: mov r, r */
    if(in.op==I_MOV && in.a==in.b) continue;
    /* Remove jump-to-next-label: jmp L; L: */
    if(in.op==I_JMP && (i+1)<F->n){
      IRIns nx = F->ins[i+1];
      if(nx.op==I_LABEL && nx.a==in.a) continue;
    }
    /* Algebraic simplifications on BIN with a known constant RHS */
    if(in.op==I_BIN){
      /* Propagate: we need to know if b or c is ICONST 0 or 1 */
      /* For the peephole pass we just do pattern matching on adjacent ICONST */
      if(i>=1){
        IRIns prev = F->ins[i-1];
        if(prev.op==I_ICONST && prev.a==in.c){
          long long k = prev.imm;
          /* x + 0 -> MOV x */
          if(in.binop==B_ADD && k==0){ in.op=I_MOV; in.b=in.b; F->ins[w++]=in; continue; }
          /* x - 0 -> MOV x */
          if(in.binop==B_SUB && k==0){ in.op=I_MOV; F->ins[w++]=in; continue; }
          /* x * 1 -> MOV x */
          if(in.binop==B_MUL && k==1){ in.op=I_MOV; F->ins[w++]=in; continue; }
          /* x * 0 -> ICONST 0 */
          if(in.binop==B_MUL && k==0){ in.op=I_ICONST; in.imm=0; F->ins[w++]=in; continue; }
          /* x / 1 -> MOV x */
          if(in.binop==B_DIV && k==1){ in.op=I_MOV; F->ins[w++]=in; continue; }
          /* x | 0 -> MOV x */
          if(in.binop==B_OR && k==0){ in.op=I_MOV; F->ins[w++]=in; continue; }
          /* x & -1 -> MOV x (all bits set) */
          if(in.binop==B_AND && k==-1){ in.op=I_MOV; F->ins[w++]=in; continue; }
          /* x ^ 0 -> MOV x */
          if(in.binop==B_XOR && k==0){ in.op=I_MOV; F->ins[w++]=in; continue; }
          /* x << 0 -> MOV x */
          if(in.binop==B_SHL && k==0){ in.op=I_MOV; F->ins[w++]=in; continue; }
          /* x >> 0 -> MOV x */
          if(in.binop==B_SHR && k==0){ in.op=I_MOV; F->ins[w++]=in; continue; }
        }
        /* Constant LHS patterns */
        if(prev.op==I_ICONST && prev.a==in.b){
          long long k = prev.imm;
          /* 0 + x -> MOV x (commutative) */
          if(in.binop==B_ADD && k==0){ in.op=I_MOV; in.b=in.c; F->ins[w++]=in; continue; }
          /* 0 * x -> ICONST 0 */
          if(in.binop==B_MUL && k==0){ in.op=I_ICONST; in.imm=0; F->ins[w++]=in; continue; }
          /* 1 * x -> MOV x */
          if(in.binop==B_MUL && k==1){ in.op=I_MOV; in.b=in.c; F->ins[w++]=in; continue; }
        }
      }
      /* x - x -> ICONST 0 */
      if(in.binop==B_SUB && in.b==in.c){
        in.op=I_ICONST; in.imm=0; F->ins[w++]=in; continue;
      }
      /* x & x -> MOV x */
      if(in.binop==B_AND && in.b==in.c){
        in.op=I_MOV; F->ins[w++]=in; continue;
      }
      /* x | x -> MOV x */
      if(in.binop==B_OR && in.b==in.c){
        in.op=I_MOV; F->ins[w++]=in; continue;
      }
      /* x ^ x -> ICONST 0 */
      if(in.binop==B_XOR && in.b==in.c){
        in.op=I_ICONST; in.imm=0; F->ins[w++]=in; continue;
      }
    }
    F->ins[w++] = in;
  }
  F->n = w;
}
static void const_fold_block_local(IRFunc* F){
  int regs = (F->max_reg>=0 ? (F->max_reg+1) : 0);
  if(regs<=0) return;

  char* is_c = (char*)xmalloc((size_t)regs);
  long long* cv = (long long*)xmalloc(sizeof(long long)*(size_t)regs);
  memset(is_c,0,(size_t)regs);

  for(int i=0;i<F->n;i++){
    IRIns* in = &F->ins[i];

    if(in->op==I_LABEL){ clear_consts(is_c, cv, regs); continue; }
    if(in->op==I_JMP || in->op==I_JZ){ clear_consts(is_c, cv, regs); continue; }

    switch(in->op){
      case I_ICONST:
        if(in->a>=0 && in->a<regs){ is_c[in->a]=1; cv[in->a]=in->imm; }
        break;
      case I_SCONST:
        if(in->a>=0 && in->a<regs){ is_c[in->a]=0; }
        break;
      case I_MOV:
        if(in->a>=0 && in->a<regs){
          if(in->b>=0 && in->b<regs && is_c[in->b]){
            in->op = I_ICONST;
            in->imm = cv[in->b];
            is_c[in->a]=1; cv[in->a]=in->imm;
          } else is_c[in->a]=0;
        }
        break;
      case I_BIN: {
        int dst=in->a, ra=in->b, rb=in->c;
        if(dst>=0 && dst<regs){
          if(rb>=0 && rb<regs && is_c[rb]){
            long long k=cv[rb];
            if(in->binop==B_ADD && k==0){
              in->op=I_MOV; in->b=ra; is_c[dst]=is_c[ra]; if(is_c[ra]) cv[dst]=cv[ra];
              break;
            }
            if(in->binop==B_MUL && k==1){
              in->op=I_MOV; in->b=ra; is_c[dst]=is_c[ra]; if(is_c[ra]) cv[dst]=cv[ra];
              break;
            }
            if(in->binop==B_MUL && k==0){
              in->op=I_ICONST; in->imm=0; is_c[dst]=1; cv[dst]=0;
              break;
            }
          }
          if(ra>=0 && ra<regs && rb>=0 && rb<regs && is_c[ra] && is_c[rb] && is_bin_pure(in->binop)){
            long long out=0;
            if(eval_bin(in->binop, cv[ra], cv[rb], &out)){
              in->op=I_ICONST; in->imm=out; is_c[dst]=1; cv[dst]=out;
            } else is_c[dst]=0;
          } else is_c[dst]=0;
        }
        break;
      }
      case I_CMP: {
        int dst=in->a, ra=in->b, rb=in->c;
        if(dst>=0 && dst<regs){
          if(ra>=0 && ra<regs && rb>=0 && rb<regs && is_c[ra] && is_c[rb]){
            int res = eval_cmp(in->cmpop, cv[ra], cv[rb]);
            in->op=I_ICONST; in->imm = res ? 1 : 0;
            is_c[dst]=1; cv[dst]=in->imm;
          } else is_c[dst]=0;
        }
        break;
      }
      case I_CALL: case I_LOAD: case I_GEP: case I_ALLOCA:
        if(in->a>=0 && in->a<regs) is_c[in->a]=0;
        break;
      default:
        break;
    }
  }

  free(is_c);
  free(cv);
}

static int ins_def_reg(IRIns* in){
  switch(in->op){
    case I_ICONST:
    case I_FCONST:
    case I_ADDRSYM:
    case I_SCONST:
    case I_MOV:
    case I_BIN:
    case I_CMP:
    case I_FBIN:
    case I_FCMP:
    case I_I2F:
    case I_F2I:
    case I_CALL:
    case I_CALLPTR:
    case I_ALLOCA:
    case I_LOAD:
    case I_GEP:
      return in->a;
    default:
      return -1;
  }
}

static int strength_reduce_pow2(IRFunc* F){
  int regs = (F->max_reg>=0 ? (F->max_reg+1) : 0);
  if(regs<=0) return 0;

  long long* cv = (long long*)xmalloc(sizeof(long long)*(size_t)regs);
  unsigned char* is_c = (unsigned char*)xmalloc((size_t)regs);
  memset(is_c, 0, (size_t)regs);

  int cap = F->n * 2 + 8;
  IRIns* out = (IRIns*)xmalloc(sizeof(IRIns)*(size_t)cap);
  int n = 0;
  int changed = 0;

  for(int i=0;i<F->n;i++){
    IRIns in = F->ins[i];
    if(in.op==I_LABEL){
      memset(is_c, 0, (size_t)regs);
      out[n++] = in;
      continue;
    }
    if(in.op==I_ICONST){
      if(in.a>=0 && in.a<regs){ is_c[in.a]=1; cv[in.a]=in.imm; }
      out[n++] = in;
      continue;
    }
    if(in.op==I_BIN && in.binop==B_MUL){
      int kb = (in.b>=0 && in.b<regs) ? is_c[in.b] : 0;
      int kc = (in.c>=0 && in.c<regs) ? is_c[in.c] : 0;
      if((kb ^ kc) != 0){
        long long k = kb ? cv[in.b] : cv[in.c];
        if(k > 0 && (k & (k - 1)) == 0){
          int sh = 0;
          while((1LL << sh) < k) sh++;
          int shreg = new_reg(F);
          IRIns ic; memset(&ic,0,sizeof(ic));
          ic.op = I_ICONST; ic.a = shreg; ic.imm = sh;
          out[n++] = ic;
          if(kb){
            int tmp = in.b; in.b = in.c; in.c = tmp;
          }
          in.binop = B_SHL;
          in.c = shreg;
          out[n++] = in;
          if(in.a>=0 && in.a<regs) is_c[in.a]=0;
          changed = 1;
          continue;
        }
      }
    }
    out[n++] = in;
    if(in.a>=0 && in.a<regs){
      switch(in.op){
        case I_BIN: case I_CMP: case I_FBIN: case I_FCMP:
        case I_I2F: case I_F2I: case I_CALL: case I_CALLPTR:
        case I_ALLOCA: case I_LOAD: case I_GEP: case I_MOV:
        case I_ADDRSYM: case I_SCONST:
          is_c[in.a]=0; break;
        default: break;
      }
    }
  }

  if(changed){
    free(F->ins);
    F->ins = out;
    F->n = n;
    F->cap = n;
  } else {
    free(out);
  }
  free(cv);
  free(is_c);
  return changed;
}

static int ins_has_side_effect(IRIns* in){
  switch(in->op){
    case I_CALL:
    case I_CALLPTR:
    case I_STORE:
    case I_RET:
    case I_LABEL:
    case I_JMP:
    case I_JZ:
      return 1;
    default:
      return 0;
  }
}

static void ins_mark_uses(IRIns* in, char* live, int regs){
  switch(in->op){
    case I_MOV:
      if(in->b>=0 && in->b<regs) live[in->b]=1;
      break;
    case I_BIN:
    case I_CMP:
      if(in->b>=0 && in->b<regs) live[in->b]=1;
      if(in->c>=0 && in->c<regs) live[in->c]=1;
      break;
    case I_FBIN:
    case I_FCMP:
      if(in->b>=0 && in->b<regs) live[in->b]=1;
      if(in->c>=0 && in->c<regs) live[in->c]=1;
      break;
    case I_I2F:
    case I_F2I:
    case I_LOAD:
      if(in->b>=0 && in->b<regs) live[in->b]=1;
      break;
    case I_STORE:
      if(in->a>=0 && in->a<regs) live[in->a]=1;
      if(in->b>=0 && in->b<regs) live[in->b]=1;
      break;
    case I_GEP:
      if(in->b>=0 && in->b<regs) live[in->b]=1;
      break;
    case I_CALL:
    case I_CALLPTR:
      if(in->op==I_CALLPTR && in->b>=0 && in->b<regs) live[in->b]=1;
      for(int i=0;i<in->argc;i++){
        int r = in->args[i];
        if(r>=0 && r<regs) live[r]=1;
      }
      break;
    case I_JZ:
      if(in->a>=0 && in->a<regs) live[in->a]=1;
      break;
    case I_RET:
      if(in->a>=0 && in->a<regs) live[in->a]=1;
      break;
    default:
      break;
  }
}

static int ins_uses_reg(const IRIns* in, int r){
  switch(in->op){
    case I_MOV: return in->b==r;
    case I_BIN:
    case I_CMP: return in->b==r || in->c==r;
    case I_FBIN:
    case I_FCMP: return in->b==r || in->c==r;
    case I_I2F:
    case I_F2I:
    case I_LOAD: return in->b==r;
    case I_STORE: return in->a==r || in->b==r;
    case I_GEP: return in->b==r;
    case I_CALL:
      for(int i=0;i<in->argc;i++) if(in->args[i]==r) return 1;
      return 0;
    case I_CALLPTR:
      if(in->b==r) return 1;
      for(int i=0;i<in->argc;i++) if(in->args[i]==r) return 1;
      return 0;
    case I_JZ: return in->a==r;
    case I_RET: return in->a==r;
    default: return 0;
  }
}

static int ins_is_pure(const IRIns* in){
  switch(in->op){
    case I_ICONST:
    case I_FCONST:
    case I_ADDRSYM:
    case I_SCONST:
    case I_MOV:
    case I_BIN:
    case I_CMP:
    case I_FBIN:
    case I_FCMP:
    case I_I2F:
    case I_F2I:
    case I_GEP:
      return 1;
    default:
      return 0;
  }
}

/* -------------------------
 * CFG + dominators
 * ------------------------- */
typedef struct IRBlock {
  int id;
  int start;
  int end;   // inclusive
  int* succ;
  int succ_n;
  int* pred;
  int pred_n;
  int* child;
  int child_n;
  int idom;
  unsigned char* dom; // bitset (bytes)
} IRBlock;

typedef struct IRCfg {
  IRBlock* blocks;
  int n;
  int entry;
} IRCfg;

static void cfg_add_succ(IRCfg* cfg, int from, int to){
  if(!cfg || from<0 || to<0) return;
  IRBlock* b = &cfg->blocks[from];
  b->succ = (int*)xrealloc(b->succ, sizeof(int)*(size_t)(b->succ_n+1));
  b->succ[b->succ_n++] = to;
}

static void cfg_add_pred(IRCfg* cfg, int to, int from){
  if(!cfg || from<0 || to<0) return;
  IRBlock* b = &cfg->blocks[to];
  b->pred = (int*)xrealloc(b->pred, sizeof(int)*(size_t)(b->pred_n+1));
  b->pred[b->pred_n++] = from;
}

static IRCfg* cfg_build(IRFunc* F){
  if(!F || F->n<=0) return NULL;

  int max_label = -1;
  for(int i=0;i<F->n;i++){
    IRIns* in = &F->ins[i];
    if(in->op==I_LABEL && in->a > max_label) max_label = in->a;
    if(in->op==I_JMP && in->a > max_label) max_label = in->a;
    if(in->op==I_JZ && in->b > max_label) max_label = in->b;
  }

  int* label_idx = NULL;
  if(max_label>=0){
    label_idx = (int*)xmalloc(sizeof(int)*(size_t)(max_label+1));
    for(int i=0;i<=max_label;i++) label_idx[i] = -1;
    for(int i=0;i<F->n;i++){
      if(F->ins[i].op==I_LABEL) label_idx[F->ins[i].a] = i;
    }
  }

  unsigned char* leader = (unsigned char*)xmalloc((size_t)F->n);
  memset(leader, 0, (size_t)F->n);
  leader[0] = 1;
  for(int i=0;i<F->n;i++){
    IRIns* in = &F->ins[i];
    if(in->op==I_LABEL) leader[i] = 1;
    if(in->op==I_JMP || in->op==I_JZ || in->op==I_RET){
      if(i+1 < F->n) leader[i+1] = 1;
    }
    if((in->op==I_JMP || in->op==I_JZ) && label_idx){
      int t = (in->op==I_JMP) ? in->a : in->b;
      if(t>=0 && t<=max_label && label_idx[t]>=0) leader[label_idx[t]] = 1;
    }
  }

  IRCfg* cfg = (IRCfg*)xmalloc(sizeof(IRCfg));
  memset(cfg, 0, sizeof(*cfg));

  // build blocks
  int i = 0;
  while(i < F->n){
    if(!leader[i]){ i++; continue; }
    int start = i;
    i++;
    while(i < F->n && !leader[i]) i++;
    int end = i - 1;
    cfg->blocks = (IRBlock*)xrealloc(cfg->blocks, sizeof(IRBlock)*(size_t)(cfg->n+1));
    IRBlock* b = &cfg->blocks[cfg->n];
    memset(b, 0, sizeof(*b));
    b->id = cfg->n;
    b->start = start;
    b->end = end;
    b->idom = -1;
    cfg->n++;
  }
  cfg->entry = 0;

  // map label -> block
  int* label_block = NULL;
  if(max_label>=0){
    label_block = (int*)xmalloc(sizeof(int)*(size_t)(max_label+1));
    for(int k=0;k<=max_label;k++) label_block[k] = -1;
    for(int bi=0; bi<cfg->n; bi++){
      IRBlock* b = &cfg->blocks[bi];
      if(b->start<=b->end && F->ins[b->start].op==I_LABEL){
        int lid = F->ins[b->start].a;
        if(lid>=0 && lid<=max_label) label_block[lid] = bi;
      }
    }
  }

  // add successors
  for(int bi=0; bi<cfg->n; bi++){
    IRBlock* b = &cfg->blocks[bi];
    int last = b->end;
    while(last >= b->start && F->ins[last].op==I_LABEL) last--;
    if(last < b->start){
      if(bi + 1 < cfg->n) cfg_add_succ(cfg, bi, bi+1);
      continue;
    }
    IRIns* in = &F->ins[last];
    if(in->op==I_JMP){
      int tgt = in->a;
      if(label_block && tgt>=0 && tgt<=max_label && label_block[tgt]>=0){
        cfg_add_succ(cfg, bi, label_block[tgt]);
      }
    } else if(in->op==I_JZ){
      int tgt = in->b;
      if(label_block && tgt>=0 && tgt<=max_label && label_block[tgt]>=0){
        cfg_add_succ(cfg, bi, label_block[tgt]);
      }
      if(bi + 1 < cfg->n) cfg_add_succ(cfg, bi, bi+1);
    } else if(in->op==I_RET){
      // no succ
    } else {
      if(bi + 1 < cfg->n) cfg_add_succ(cfg, bi, bi+1);
    }
  }

  // add preds
  for(int bi=0; bi<cfg->n; bi++){
    IRBlock* b = &cfg->blocks[bi];
    for(int s=0;s<b->succ_n;s++){
      cfg_add_pred(cfg, b->succ[s], bi);
    }
  }

  if(label_idx) free(label_idx);
  if(label_block) free(label_block);
  free(leader);

  return cfg;
}

static void cfg_compute_dominators(IRCfg* cfg){
  if(!cfg || cfg->n<=0) return;
  int n = cfg->n;

  for(int i=0;i<n;i++){
    cfg->blocks[i].dom = (unsigned char*)xmalloc((size_t)n);
    if(i==cfg->entry){
      memset(cfg->blocks[i].dom, 0, (size_t)n);
      cfg->blocks[i].dom[i] = 1;
    } else {
      memset(cfg->blocks[i].dom, 1, (size_t)n);
    }
    cfg->blocks[i].idom = -1;
    cfg->blocks[i].child = NULL;
    cfg->blocks[i].child_n = 0;
  }

  int changed = 1;
  while(changed){
    changed = 0;
    for(int b=0;b<n;b++){
      if(b==cfg->entry) continue;
      IRBlock* blk = &cfg->blocks[b];
      if(blk->pred_n==0) continue;
      unsigned char* newdom = (unsigned char*)xmalloc((size_t)n);
      memset(newdom, 1, (size_t)n);
      for(int pi=0; pi<blk->pred_n; pi++){
        int p = blk->pred[pi];
        unsigned char* pd = cfg->blocks[p].dom;
        for(int k=0;k<n;k++) newdom[k] = (unsigned char)(newdom[k] & pd[k]);
      }
      newdom[b] = 1;
      if(memcmp(newdom, blk->dom, (size_t)n)!=0){
        memcpy(blk->dom, newdom, (size_t)n);
        changed = 1;
      }
      free(newdom);
    }
  }

  // compute idom
  for(int b=0;b<n;b++){
    if(b==cfg->entry) continue;
    IRBlock* blk = &cfg->blocks[b];
    int idom = -1;
    for(int d=0; d<n; d++){
      if(d==b) continue;
      if(!blk->dom[d]) continue;
      int dominated_by_all = 1;
      for(int c=0;c<n;c++){
        if(c==b || c==d) continue;
        if(!blk->dom[c]) continue;
        if(!cfg->blocks[c].dom[d]){
          dominated_by_all = 0;
          break;
        }
      }
      if(dominated_by_all){ idom = d; break; }
    }
    blk->idom = idom;
  }

  // build dom tree children
  for(int b=0;b<n;b++){
    int idom = cfg->blocks[b].idom;
    if(idom>=0){
      IRBlock* p = &cfg->blocks[idom];
      p->child = (int*)xrealloc(p->child, sizeof(int)*(size_t)(p->child_n+1));
      p->child[p->child_n++] = b;
    }
  }
}

static void cfg_free(IRCfg* cfg){
  if(!cfg) return;
  for(int i=0;i<cfg->n;i++){
    free(cfg->blocks[i].succ);
    free(cfg->blocks[i].pred);
    free(cfg->blocks[i].child);
    free(cfg->blocks[i].dom);
  }
  free(cfg->blocks);
  free(cfg);
}

static int dce_unreachable(IRFunc* F, IRCfg* cfg){
  if(!F || !cfg || cfg->n<=0) return 0;
  int n = cfg->n;
  unsigned char* reach = (unsigned char*)xmalloc((size_t)n);
  memset(reach, 0, (size_t)n);
  int* stack = (int*)xmalloc(sizeof(int)*(size_t)n);
  int sp = 0;
  stack[sp++] = cfg->entry;
  reach[cfg->entry] = 1;
  while(sp>0){
    int b = stack[--sp];
    IRBlock* blk = &cfg->blocks[b];
    for(int i=0;i<blk->succ_n;i++){
      int s = blk->succ[i];
      if(!reach[s]){
        reach[s] = 1;
        stack[sp++] = s;
      }
    }
  }

  int changed = 0;
  IRIns* out = (IRIns*)xmalloc(sizeof(IRIns)*(size_t)F->n);
  int w = 0;
  for(int b=0;b<n;b++){
    IRBlock* blk = &cfg->blocks[b];
    if(!reach[b]){
      changed = 1;
      for(int i=blk->start;i<=blk->end;i++){
        IRIns* in = &F->ins[i];
        if(in->args) free(in->args);
      }
      continue;
    }
    for(int i=blk->start;i<=blk->end;i++){
      out[w++] = F->ins[i];
    }
  }
  if(changed){
    free(F->ins);
    F->ins = out;
    F->n = w;
    F->cap = w;
  } else {
    free(out);
  }

  free(stack);
  free(reach);
  return changed;
}

static int licm_simple(IRFunc* F){
  if(!F || F->n<=0) return 0;
  int regs = (F->max_reg>=0 ? (F->max_reg+1) : 0);
  int changed = 0;

  for(int pass=0; pass<4; pass++){
    int max_label = -1;
    for(int i=0;i<F->n;i++){
      IRIns* in = &F->ins[i];
      if(in->op==I_LABEL && in->a > max_label) max_label = in->a;
      if(in->op==I_JMP && in->a > max_label) max_label = in->a;
      if(in->op==I_JZ && in->b > max_label) max_label = in->b;
    }
    if(max_label < 0) break;

    int* label_idx = (int*)xmalloc(sizeof(int)*(size_t)(max_label+1));
    for(int i=0;i<=max_label;i++) label_idx[i] = -1;
    for(int i=0;i<F->n;i++){
      if(F->ins[i].op==I_LABEL) label_idx[F->ins[i].a] = i;
    }

    int moved = 0;
    for(int i=0;i<F->n;i++){
      IRIns* br = &F->ins[i];
      int target = -1;
      if(br->op==I_JMP) target = br->a;
      else if(br->op==I_JZ) target = br->b;
      else continue;
      if(target < 0 || target > max_label) continue;
      int t = label_idx[target];
      if(t < 0 || t >= i) continue;

      char* def_in_loop = NULL;
      if(regs > 0){
        def_in_loop = (char*)xmalloc((size_t)regs);
        memset(def_in_loop, 0, (size_t)regs);
      }
      for(int j=t; j<i; j++){
        int d = ins_def_reg(&F->ins[j]);
        if(d>=0 && d<regs) def_in_loop[d] = 1;
      }

      char* inv = (char*)xmalloc((size_t)F->n);
      memset(inv, 0, (size_t)F->n);
      int inv_n = 0;
      for(int j=t; j<i; j++){
        IRIns* in = &F->ins[j];
        int d = ins_def_reg(in);
        if(d<0) continue;
        if(!ins_is_pure(in)) continue;
        int ok = 1;
        for(int r=0; r<regs; r++){
          if(def_in_loop && def_in_loop[r] && ins_uses_reg(in, r)){
            ok = 0;
            break;
          }
        }
        if(ok){
          inv[j] = 1;
          inv_n++;
        }
      }

      if(inv_n > 0){
        IRIns* tmp = (IRIns*)xmalloc(sizeof(IRIns)*(size_t)F->n);
        int w = 0;
        for(int j=0;j<t;j++) tmp[w++] = F->ins[j];
        for(int j=t;j<i;j++) if(inv[j]) tmp[w++] = F->ins[j];
        for(int j=t;j<F->n;j++) if(!inv[j]) tmp[w++] = F->ins[j];
        for(int j=0;j<w;j++) F->ins[j] = tmp[j];
        F->n = w;
        free(tmp);
        moved = 1;
        changed = 1;
      }

      if(def_in_loop) free(def_in_loop);
      free(inv);
      if(moved) break;
    }

    free(label_idx);
    if(!moved) break;
  }

  return changed;
}

/* -------------------------
 * GVN (dominance-based)
 * ------------------------- */
typedef struct {
  IROp op;
  IRBin binop;
  IRCmp cmpop;
  IRFlt fop;
  int a;
  int b;
  long long imm;
  long long immf;
  int sid;
  const char* name;
  int nlen;
} GVNKey;

typedef struct {
  GVNKey key;
  int reg;
} GVNEntry;

typedef struct {
  GVNEntry* items;
  int n, cap;
} GVNTable;

static int gvn_is_commutative_bin(IRBin op){
  return (op==B_ADD || op==B_MUL || op==B_AND || op==B_OR || op==B_XOR);
}

static int gvn_is_commutative_f(IRFlt op){
  return (op==F_ADD || op==F_MUL);
}

static void gvn_table_init(GVNTable* t){
  t->items = NULL;
  t->n = 0;
  t->cap = 0;
}

static void gvn_table_free(GVNTable* t){
  if(t->items) free(t->items);
  t->items = NULL;
  t->n = 0;
  t->cap = 0;
}

static void gvn_table_clone(GVNTable* dst, const GVNTable* src){
  gvn_table_init(dst);
  if(!src || src->n<=0) return;
  dst->items = (GVNEntry*)xmalloc(sizeof(GVNEntry)*(size_t)src->n);
  memcpy(dst->items, src->items, sizeof(GVNEntry)*(size_t)src->n);
  dst->n = src->n;
  dst->cap = src->n;
}

static int gvn_key_eq(const GVNKey* a, const GVNKey* b){
  if(a->op!=b->op) return 0;
  if(a->binop!=b->binop) return 0;
  if(a->cmpop!=b->cmpop) return 0;
  if(a->fop!=b->fop) return 0;
  if(a->a!=b->a || a->b!=b->b) return 0;
  if(a->imm!=b->imm || a->immf!=b->immf) return 0;
  if(a->sid!=b->sid) return 0;
  if(a->nlen!=b->nlen) return 0;
  if(a->nlen>0 && a->name && b->name){
    if(memcmp(a->name, b->name, (size_t)a->nlen)!=0) return 0;
  }
  return 1;
}

static int gvn_table_find(const GVNTable* t, const GVNKey* key){
  for(int i=0;i<t->n;i++){
    if(gvn_key_eq(&t->items[i].key, key)) return t->items[i].reg;
  }
  return -1;
}

static void gvn_table_add(GVNTable* t, const GVNKey* key, int reg){
  if(t->n==t->cap){
    t->cap = t->cap ? t->cap*2 : 32;
    t->items = (GVNEntry*)xrealloc(t->items, sizeof(GVNEntry)*(size_t)t->cap);
  }
  t->items[t->n].key = *key;
  t->items[t->n].reg = reg;
  t->n++;
}

static int alias_reg(int* alias, int r){
  if(r<0) return r;
  int a = alias[r];
  while(a>=0 && a != alias[a]) a = alias[a];
  alias[r] = a;
  return a;
}

static int rewrite_reg(int* alias, int* r){
  if(!r || *r<0) return 0;
  int old = *r;
  int nw = alias_reg(alias, old);
  *r = nw;
  return nw != old;
}

static int gvn_rewrite_operands(IRIns* in, int* alias){
  int changed = 0;
  switch(in->op){
    case I_MOV:
      changed |= rewrite_reg(alias, &in->b);
      break;
    case I_BIN:
    case I_CMP:
      changed |= rewrite_reg(alias, &in->b);
      changed |= rewrite_reg(alias, &in->c);
      break;
    case I_FBIN:
    case I_FCMP:
      changed |= rewrite_reg(alias, &in->b);
      changed |= rewrite_reg(alias, &in->c);
      break;
    case I_I2F:
    case I_F2I:
    case I_LOAD:
      changed |= rewrite_reg(alias, &in->b);
      break;
    case I_STORE:
      changed |= rewrite_reg(alias, &in->a);
      changed |= rewrite_reg(alias, &in->b);
      break;
    case I_GEP:
      changed |= rewrite_reg(alias, &in->b);
      break;
    case I_CALL:
      for(int i=0;i<in->argc;i++){
        changed |= rewrite_reg(alias, &in->args[i]);
      }
      break;
    case I_CALLPTR:
      changed |= rewrite_reg(alias, &in->b);
      for(int i=0;i<in->argc;i++){
        changed |= rewrite_reg(alias, &in->args[i]);
      }
      break;
    case I_JZ:
      changed |= rewrite_reg(alias, &in->a);
      break;
    case I_RET:
      changed |= rewrite_reg(alias, &in->a);
      break;
    default:
      break;
  }
  return changed;
}

static void gvn_key_from_ins(const IRIns* in, GVNKey* key){
  memset(key, 0, sizeof(*key));
  key->op = in->op;
  key->binop = in->binop;
  key->cmpop = in->cmpop;
  key->fop = in->fop;
  switch(in->op){
    case I_ICONST:
      key->imm = in->imm;
      break;
    case I_FCONST:
      key->immf = in->immf;
      break;
    case I_ADDRSYM:
      key->name = in->name;
      key->nlen = in->nlen;
      break;
    case I_SCONST:
      key->sid = in->sid;
      break;
    case I_MOV:
      key->a = in->b;
      break;
    case I_BIN:
      key->a = in->b;
      key->b = in->c;
      if(gvn_is_commutative_bin(in->binop) && key->a > key->b){
        int tmp = key->a; key->a = key->b; key->b = tmp;
      }
      break;
    case I_CMP:
      key->a = in->b;
      key->b = in->c;
      break;
    case I_FBIN:
      key->a = in->b;
      key->b = in->c;
      if(gvn_is_commutative_f(in->fop) && key->a > key->b){
        int tmp = key->a; key->a = key->b; key->b = tmp;
      }
      break;
    case I_FCMP:
      key->a = in->b;
      key->b = in->c;
      break;
    case I_I2F:
    case I_F2I:
    case I_LOAD:
      key->a = in->b;
      break;
    case I_GEP:
      key->a = in->b;
      key->imm = in->imm;
      break;
    default:
      break;
  }
}

static void gvn_block(IRFunc* F, IRCfg* cfg, int bid, int regs, int* alias_in, const GVNTable* table_in, int* changed){
  IRBlock* blk = &cfg->blocks[bid];
  int* alias = (int*)xmalloc(sizeof(int)*(size_t)regs);
  if(alias_in){
    memcpy(alias, alias_in, sizeof(int)*(size_t)regs);
  } else {
    for(int i=0;i<regs;i++) alias[i]=i;
  }

  GVNTable table;
  gvn_table_clone(&table, table_in);

  for(int i=blk->start; i<=blk->end; i++){
    IRIns* in = &F->ins[i];
    if(gvn_rewrite_operands(in, alias)) *changed = 1;
    int def = ins_def_reg(in);
    if(def>=0 && def<regs) alias[def] = def;
    if(ins_is_pure(in)){
      GVNKey key;
      gvn_key_from_ins(in, &key);
      int existing = gvn_table_find(&table, &key);
      if(existing >= 0 && def >= 0){
        IRIns mv; memset(&mv,0,sizeof(mv));
        mv.op = I_MOV; mv.a = def; mv.b = existing;
        F->ins[i] = mv;
        alias[def] = existing;
        *changed = 1;
        continue;
      }
      if(def>=0) gvn_table_add(&table, &key, def);
    }
  }

  for(int i=0;i<blk->child_n;i++){
    gvn_block(F, cfg, blk->child[i], regs, alias, &table, changed);
  }

  gvn_table_free(&table);
  free(alias);
}

static int gvn_func(IRFunc* F, IRCfg* cfg){
  if(!F || !cfg) return 0;
  int regs = (F->max_reg>=0 ? (F->max_reg+1) : 0);
  if(regs<=0) return 0;
  if(!cfg->blocks || cfg->n<=0) return 0;
  if(!cfg->blocks[0].dom) cfg_compute_dominators(cfg);
  int changed = 0;
  int* alias = (int*)xmalloc(sizeof(int)*(size_t)regs);
  for(int i=0;i<regs;i++) alias[i]=i;
  GVNTable base; gvn_table_init(&base);
  gvn_block(F, cfg, cfg->entry, regs, alias, &base, &changed);
  gvn_table_free(&base);
  free(alias);
  return changed;
}

/* -------------------------
 * LICM (CFG-based, simple)
 * ------------------------- */
static int reg_uses_loopdef(const IRIns* in, const unsigned char* loop_defs, const unsigned char* inv_regs, int regs){
  #define BAD_REG(_r) ((_r)>=0 && (_r)<regs && loop_defs[_r] && !inv_regs[_r])
  switch(in->op){
    case I_MOV: return BAD_REG(in->b);
    case I_BIN:
    case I_CMP: return BAD_REG(in->b) || BAD_REG(in->c);
    case I_FBIN:
    case I_FCMP: return BAD_REG(in->b) || BAD_REG(in->c);
    case I_I2F:
    case I_F2I:
    case I_LOAD: return BAD_REG(in->b);
    case I_GEP: return BAD_REG(in->b);
    case I_CALL:
    case I_CALLPTR:
    case I_STORE:
    case I_RET:
    case I_LABEL:
    case I_JMP:
    case I_JZ:
    case I_ALLOCA:
      return 1;
    default:
      return 0;
  }
  #undef BAD_REG
}

static int licm_cfg(IRFunc* F, IRCfg* cfg){
  if(!F || !cfg) return 0;
  if(!cfg->blocks || cfg->n<=0) return 0;
  if(!cfg->blocks[0].dom) cfg_compute_dominators(cfg);

  int regs = (F->max_reg>=0 ? (F->max_reg+1) : 0);
  int* reg_def = NULL;
  if(regs>0){
    reg_def = (int*)xmalloc(sizeof(int)*(size_t)regs);
    for(int i=0;i<regs;i++) reg_def[i] = -1;
  }

  for(int b=0;b<cfg->n;b++){
    IRBlock* blk = &cfg->blocks[b];
    for(int i=blk->start;i<=blk->end;i++){
      int d = ins_def_reg(&F->ins[i]);
      if(d>=0 && d<regs) reg_def[d] = b;
    }
  }

  int changed = 0;
  for(int b=0;b<cfg->n;b++){
    IRBlock* blk = &cfg->blocks[b];
    for(int si=0; si<blk->succ_n; si++){
      int h = blk->succ[si];
      if(h<0 || h>=cfg->n) continue;
      if(!cfg->blocks[b].dom || !cfg->blocks[b].dom[h]) continue; // h dominates b

      // build loop set
      unsigned char* in_loop = (unsigned char*)xmalloc((size_t)cfg->n);
      memset(in_loop, 0, (size_t)cfg->n);
      int* stack = (int*)xmalloc(sizeof(int)*(size_t)cfg->n);
      int sp = 0;
      in_loop[h] = 1;
      in_loop[b] = 1;
      stack[sp++] = b;
      while(sp>0){
        int x = stack[--sp];
        IRBlock* xb = &cfg->blocks[x];
        for(int pi=0; pi<xb->pred_n; pi++){
          int p = xb->pred[pi];
          if(!in_loop[p]){
            in_loop[p] = 1;
            if(p != h) stack[sp++] = p;
          }
        }
      }

      // find preheader
      int preheader = -1;
      for(int pi=0; pi<cfg->blocks[h].pred_n; pi++){
        int p = cfg->blocks[h].pred[pi];
        if(!in_loop[p]){
          if(preheader != -1){ preheader = -1; break; }
          preheader = p;
        }
      }
      if(preheader < 0){
        free(in_loop);
        free(stack);
        continue;
      }

      unsigned char* loop_defs = NULL;
      if(regs>0){
        loop_defs = (unsigned char*)xmalloc((size_t)regs);
        memset(loop_defs, 0, (size_t)regs);
        for(int r=0;r<regs;r++){
          int db = reg_def[r];
          if(db>=0 && db<cfg->n && in_loop[db]) loop_defs[r] = 1;
        }
      }

      unsigned char* inv_regs = NULL;
      unsigned char* inv_ins = (unsigned char*)xmalloc((size_t)F->n);
      memset(inv_ins, 0, (size_t)F->n);
      if(regs>0){
        inv_regs = (unsigned char*)xmalloc((size_t)regs);
        memset(inv_regs, 0, (size_t)regs);
      }

      int progress = 1;
      while(progress){
        progress = 0;
        for(int bi=0; bi<cfg->n; bi++){
          if(!in_loop[bi]) continue;
          IRBlock* lb = &cfg->blocks[bi];
          for(int i=lb->start;i<=lb->end;i++){
            IRIns* in = &F->ins[i];
            if(inv_ins[i]) continue;
            int d = ins_def_reg(in);
            if(d<0) continue;
            if(!ins_is_pure(in)) continue;
            if(reg_uses_loopdef(in, loop_defs, inv_regs, regs)) continue;
            inv_ins[i] = 1;
            if(inv_regs && d>=0 && d<regs) inv_regs[d] = 1;
            progress = 1;
          }
        }
      }

      int any_inv = 0;
      for(int i=0;i<F->n;i++) if(inv_ins[i]){ any_inv = 1; break; }
      if(any_inv){
        int ph_start = cfg->blocks[preheader].start;
        int ph_end = cfg->blocks[preheader].end;
        int ph_last = ph_end;
        while(ph_last >= ph_start && F->ins[ph_last].op==I_LABEL) ph_last--;
        int insert_at = ph_end + 1;
        if(ph_last >= ph_start){
          IROp op = F->ins[ph_last].op;
          if(op==I_JMP || op==I_JZ || op==I_RET){
            insert_at = ph_last;
          }
        }
        IRIns* out = (IRIns*)xmalloc(sizeof(IRIns)*(size_t)F->n);
        int w = 0;
        int inserted = 0;
        for(int i=0;i<F->n;i++){
          if(i==insert_at && !inserted){
            for(int j=0;j<F->n;j++){
              if(inv_ins[j]) out[w++] = F->ins[j];
            }
            inserted = 1;
          }
          if(inv_ins[i]){
            continue;
          }
          out[w++] = F->ins[i];
        }
        if(!inserted){
          for(int j=0;j<F->n;j++){
            if(inv_ins[j]) out[w++] = F->ins[j];
          }
        }
        free(F->ins);
        F->ins = out;
        F->n = w;
        F->cap = w;
        changed = 1;
      }

      if(loop_defs) free(loop_defs);
      if(inv_regs) free(inv_regs);
      free(inv_ins);
      free(in_loop);
      free(stack);

      if(changed) break;
    }
    if(changed) break;
  }

  if(reg_def) free(reg_def);
  return changed;
}

static IRFunc* ir_find_func(IRModule* M, const char* name, int len){
  if(!M || !name) return NULL;
  for(int i=0;i<M->fn_n;i++){
    IRFunc* F = &M->fns[i];
    if(F->len==len && strncmp(F->name, name, (size_t)len)==0) return F;
  }
  return NULL;
}

static int ir_ins_cost(const IRIns* in){
  switch(in->op){
    case I_CALL:
    case I_CALLPTR: return 8;
    case I_LOAD:
    case I_STORE: return 4;
    case I_ALLOCA: return 4;
    case I_JMP:
    case I_JZ: return 2;
    case I_RET: return 1;
    case I_LABEL: return 0;
    default: return 1;
  }
}

static int ir_func_cost(IRFunc* F){
  int cost = 0;
  for(int i=0;i<F->n;i++){
    cost += ir_ins_cost(&F->ins[i]);
  }
  return cost;
}

static int ir_func_has_call(IRFunc* F){
  for(int i=0;i<F->n;i++){
    if(F->ins[i].op==I_CALL || F->ins[i].op==I_CALLPTR) return 1;
  }
  return 0;
}

static int ir_func_has_branch(IRFunc* F){
  for(int i=0;i<F->n;i++){
    if(F->ins[i].op==I_JMP || F->ins[i].op==I_JZ) return 1;
  }
  return 0;
}

static int ir_func_has_backedge(IRFunc* F){
  int max_label = -1;
  for(int i=0;i<F->n;i++){
    IRIns* in = &F->ins[i];
    if(in->op==I_LABEL && in->a > max_label) max_label = in->a;
    if(in->op==I_JMP && in->a > max_label) max_label = in->a;
    if(in->op==I_JZ && in->b > max_label) max_label = in->b;
  }
  if(max_label < 0) return 0;
  int* label_idx = (int*)xmalloc(sizeof(int)*(size_t)(max_label+1));
  for(int i=0;i<=max_label;i++) label_idx[i] = -1;
  for(int i=0;i<F->n;i++){
    if(F->ins[i].op==I_LABEL) label_idx[F->ins[i].a] = i;
  }
  int has = 0;
  for(int i=0;i<F->n;i++){
    IRIns* in = &F->ins[i];
    if(in->op==I_JMP || in->op==I_JZ){
      int t = (in->op==I_JMP) ? in->a : in->b;
      if(t>=0 && t<=max_label && label_idx[t]>=0 && label_idx[t] < i){
        has = 1;
        break;
      }
    }
  }
  free(label_idx);
  return has;
}

static int inline_candidate(IRFunc* F){
  if(!F || F->is_extern) return 0;
  if(F->n <= 0) return 0;
  if(F->force_inline) return 1;
  int cost = ir_func_cost(F);
  if(cost > 80) return 0;
  int has_loop = ir_func_has_backedge(F);
  int has_call = ir_func_has_call(F);
  int has_branch = ir_func_has_branch(F);
  if(has_loop) return 0;
  if(has_call && cost > 24) return 0;
  if(has_branch) return 0;
  return 1;
}

static int collect_param_regs(IRFunc* F, int* out, int cap){
  int n = 0;
  if(!F || !out || cap <= 0) return 0;
  if(F->param_regs && F->param_regs_n > 0){
    int lim = F->param_regs_n;
    if(lim > cap) lim = cap;
    for(int i=0;i<lim;i++) out[n++] = F->param_regs[i];
    return n;
  }
  for(int i=0;i<F->n;i++){
    IRIns* in = &F->ins[i];
    if(in->op==I_LABEL) break;
    if(in->op==I_STORE){
      if(n < cap){
        out[n++] = in->b;
      }
      if(n >= F->param_count) break;
    }
  }
  return n;
}

static int inline_small_funcs(IRModule* M){
  if(!M) return 0;
  int changed = 0;

  for(int fi=0; fi<M->fn_n; fi++){
    IRFunc* F = &M->fns[fi];
    if(F->n <= 0) continue;

    IRIns* out = NULL;
    int cap = 0;
    int n = 0;
    int f_changed = 0;
    #define EMIT(_in) do { \
      if(n==cap){ cap = cap? cap*2 : (F->n + 32); out = (IRIns*)xrealloc(out, sizeof(IRIns)*(size_t)cap); } \
      out[n++] = (_in); \
    } while(0)

    for(int i=0;i<F->n;i++){
      IRIns in = F->ins[i];
      if(in.op==I_CALL){
        IRFunc* cal = ir_find_func(M, in.name, in.nlen);
        if(cal && cal!=F && inline_candidate(cal) && cal->param_count==in.argc){
          int map_n = (cal->max_reg>=0 ? (cal->max_reg+1) : 0);
          int* map = (int*)xmalloc(sizeof(int)*(size_t)(map_n>0?map_n:1));
          for(int k=0;k<map_n;k++) map[k] = -1;
          if(cal->param_count > 0){
            int* preg = (int*)xmalloc(sizeof(int)*(size_t)cal->param_count);
            int pn = collect_param_regs(cal, preg, cal->param_count);
            for(int k=0;k<pn && k<in.argc;k++){
              int pr = preg[k];
              if(pr >= 0 && pr < map_n) map[pr] = in.args[k];
            }
            free(preg);
          }

          for(int k=0;k<cal->n;k++){
            IRIns ci = cal->ins[k];
            if(ci.op==I_LABEL) continue;
            if(ci.op==I_RET){
              if(in.a>=0){
                IRIns mv; memset(&mv,0,sizeof(mv));
                mv.op = I_MOV;
                mv.a = in.a;
                mv.b = (ci.a>=0 && ci.a<map_n && map[ci.a]>=0) ? map[ci.a] : ci.a;
                EMIT(mv);
              }
              break;
            }

            IRIns ni = ci;
            if(ni.args && ni.argc > 0){
              int* na = (int*)xmalloc(sizeof(int)*(size_t)ni.argc);
              for(int t=0;t<ni.argc;t++) na[t] = ni.args[t];
              ni.args = na;
            }
            if(ni.name_owned && ni.name){
              char* nn = (char*)xmalloc((size_t)ni.nlen + 1);
              memcpy(nn, ni.name, (size_t)ni.nlen);
              nn[ni.nlen] = 0;
              ni.name = nn;
              ni.name_owned = 1;
            } else {
              ni.name_owned = 0;
            }
            // remap regs
            int ra = ci.a;
            int rb = ci.b;
            int rc = ci.c;
            if(ra>=0){
              if(ra<map_n){
                if(map[ra]<0) map[ra]=new_reg(F);
                ni.a = map[ra];
              } else {
                ni.a = ra;
              }
            }
            if(rb>=0){
              if(rb<map_n){
                if(map[rb]<0) map[rb]=new_reg(F);
                ni.b = map[rb];
              } else {
                ni.b = rb;
              }
            }
            if(rc>=0){
              if(rc<map_n){
                if(map[rc]<0) map[rc]=new_reg(F);
                ni.c = map[rc];
              } else {
                ni.c = rc;
              }
            }
            if(ni.args && ni.argc > 0){
              for(int t=0;t<ni.argc;t++){
                int r = ni.args[t];
                if(r>=0){
                  if(r < map_n){
                    if(map[r] < 0) map[r] = new_reg(F);
                    ni.args[t] = map[r];
                  } else {
                    ni.args[t] = r;
                  }
                }
              }
            }
            EMIT(ni);
          }

          if(in.args) free(in.args);
          free(map);
          f_changed = 1;
          changed = 1;
          continue;
        }
      }

      EMIT(in);
    }

    if(f_changed){
      free(F->ins);
      F->ins = out;
      F->n = n;
      F->cap = cap;
    } else {
      free(out);
    }
    #undef EMIT
  }

  return changed;
}

static void dce_locals(IRFunc* F){
  int regs = (F->max_reg>=0 ? (F->max_reg+1) : 0);
  if(regs<=0 || F->n<=0) return;
  char* live = (char*)xmalloc((size_t)regs);
  memset(live, 0, (size_t)regs);

  IRIns* tmp = (IRIns*)xmalloc(sizeof(IRIns)*(size_t)F->n);
  int w = 0;
  for(int i=F->n-1; i>=0; i--){
    IRIns in = F->ins[i];
    int def = ins_def_reg(&in);
    int keep = ins_has_side_effect(&in);
    if(def>=0 && def<regs && live[def]) keep = 1;

    if(!keep){
      if((in.op==I_CALL || in.op==I_CALLPTR) && in.args) free(in.args);
      continue;
    }

    if(def>=0 && def<regs) live[def]=0;
    ins_mark_uses(&in, live, regs);
    tmp[w++] = in;
  }

  // reverse into F->ins
  for(int i=0;i<w;i++){
    F->ins[i] = tmp[w-1-i];
  }
  F->n = w;
  free(tmp);
  free(live);
}

static void ir_verify_ssa(IRFunc* F){
  int regs = (F->max_reg>=0 ? (F->max_reg+1) : 0);
  if(regs<=0 || F->n<=0) return;
  unsigned char* def = (unsigned char*)xmalloc((size_t)regs);
  memset(def, 0, (size_t)regs);
  for(int i=0;i<F->n;i++){
    int d = ins_def_reg(&F->ins[i]);
    if(d>=0 && d<regs){
      if(def[d]) die("IR: SSA violation (reg defined twice)");
      def[d] = 1;
    }
  }
  free(def);
}

static int fixup_missing_labels(IRFunc* F){
  if(!F || F->n<=0) return 0;
  int max_label = -1;
  for(int i=0;i<F->n;i++){
    IRIns* in = &F->ins[i];
    if(in->op==I_LABEL && in->a > max_label) max_label = in->a;
    if(in->op==I_JMP && in->a > max_label) max_label = in->a;
    if(in->op==I_JZ && in->b > max_label) max_label = in->b;
  }
  if(max_label < 0) return 0;
  if(F->next_label <= max_label) F->next_label = max_label + 1;

  unsigned char* present = (unsigned char*)xmalloc((size_t)(max_label+1));
  unsigned char* need = (unsigned char*)xmalloc((size_t)(max_label+1));
  int* next = (int*)xmalloc(sizeof(int)*(size_t)(max_label+1));
  memset(present, 0, (size_t)(max_label+1));
  memset(need, 0, (size_t)(max_label+1));

  for(int i=0;i<F->n;i++){
    IRIns* in = &F->ins[i];
    if(in->op==I_LABEL && in->a>=0 && in->a<=max_label) present[in->a] = 1;
  }

  int next_label = -1;
  for(int i=max_label;i>=0;i--){
    if(present[i]) next_label = i;
    next[i] = next_label;
  }

  int changed = 0;
  for(int i=0;i<F->n;i++){
    IRIns* in = &F->ins[i];
    if(in->op!=I_JMP && in->op!=I_JZ) continue;
    int* tgtp = (in->op==I_JMP) ? &in->a : &in->b;
    int t = *tgtp;
    if(t>=0 && t<=max_label && !present[t]){
      if(next[t] >= 0){
        *tgtp = next[t];
        changed = 1;
      } else {
        need[t] = 1;
      }
    }
  }

  int add_count = 0;
  for(int i=0;i<=max_label;i++) if(need[i]) add_count++;
  if(add_count > 0){
    int insert_pos = F->n;
    if(F->n>0 && F->ins[F->n-1].op==I_RET) insert_pos = F->n - 1;
    IRIns* out = (IRIns*)xmalloc(sizeof(IRIns)*(size_t)(F->n + add_count));
    int w = 0;
    for(int i=0;i<insert_pos;i++) out[w++] = F->ins[i];
    for(int i=0;i<=max_label;i++){
      if(need[i]){
        IRIns lab; memset(&lab,0,sizeof(lab));
        lab.op = I_LABEL; lab.a = i;
        out[w++] = lab;
        present[i] = 1;
        changed = 1;
      }
    }
    for(int i=insert_pos;i<F->n;i++) out[w++] = F->ins[i];
    free(F->ins);
    F->ins = out;
    F->n = w;
    F->cap = w;
  }

  free(present);
  free(need);
  free(next);
  return changed;
}

static void promote_single_assign_locals(IRFunc* F){
  if(!F || F->n <= 0) return;
  int regs = (F->max_reg>=0 ? (F->max_reg+1) : F->next_reg);
  if(regs <= 0) return;

  unsigned char* is_alloca = (unsigned char*)xmalloc((size_t)regs);
  memset(is_alloca, 0, (size_t)regs);

  int* alloca_size = (int*)xmalloc(sizeof(int)*(size_t)regs);
  memset(alloca_size, 0, sizeof(int)*(size_t)regs);

  int* store_count = (int*)xmalloc(sizeof(int)*(size_t)regs);
  memset(store_count, 0, sizeof(int)*(size_t)regs);

  int* escape_count = (int*)xmalloc(sizeof(int)*(size_t)regs);
  memset(escape_count, 0, sizeof(int)*(size_t)regs);

  int* store_val = (int*)xmalloc(sizeof(int)*(size_t)regs);
  for(int i=0;i<regs;i++) store_val[i] = -1;

  int* store_idx = (int*)xmalloc(sizeof(int)*(size_t)regs);
  for(int i=0;i<regs;i++) store_idx[i] = -1;

  int* first_load_idx = (int*)xmalloc(sizeof(int)*(size_t)regs);
  for(int i=0;i<regs;i++) first_load_idx[i] = -1;

  for(int i=0;i<F->n;i++){
    IRIns* in = &F->ins[i];
    if(in->op==I_ALLOCA){
      if(in->a >= 0 && in->a < regs){
        is_alloca[in->a] = 1;
        alloca_size[in->a] = in->size;
      }
    }
  }

  for(int i=0;i<F->n;i++){
    IRIns* in = &F->ins[i];
    if(in->op==I_STORE){
      int addr = in->a;
      if(addr>=0 && addr<regs && is_alloca[addr]){
        store_count[addr]++;
        if(store_count[addr]==1){
          store_val[addr] = in->b;
          store_idx[addr] = i;
        }
      }
      int val = in->b;
      if(val>=0 && val<regs && is_alloca[val]) escape_count[val]++;
    } else if(in->op==I_LOAD){
      int addr = in->b;
      if(addr>=0 && addr<regs && is_alloca[addr]){
        if(first_load_idx[addr] < 0) first_load_idx[addr] = i;
      }
    } else if(in->op==I_ALLOCA){
      // skip
    } else if(in->op==I_RET){
      if(in->a>=0 && in->a<regs && is_alloca[in->a]) escape_count[in->a]++;
    } else {
      if(in->a>=0 && in->a<regs && is_alloca[in->a] && in->op!=I_LABEL && in->op!=I_JMP && in->op!=I_JZ) escape_count[in->a]++;
      if(in->b>=0 && in->b<regs && is_alloca[in->b]) escape_count[in->b]++;
      if(in->c>=0 && in->c<regs && is_alloca[in->c]) escape_count[in->c]++;
      if((in->op==I_CALL || in->op==I_CALLPTR) && in->args){
        for(int j=0;j<in->argc;j++){
          if(in->args[j]>=0 && in->args[j]<regs && is_alloca[in->args[j]]) escape_count[in->args[j]]++;
        }
      }
    }
  }

  for(int r=0;r<regs;r++){
    if(is_alloca[r] && store_count[r]==1 && escape_count[r]==0){
      if(alloca_size[r] > 0 && alloca_size[r] < 8) continue;
      if(first_load_idx[r] >= 0 && store_idx[r] >= first_load_idx[r]) continue;
      
      int val = store_val[r];
      if(val < 0) continue;
      
      for(int i=0;i<F->n;i++){
        if(F->ins[i].op==I_LOAD && F->ins[i].b==r){
          F->ins[i].op = I_MOV;
          F->ins[i].b = val;
        } else if(F->ins[i].op==I_STORE && F->ins[i].a==r){
          int z = new_reg(F);
          F->ins[i].op = I_MOV;
          F->ins[i].a = z;
          F->ins[i].b = z;
        } else if(F->ins[i].op==I_ALLOCA && F->ins[i].a==r){
          int z = new_reg(F);
          F->ins[i].op = I_MOV;
          F->ins[i].a = z;
          F->ins[i].b = z;
        }
      }
    }
  }

  free(is_alloca);
  free(alloca_size);
  free(store_count);
  free(escape_count);
  free(store_val);
  free(store_idx);
  free(first_load_idx);
}

void ir_optimize(IRModule* M){
  if(!M) return;
  inline_small_funcs(M);
  for(int fi=0; fi<M->fn_n; fi++){
    IRFunc* F=&M->fns[fi];
    promote_single_assign_locals(F);
    for(int it=0; it<2; it++){
      peephole_remove_trivial(F);
      const_fold_block_local(F);
      strength_reduce_pow2(F);
      IRCfg* cfg0 = cfg_build(F);
      if(cfg0){
        dce_unreachable(F, cfg0);
        cfg_free(cfg0);
      }
      IRCfg* cfg1 = cfg_build(F);
      if(cfg1){
        gvn_func(F, cfg1);
        licm_cfg(F, cfg1);
        cfg_free(cfg1);
      }
      licm_simple(F);
      dce_locals(F);
      peephole_remove_trivial(F);
    }
    fixup_missing_labels(F);
    ir_verify_ssa(F);
  }
}

void ir_free(IRModule* M){
  if(!M) return;

  for(int i=0;i<M->str_n;i++) free(M->strs[i].bytes);
  free(M->strs);
  for(int i=0;i<M->global_n;i++){
    if(M->globals[i].name_owned && M->globals[i].name) free((void*)M->globals[i].name);
  }
  free(M->globals);

  for(int fi=0; fi<M->fn_n; fi++){
    IRFunc* F=&M->fns[fi];
    for(int i=0;i<F->n;i++){
      if(F->ins[i].args) free(F->ins[i].args);
      if(F->ins[i].name_owned && F->ins[i].name) free((void*)F->ins[i].name);
    }
    free(F->ins);
    free(F->param_regs);
    free(F->vname);
    free(F->vlen);
    free(F->vaddr);
    free(F->vty);
    free(F->brk);
    free(F->cont);
    if(F->name_owned && F->name) free((void*)F->name);
  }
  free(M->fns);
  free(M);
}
