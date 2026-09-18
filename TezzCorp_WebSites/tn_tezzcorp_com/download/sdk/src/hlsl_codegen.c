// src/hlsl_codegen.c
// HLSL compute backend for restricted kernel subset.
#include "hlsl_codegen.h"
#include "util.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

typedef struct {
  FILE* f;
  int indent;
  int temp_id;
  TypeEnv* tenv;
} HlslCtx;

static void* xmalloc(size_t n){ void* p = malloc(n); if(!p) die("out of memory"); return p; }

static void hlsl_unsupported(SrcLoc loc, const char* msg);

static void emit_indent(HlslCtx* c){
  for(int i=0;i<c->indent;i++) fputs("  ", c->f);
}

static void emit_line(HlslCtx* c, const char* fmt, ...){
  emit_indent(c);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(c->f, fmt, ap);
  va_end(ap);
  fputc('\n', c->f);
}

static int is_float_ty(Type* t){ return t && t->k==TY_F64; }
static int is_ptr_ty(Type* t){ return t && t->k==TY_PTR; }

static const char* hlsl_type_name(Type* t){
  if(is_float_ty(t)){
    return "double";
  }
  return "int64_t";
}

static int is_hlsl_reserved(const char* s, int len){
  static const char* reserved[] = {
    "out","in","inout","struct","typedef","static","const","register",
    "row_major","column_major","globallycoherent","volatile","groupshared",
    "shared","cbuffer","tbuffer"
  };
  for(size_t i=0;i<sizeof(reserved)/sizeof(reserved[0]);i++){
    const char* r = reserved[i];
    int rlen = (int)strlen(r);
    if(len==rlen && strncmp(s, r, (size_t)len)==0) return 1;
  }
  return 0;
}

static char* mangle_ident(const char* in, int len){
  int cap = len * 2 + 8;
  char* out = (char*)xmalloc((size_t)cap);
  int w = 0;
  if(len <= 0){
    strcpy(out, "v");
    return out;
  }
  char c0 = in[0];
  if(!(isalpha((unsigned char)c0) || c0=='_') || is_hlsl_reserved(in, len)){
    out[w++] = '_';
  }
  for(int i=0;i<len && w < cap-1;i++){
    char c = in[i];
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'){
      out[w++] = c;
    } else {
      out[w++] = '_';
    }
  }
  out[w] = 0;
  return out;
}

static char* new_temp(HlslCtx* c){
  char buf[32];
  snprintf(buf, sizeof(buf), "t%d", c->temp_id++);
  size_t n = strlen(buf);
  char* out = (char*)xmalloc(n+1);
  memcpy(out, buf, n+1);
  return out;
}

typedef struct {
  char* name;
  Type* ty;
} HVal;

static HVal emit_expr(HlslCtx* c, Expr* e);
static HVal emit_addr(HlslCtx* c, Expr* e);

static HVal emit_temp(HlslCtx* c, Type* ty, const char* expr){
  HVal v;
  v.name = new_temp(c);
  v.ty = ty;
  emit_line(c, "%s %s = %s;", hlsl_type_name(ty), v.name, expr);
  return v;
}

static HVal emit_load(HlslCtx* c, Type* ty, HVal addr){
  if(ty && ty->k==TY_U8){
    char expr[128];
    snprintf(expr, sizeof(expr), "(int64_t)tn_load_u8((uint64_t)%s)", addr.name);
    return emit_temp(c, c->tenv->b.ty_i64, expr);
  }
  if(is_float_ty(ty)){
    hlsl_unsupported((SrcLoc){0}, "HLSL backend does not support float yet");
  }
  {
    char expr[128];
    snprintf(expr, sizeof(expr), "tn_load_i64((uint64_t)%s)", addr.name);
    return emit_temp(c, c->tenv->b.ty_i64, expr);
  }
}

static void emit_store(HlslCtx* c, Type* ty, HVal addr, HVal value){
  if(ty && ty->k==TY_U8){
    emit_line(c, "tn_store_u8((uint64_t)%s, (uint)%s);", addr.name, value.name);
    return;
  }
  if(is_float_ty(ty)){
    hlsl_unsupported(addr.ty ? addr.ty->decl_loc : (SrcLoc){0}, "HLSL backend does not support float yet");
  }
  emit_line(c, "tn_store_i64((uint64_t)%s, (int64_t)%s);", addr.name, value.name);
}

static void hlsl_unsupported(SrcLoc loc, const char* msg){
  err_at(loc, msg);
}

static HVal emit_cast(HlslCtx* c, Type* to, HVal v){
  if(!to) return v;
  if(is_float_ty(to)){
    hlsl_unsupported((SrcLoc){0}, "HLSL backend does not support float yet");
  }
  if(is_float_ty(v.ty) && !is_float_ty(to)){
    hlsl_unsupported((SrcLoc){0}, "HLSL backend does not support float yet");
  }
  if(is_ptr_ty(to)){
    char expr[128];
    snprintf(expr, sizeof(expr), "(int64_t)%s", v.name);
    return emit_temp(c, to, expr);
  }
  return v;
}

static HVal emit_expr(HlslCtx* c, Expr* e){
  if(!e){
    return emit_temp(c, c->tenv->b.ty_i64, "0");
  }
  switch(e->k){
    case EX_NUM: {
      char expr[128];
      snprintf(expr, sizeof(expr), "%lld", (long long)e->num);
      return emit_temp(c, c->tenv->b.ty_i64, expr);
    }
    case EX_FLOAT: {
      hlsl_unsupported(e->loc, "HLSL backend does not support float yet");
      break;
    }
    case EX_NAME: {
      HVal v;
      v.ty = e->ty;
      v.name = mangle_ident(e->name.name, e->name.len);
      return v;
    }
    case EX_CALL: {
      Expr* callee = e->call.callee;
      if(callee && callee->k==EX_NAME && callee->name.len==7 &&
         strncmp(callee->name.name, "gpu_tid", 7)==0){
        return emit_temp(c, c->tenv->b.ty_i64, "(int64_t)DTid.x");
      }
      hlsl_unsupported(e->loc, "HLSL kernel subset only supports gpu_tid() call");
      break;
    }
    case EX_UN: {
      HVal v = emit_expr(c, e->un.e);
      if(e->un.op==UN_NEG){
        char expr[128];
        snprintf(expr, sizeof(expr), "(-%s)", v.name);
        return emit_temp(c, v.ty, expr);
      }
      if(e->un.op==UN_LNOT){
        char expr[128];
        snprintf(expr, sizeof(expr), "((%s)==0 ? 1 : 0)", v.name);
        return emit_temp(c, c->tenv->b.ty_i64, expr);
      }
      if(e->un.op==UN_BNOT){
        char expr[128];
        snprintf(expr, sizeof(expr), "(~%s)", v.name);
        return emit_temp(c, c->tenv->b.ty_i64, expr);
      }
      return v;
    }
    case EX_BIN: {
      BinOp op = e->bin.op;
      Type* rty = e->ty;

      // pointer arithmetic
      if((op==OP_ADD || op==OP_SUB) && (is_ptr_ty(e->bin.a->ty) || is_ptr_ty(e->bin.b->ty))){
        Expr* pexpr = is_ptr_ty(e->bin.a->ty) ? e->bin.a : e->bin.b;
        Expr* iexpr = (pexpr == e->bin.a) ? e->bin.b : e->bin.a;
        Type* pty = pexpr->ty;
        Type* elem = pty ? pty->elem : NULL;
        int esz = elem ? type_size(elem) : 0;
        if(esz <= 0) hlsl_unsupported(e->loc, "invalid pointer element size for HLSL");
        HVal pv = emit_expr(c, pexpr);
        HVal iv = emit_expr(c, iexpr);
        char mul_expr[128];
        snprintf(mul_expr, sizeof(mul_expr), "(%s * %d)", iv.name, esz);
        HVal off = emit_temp(c, c->tenv->b.ty_i64, mul_expr);
        if(op==OP_SUB && pexpr==e->bin.a){
          char expr[128];
          snprintf(expr, sizeof(expr), "(%s - %s)", pv.name, off.name);
          return emit_temp(c, rty, expr);
        }
        if(op==OP_ADD){
          char expr[128];
          snprintf(expr, sizeof(expr), "(%s + %s)", pv.name, off.name);
          return emit_temp(c, rty, expr);
        }
        hlsl_unsupported(e->loc, "unsupported pointer arithmetic form in HLSL");
      }

      HVal a = emit_expr(c, e->bin.a);
      HVal b = emit_expr(c, e->bin.b);

      if(op==OP_EQ || op==OP_NEQ || op==OP_LT || op==OP_LTE || op==OP_GT || op==OP_GTE){
        const char* cop = "==";
        if(op==OP_NEQ) cop = "!=";
        else if(op==OP_LT) cop = "<";
        else if(op==OP_LTE) cop = "<=";
        else if(op==OP_GT) cop = ">";
        else if(op==OP_GTE) cop = ">=";
        char expr[128];
        snprintf(expr, sizeof(expr), "((%s %s %s) ? 1 : 0)", a.name, cop, b.name);
        return emit_temp(c, c->tenv->b.ty_i64, expr);
      }

      if(op==OP_LAND || op==OP_LOR){
        const char* lop = (op==OP_LAND) ? "&&" : "||";
        char expr[160];
        snprintf(expr, sizeof(expr), "(((%s)!=0 %s (%s)!=0) ? 1 : 0)", a.name, lop, b.name);
        return emit_temp(c, c->tenv->b.ty_i64, expr);
      }

      if(is_float_ty(rty) || is_float_ty(a.ty) || is_float_ty(b.ty)){
        hlsl_unsupported(e->loc, "HLSL backend does not support float yet");
      }

      const char* bop = "+";
      switch(op){
        case OP_ADD: bop = "+"; break;
        case OP_SUB: bop = "-"; break;
        case OP_MUL: bop = "*"; break;
        case OP_DIV: bop = "/"; break;
        case OP_MOD: bop = "%"; break;
        case OP_BAND: bop = "&"; break;
        case OP_BOR: bop = "|"; break;
        case OP_BXOR: bop = "^"; break;
        case OP_SHL: bop = "<<"; break;
        case OP_SHR: bop = ">>"; break;
        default: bop = "+"; break;
      }
      char expr[160];
      snprintf(expr, sizeof(expr), "(%s %s %s)", a.name, bop, b.name);
      return emit_temp(c, rty ? rty : c->tenv->b.ty_i64, expr);
    }
    case EX_INDEX: {
      HVal addr = emit_addr(c, e);
      return emit_load(c, e->ty, addr);
    }
    case EX_DOT: {
      HVal addr = emit_addr(c, e);
      return emit_load(c, e->ty, addr);
    }
    case EX_DEREF: {
      HVal addr = emit_addr(c, e);
      return emit_load(c, e->ty, addr);
    }
    case EX_CAST: {
      HVal v = emit_expr(c, e->cast.e);
      return emit_cast(c, e->cast.to, v);
    }
    case EX_SIZEOF: {
      long long s = 0;
      if(e->siz.is_type){
        s = type_size(e->siz.ty);
      } else if(e->siz.e && e->siz.e->ty){
        s = type_size(e->siz.e->ty);
      }
      char expr[64];
      snprintf(expr, sizeof(expr), "%lld", s);
      return emit_temp(c, c->tenv->b.ty_i64, expr);
    }
    case EX_ALIGNOF: {
      long long s = 0;
      if(e->siz.is_type){
        s = type_align(e->siz.ty);
      } else if(e->siz.e && e->siz.e->ty){
        s = type_align(e->siz.e->ty);
      }
      char expr[64];
      snprintf(expr, sizeof(expr), "%lld", s);
      return emit_temp(c, c->tenv->b.ty_i64, expr);
    }
    case EX_STR:
      hlsl_unsupported(e->loc, "string literals not supported in HLSL kernels");
      break;
    case EX_ARRAY_LIT:
    case EX_STRUCT_LIT:
      hlsl_unsupported(e->loc, "literal aggregates not supported in HLSL kernels");
      break;
    case EX_ADDR:
      hlsl_unsupported(e->loc, "address-of is not supported in HLSL kernels");
      break;
  }
  return emit_temp(c, c->tenv->b.ty_i64, "0");
}

static HVal emit_addr(HlslCtx* c, Expr* e){
  if(!e) return emit_temp(c, c->tenv->b.ty_i64, "0");
  switch(e->k){
    case EX_NAME: {
      if(is_ptr_ty(e->ty)){
        return emit_expr(c, e);
      }
      hlsl_unsupported(e->loc, "HLSL kernel requires addressable lvalue");
      break;
    }
    case EX_DEREF: {
      return emit_expr(c, e->un.e);
    }
    case EX_INDEX: {
      Expr* base = e->index.base;
      Expr* idx = e->index.idx;
      if(!base || !idx) hlsl_unsupported(e->loc, "invalid index expression");
      Type* bty = base->ty;
      Type* elem = NULL;
      if(bty && bty->k==TY_PTR) elem = bty->elem;
      if(!elem) hlsl_unsupported(e->loc, "indexing only supported on pointer types in HLSL");
      int esz = type_size(elem);
      HVal b = emit_expr(c, base);
      HVal i = emit_expr(c, idx);
      char off_expr[128];
      snprintf(off_expr, sizeof(off_expr), "(%s * %d)", i.name, esz);
      HVal off = emit_temp(c, c->tenv->b.ty_i64, off_expr);
      char addr_expr[128];
      snprintf(addr_expr, sizeof(addr_expr), "(%s + %s)", b.name, off.name);
      return emit_temp(c, c->tenv->b.ty_i64, addr_expr);
    }
    case EX_DOT: {
      Expr* base = e->dot.base;
      if(!base) hlsl_unsupported(e->loc, "invalid field access");
      Type* bty = base->ty;
      Type* st = bty;
      if(bty && bty->k==TY_PTR) st = bty->elem;
      if(!st || st->k!=TY_STRUCT) hlsl_unsupported(e->loc, "field access on non-struct in HLSL");
      Field* fld = type_find_field(st, e->dot.mem, e->dot.memlen);
      if(!fld) hlsl_unsupported(e->loc, "unknown struct field in HLSL");
      HVal baddr = emit_addr(c, base);
      char addr_expr[128];
      snprintf(addr_expr, sizeof(addr_expr), "(%s + %d)", baddr.name, fld->off);
      return emit_temp(c, c->tenv->b.ty_i64, addr_expr);
    }
    default:
      hlsl_unsupported(e->loc, "unsupported lvalue in HLSL kernel");
      break;
  }
  return emit_temp(c, c->tenv->b.ty_i64, "0");
}

static void emit_stmt(HlslCtx* c, Stmt* s);

static void emit_stmt_block_items(HlslCtx* c, Stmt* s){
  if(!s) return;
  if(s->k==ST_BLOCK){
    for(int i=0;i<s->block.n;i++) emit_stmt(c, s->block.items[i]);
  } else {
    emit_stmt(c, s);
  }
}

static void emit_stmt(HlslCtx* c, Stmt* s){
  if(!s) return;
  switch(s->k){
    case ST_BLOCK:
      emit_line(c, "{");
      c->indent++;
      for(int i=0;i<s->block.n;i++) emit_stmt(c, s->block.items[i]);
      c->indent--;
      emit_line(c, "}");
      return;
    case ST_UNSAFE:
      emit_stmt(c, s->uns.body);
      return;
    case ST_LET: {
      char* name = mangle_ident(s->let.name, s->let.len);
      const char* tyname = hlsl_type_name(s->let.ty);
      if(s->let.init){
        HVal v = emit_expr(c, s->let.init);
        HVal cv = emit_cast(c, s->let.ty, v);
        emit_line(c, "%s %s = %s;", tyname, name, cv.name);
      } else {
        emit_line(c, "%s %s = 0;", tyname, name);
      }
      return;
    }
    case ST_ASSIGN: {
      TokenKind op = s->asg.op;
      Expr* lhs = s->asg.lhs;
      Expr* rhs = s->asg.rhs;
      if(!lhs) return;
      if(op==TK_ASSIGN){
        if(lhs->k==EX_NAME){
          HVal rv = emit_expr(c, rhs);
          emit_line(c, "%s = %s;", mangle_ident(lhs->name.name, lhs->name.len), rv.name);
        } else {
          HVal addr = emit_addr(c, lhs);
          HVal rv = emit_expr(c, rhs);
          emit_store(c, lhs->ty, addr, rv);
        }
        return;
      }

      // compound assignment
      const char* bop = NULL;
      if(op==TK_PLUSEQ) bop = "+";
      else if(op==TK_MINUSEQ) bop = "-";
      else if(op==TK_STAREQ) bop = "*";
      else if(op==TK_SLASHEQ) bop = "/";
      else if(op==TK_PERCENTEQ) bop = "%";
      else if(op==TK_ANDEQ) bop = "&";
      else if(op==TK_OREQ) bop = "|";
      else if(op==TK_XOREQ) bop = "^";
      else if(op==TK_SHLEQ) bop = "<<";
      else if(op==TK_SHREQ) bop = ">>";

      if(bop){
        if(lhs->k==EX_NAME){
          char* lname = mangle_ident(lhs->name.name, lhs->name.len);
          HVal rv = emit_expr(c, rhs);
          emit_line(c, "%s = (%s %s %s);", lname, lname, bop, rv.name);
        } else {
          HVal addr = emit_addr(c, lhs);
          HVal cur = emit_load(c, lhs->ty, addr);
          HVal rv = emit_expr(c, rhs);
          char expr[160];
          snprintf(expr, sizeof(expr), "(%s %s %s)", cur.name, bop, rv.name);
          HVal res = emit_temp(c, lhs->ty, expr);
          emit_store(c, lhs->ty, addr, res);
        }
        return;
      }

      hlsl_unsupported(s->loc, "unsupported assignment op in HLSL kernel");
      return;
    }
    case ST_EXPR: {
      (void)emit_expr(c, s->expr.e);
      return;
    }
    case ST_IF: {
      HVal cond = emit_expr(c, s->iff.cond);
      emit_line(c, "if ((%s) != 0) {", cond.name);
      c->indent++;
      emit_stmt_block_items(c, s->iff.thenb);
      c->indent--;
      emit_line(c, "}");
      if(s->iff.elseb){
        emit_line(c, "else {");
        c->indent++;
        emit_stmt_block_items(c, s->iff.elseb);
        c->indent--;
        emit_line(c, "}");
      }
      return;
    }
    case ST_WHILE: {
      HVal cond = emit_expr(c, s->wh.cond);
      emit_line(c, "while ((%s) != 0) {", cond.name);
      c->indent++;
      emit_stmt_block_items(c, s->wh.body);
      c->indent--;
      emit_line(c, "}");
      return;
    }
    case ST_FOR: {
      emit_line(c, "{");
      c->indent++;
      if(s->fr.init) emit_stmt(c, s->fr.init);
      if(s->fr.cond){
        HVal cond = emit_expr(c, s->fr.cond);
        emit_line(c, "while ((%s) != 0) {", cond.name);
      } else {
        emit_line(c, "while (1) {");
      }
      c->indent++;
      emit_stmt_block_items(c, s->fr.body);
      if(s->fr.step) emit_stmt(c, s->fr.step);
      c->indent--;
      emit_line(c, "}");
      c->indent--;
      emit_line(c, "}");
      return;
    }
    case ST_BREAK:
      emit_line(c, "break;");
      return;
    case ST_CONTINUE:
      emit_line(c, "continue;");
      return;
    case ST_RET:
      emit_line(c, "return;");
      return;
    case ST_SWITCH:
      hlsl_unsupported(s->loc, "switch not supported in HLSL kernel subset");
      return;
    default:
      hlsl_unsupported(s->loc, "unsupported statement in HLSL kernel subset");
      return;
  }
}

static void emit_hlsl_prelude(FILE* f){
  fputs("// Auto-generated by tezzc (HLSL kernel)\n", f);
  fputs("// Restricted kernel subset: no calls other than gpu_tid, no strings, no heap.\n", f);
  fputs("RWByteAddressBuffer g_mem : register(u0);\n\n", f);
  fputs("static uint64_t tn_u64(uint2 v){ return ((uint64_t)v.y << 32) | (uint64_t)v.x; }\n", f);
  fputs("static uint2 tn_u64_to_u2(uint64_t v){ return uint2((uint)(v & 0xffffffffu), (uint)(v >> 32)); }\n", f);
  fputs("static int64_t tn_load_i64(uint64_t addr){ uint2 v = g_mem.Load2((uint)addr); return (int64_t)tn_u64(v); }\n", f);
  fputs("static uint tn_load_u8(uint64_t addr){ uint off = (uint)(addr & ~3ull); uint shift = (uint)(addr & 3ull) * 8u; uint v = g_mem.Load(off); return (v >> shift) & 0xFFu; }\n", f);
  fputs("static void tn_store_i64(uint64_t addr, int64_t v){ g_mem.Store2((uint)addr, tn_u64_to_u2((uint64_t)v)); }\n", f);
  fputs("static void tn_store_u8(uint64_t addr, uint v){ uint off = (uint)(addr & ~3ull); uint shift = (uint)(addr & 3ull) * 8u; uint cur = g_mem.Load(off); cur = (cur & ~(0xFFu << shift)) | ((v & 0xFFu) << shift); g_mem.Store(off, cur); }\n\n", f);
}

static void emit_kernel_fn(HlslCtx* c, FnDecl* fn){
  if(!fn || !fn->is_kernel) return;
  if(fn->arity != 1){
    err_at(fn->loc, "HLSL kernel backend requires fn(*char)->int signature");
  }
  char* fname = mangle_ident(fn->name, fn->len);
  emit_line(c, "[numthreads(1,1,1)]");
  emit_line(c, "void %s(uint3 DTid : SV_DispatchThreadID)", fname);
  emit_line(c, "{");
  c->indent++;
  // map the single kernel arg to base pointer offset 0
  char* pname = mangle_ident(fn->pnames[0], fn->plens[0]);
  emit_line(c, "int64_t %s = 0;", pname);
  emit_stmt_block_items(c, fn->body);
  emit_line(c, "return;");
  c->indent--;
  emit_line(c, "}");
  emit_line(c, "");
}

void hlsl_emit_kernels(ModuleGraph* G, TypeEnv* tenv, Module* entry, const char* out_path){
  if(!G || !tenv || !out_path) return;
  FILE* f = fopen(out_path, "wb");
  if(!f) die("hlsl_codegen: cannot open output");

  emit_hlsl_prelude(f);

  HlslCtx ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.f = f;
  ctx.tenv = tenv;

  int found = 0;
  for(int mi=0; mi<G->mod_n; mi++){
    Module* M = G->mods[mi];
    if(!M) continue;
    for(int fi=0; fi<M->fn_n; fi++){
      FnDecl* fn = M->fns[fi];
      if(fn && fn->is_kernel){
        found = 1;
        emit_kernel_fn(&ctx, fn);
      }
    }
  }

  if(!found){
    err_at2(entry ? entry->path : out_path, entry ? entry->src : "", 1, 1,
            "no kernel fn found for HLSL output");
  }

  fclose(f);
}
