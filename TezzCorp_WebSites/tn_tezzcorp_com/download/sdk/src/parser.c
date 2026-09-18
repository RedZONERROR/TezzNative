#include "parser.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

TokenKind op_tok = TK_EOF;

/* -------------------------------------------------
 * alloc helpers
 * ------------------------------------------------- */
static void* xmalloc(size_t n){ void* p=malloc(n); if(!p) die("out of memory"); return p; }
static void* xrealloc(void* p, size_t n){ void* q=realloc(p,n); if(!q) die("out of memory"); return q; }

static void perr(Parser* P, const char* msg){ err_at(tok_loc(&P->lex), msg); }

static int tok_is(Parser* P, TokenKind k){ return P->lex.cur.kind==k; }
static void eat(Parser* P, TokenKind k, const char* msg){ if(!tok_is(P,k)) perr(P,msg); lex_next(&P->lex); }

static void eat_newlines(Parser* P){
  while(tok_is(P, TK_NEWLINE)) lex_next(&P->lex);
}

static void eat_stmt_end(Parser* P, const char* msg){
  if(tok_is(P, TK_SEMI)){ lex_next(&P->lex); eat_newlines(P); return; }
  if(tok_is(P, TK_NEWLINE)){ eat_newlines(P); return; }
  if(tok_is(P, TK_DEDENT) || tok_is(P, TK_RBRACE) || tok_is(P, TK_EOF)) return;
  perr(P, msg);
}

/* Treat some keyword-tokens as identifiers (builtins) */
static int tok_is_name(Parser* P){
  TokenKind k = P->lex.cur.kind;
  return k==TK_IDENT || k==TK_SAYSTR || k==TK_MALLOC || k==TK_FREE || k==TK_INPUT_LINE || k==TK_INPUT_I64;
}

static int tok_ident_eq(Parser* P, const char* lit){
  Token* t=&P->lex.cur;
  int n=(int)strlen(lit);
  return (t->kind==TK_IDENT && t->len==n && strncmp(t->start,lit,(size_t)n)==0);
}

static int next_is_colon(Parser* P){
  Lexer L = P->lex;
  lex_next(&L);
  return L.cur.kind==TK_COLON;
}

static Type* builtin_type_from_name(TypeEnv* E, const char* s, int n){
  if(!E || !s) return NULL;
  if(n==2 && strncmp(s,"i8",2)==0) return E->b.ty_i8;
  if(n==3 && strncmp(s,"i16",3)==0) return E->b.ty_i16;
  if(n==3 && strncmp(s,"i32",3)==0) return E->b.ty_i32;
  if(n==3 && strncmp(s,"i64",3)==0) return E->b.ty_i64;
  if(n==2 && strncmp(s,"u8",2)==0) return E->b.ty_u8;
  if(n==3 && strncmp(s,"u16",3)==0) return E->b.ty_u16;
  if(n==3 && strncmp(s,"u32",3)==0) return E->b.ty_u32;
  if(n==3 && strncmp(s,"u64",3)==0) return E->b.ty_u64;
  if(n==3 && strncmp(s,"f64",3)==0) return E->b.ty_f64;
  if(n==4 && strncmp(s,"void",4)==0) return E->b.ty_void;
  return NULL;
}

static int is_type_ident_token(Parser* P, Token* t){
  if(t->kind!=TK_IDENT) return 0;
  if(builtin_type_from_name(P->tenv, t->start, t->len)) return 1;
  if(tenv_find_alias(P->tenv, t->start, t->len)) return 1;
  if(tenv_find_struct(P->tenv, t->start, t->len)) return 1;
  return 0;
}

static int parse_type_peek(Parser* P, Lexer* L){
  if(L->cur.kind==TK_FN){
    lex_next(L);
    if(L->cur.kind!=TK_LPAREN) return 0;
    lex_next(L);
    if(L->cur.kind!=TK_RPAREN){
      for(;;){
        if(!parse_type_peek(P, L)) return 0;
        if(L->cur.kind==TK_COMMA){ lex_next(L); continue; }
        break;
      }
    }
    if(L->cur.kind!=TK_RPAREN) return 0;
    lex_next(L);
    if(L->cur.kind==TK_ARROW){
      lex_next(L);
      if(!parse_type_peek(P, L)) return 0;
    }
    return 1;
  }
  if(L->cur.kind==TK_STAR){
    lex_next(L);
    return parse_type_peek(P, L);
  }
  if(L->cur.kind==TK_LBRACKET){
    lex_next(L);
    if(!parse_type_peek(P, L)) return 0;
    if(L->cur.kind!=TK_SEMI) return 0;
    lex_next(L);
    if(L->cur.kind!=TK_NUMBER) return 0;
    lex_next(L);
    if(L->cur.kind!=TK_RBRACKET) return 0;
    lex_next(L);
    return 1;
  }
  if(is_type_ident_token(P, &L->cur)){
    lex_next(L);
    return 1;
  }
  return 0;
}

static int looks_like_c_cast(Parser* P){
  if(!tok_is(P, TK_LPAREN)) return 0;
  Lexer L = P->lex;
  lex_next(&L);
  if(!parse_type_peek(P, &L)) return 0;
  return L.cur.kind==TK_RPAREN;
}

/* Duplicate a C string */
static char* dup_cstr(const char* s){
  if(!s) s="";
  size_t n=strlen(s);
  char* out=(char*)xmalloc(n+1);
  memcpy(out,s,n+1);
  return out;
}

/* -------------------------------------------------
 * AST node alloc
 * ------------------------------------------------- */
static Expr* new_expr(Parser* P, ExprKind k){
  Expr* e=(Expr*)xmalloc(sizeof(Expr));
  memset(e,0,sizeof(*e));
  e->k=k;
  e->loc = tok_loc(&P->lex);
  return e;
}
static Stmt* new_stmt(Parser* P, StmtKind k){
  Stmt* s=(Stmt*)xmalloc(sizeof(Stmt));
  memset(s,0,sizeof(*s));
  s->k=k;
  s->loc = tok_loc(&P->lex);
  return s;
}

static Expr* mk_name_lit(Parser* P, SrcLoc loc, const char* lit){
  (void)P;
  Expr* e=(Expr*)xmalloc(sizeof(Expr));
  memset(e,0,sizeof(*e));
  e->k=EX_NAME;
  e->loc=loc;
  e->name.name=lit;
  e->name.len=(int)strlen(lit);
  return e;
}
static Expr* mk_calln(Parser* P, SrcLoc loc, Expr* callee, Expr** args, int argc){
  Expr* c=new_expr(P,EX_CALL);
  c->loc=loc;
  c->call.callee=callee;
  c->call.args=args;
  c->call.argc=argc;
  return c;
}
static Stmt* mk_stmt_expr(Parser* P, SrcLoc loc, Expr* e){
  Stmt* s=new_stmt(P,ST_EXPR);
  s->loc=loc;
  s->expr.e=e;
  return s;
}

/* -------------------------------------------------
 * Type parsing
 * ------------------------------------------------- */
static Type* parse_type(Parser* P);

static Type* parse_opt_type(Parser* P){
  if(tok_is(P, TK_COLON)){
    eat(P, TK_COLON, "");
    return parse_type(P);
  }
  return P->tenv->b.ty_i64; /* default */
}

static Type* parse_type(Parser* P){
  if(tok_is(P, TK_FN)){
    lex_next(&P->lex);
    eat(P, TK_LPAREN, "expected '(' in fn type");
    Type** params=NULL;
    int pn=0;
    if(!tok_is(P, TK_RPAREN)){
      for(;;){
        Type* pt = parse_type(P);
        params=(Type**)xrealloc(params,sizeof(Type*)*(size_t)(pn+1));
        params[pn++]=pt;
        if(tok_is(P, TK_COMMA)){ lex_next(&P->lex); continue; }
        break;
      }
    }
    eat(P, TK_RPAREN, "expected ')' after fn params");
    Type* ret = P->tenv->b.ty_i64;
    if(tok_is(P, TK_ARROW)){
      lex_next(&P->lex);
      ret = parse_type(P);
    }
    Type* f = type_fn(P->tenv, ret, params, pn);
    return type_ptr(P->tenv, f);
  }
  if(tok_is(P,TK_STAR)){
    lex_next(&P->lex);
    Type* inner=parse_type(P);
    return type_ptr(P->tenv, inner);
  }
  if(tok_is(P,TK_LBRACKET)){
    lex_next(&P->lex);
    Type* inner=parse_type(P);
    eat(P,TK_SEMI,"expected ';' in array type [T;N]");
    if(!tok_is(P,TK_NUMBER)) perr(P,"expected number in array type");
    long long n=P->lex.cur.num;
    lex_next(&P->lex);
    eat(P,TK_RBRACKET,"expected ']' after array type");
    return type_array(P->tenv, inner, n);
  }

  if(tok_is(P,TK_IDENT)){
    Token t=P->lex.cur;
    Type* builtin = builtin_type_from_name(P->tenv, t.start, t.len);
    if(builtin){ lex_next(&P->lex); return builtin; }
  }

  /* struct name type */
  if(tok_is(P,TK_IDENT)){
    Token t=P->lex.cur;
    Type* alias = tenv_find_alias(P->tenv, t.start, t.len);
    if(alias){ lex_next(&P->lex); return alias; }
    Type* st=tenv_find_struct(P->tenv, t.start, t.len);
    if(!st){
      SrcLoc loc = tok_loc(&P->lex);
      st = tenv_add_incomplete_struct(P->tenv, t.start, t.len, loc);
    }
    lex_next(&P->lex);
    return st;
  }

  perr(P,"expected type");
  return P->tenv->b.ty_i64;
}

/* -------------------------------------------------
 * Expressions (precedence)
 * ------------------------------------------------- */
static Expr* parse_expr(Parser* P);
static Expr* parse_lor(Parser* P);
static Expr* parse_land(Parser* P);
static Expr* parse_bor(Parser* P);
static Expr* parse_bxor(Parser* P);
static Expr* parse_band(Parser* P);
static Expr* parse_eq(Parser* P);
static Expr* parse_rel(Parser* P);
static Expr* parse_shift(Parser* P);
static Expr* parse_add(Parser* P);
static Expr* parse_mul(Parser* P);
static Expr* parse_unary(Parser* P);
static Expr* parse_postfix(Parser* P);
static Expr* parse_primary(Parser* P);

static BinOp tok_to_binop(TokenKind k){
  switch(k){
    case TK_PLUS: return OP_ADD;
    case TK_MINUS: return OP_SUB;
    case TK_STAR: return OP_MUL;
    case TK_SLASH: return OP_DIV;
    case TK_PERCENT: return OP_MOD;
    case TK_EQ: return OP_EQ;
    case TK_NEQ: return OP_NEQ;
    case TK_LT: return OP_LT;
    case TK_LTE: return OP_LTE;
    case TK_GT: return OP_GT;
    case TK_GTE: return OP_GTE;
    case TK_LAND: return OP_LAND;
    case TK_LOR: return OP_LOR;
    case TK_BAND: return OP_BAND;
    case TK_BOR: return OP_BOR;
    case TK_BXOR: return OP_BXOR;
    case TK_SHL: return OP_SHL;
    case TK_SHR: return OP_SHR;
    default: return OP_ADD;
  }
}

static Expr* bin(Parser* P, BinOp op, Expr* a, Expr* b){
  Expr* e=new_expr(P,EX_BIN);
  e->bin.op=op; e->bin.a=a; e->bin.b=b;
  e->loc=a->loc;
  return e;
}

static int is_zero_arg_builtin_token(TokenKind k){
  return (k==TK_INPUT_I64 || k==TK_INPUT_LINE);
}

/* Lookahead: is "say( ... ) ;" truly call-style? If not, treat '(' as grouping */
static int say_is_call_style(Parser* P){
  if(P->lex.cur.kind!=TK_LPAREN) return 0;

  Lexer L = P->lex; /* copy lexer */
  int depth=0;

  for(;;){
    TokenKind k = L.cur.kind;
    if(k==TK_EOF) return 0;

    if(k==TK_LPAREN) depth++;
    if(k==TK_RPAREN){
      depth--;
      if(depth==0){
        lex_next(&L); /* token after ')' */
        return (L.cur.kind==TK_SEMI || L.cur.kind==TK_NEWLINE || L.cur.kind==TK_DEDENT || L.cur.kind==TK_RBRACE || L.cur.kind==TK_EOF);
      }
    }
    lex_next(&L);
  }
}

static Expr* parse_primary(Parser* P){
  if(tok_is(P,TK_NUMBER)){
    Expr* e=new_expr(P,EX_NUM);
    e->num=P->lex.cur.num;
    lex_next(&P->lex);
    return e;
  }

  if(tok_is(P,TK_FLOAT)){
    Expr* e=new_expr(P,EX_FLOAT);
    e->f=P->lex.cur.f;
    lex_next(&P->lex);
    return e;
  }

  if(tok_is(P,TK_STRING)){
    Expr* e=new_expr(P,EX_STR);
    /* lexer stores unescaped heap string in Token.str and frees it on next token */
    const char* ts = P->lex.cur.str ? P->lex.cur.str : "";
    e->str = dup_cstr(ts);
    lex_next(&P->lex);
    return e;
  }

  if(tok_is(P,TK_LPAREN)){
    lex_next(&P->lex);
    Expr* e=parse_expr(P);
    eat(P,TK_RPAREN,"expected ')'");
    return e;
  }

  /* array literal: [e1,e2,...] */
  if(tok_is(P,TK_LBRACKET)){
    lex_next(&P->lex);
    Expr* e=new_expr(P,EX_ARRAY_LIT);
    e->arr.items=NULL; e->arr.n=0;

    if(tok_is(P,TK_RBRACKET)){ lex_next(&P->lex); return e; }

    for(;;){
      Expr* it=parse_expr(P);
      e->arr.items=(Expr**)xrealloc(e->arr.items,sizeof(Expr*)*(size_t)(e->arr.n+1));
      e->arr.items[e->arr.n++]=it;
      if(tok_is(P,TK_COMMA)){ lex_next(&P->lex); continue; }
      eat(P,TK_RBRACKET,"expected ']' or ',' in array literal");
      break;
    }
    return e;
  }

  /* names (idents + builtin tokens) */
  if(tok_is_name(P)){
    Token t=P->lex.cur;
    SrcLoc loc = tok_loc(&P->lex);

    Expr* e=new_expr(P,EX_NAME);
    e->loc=loc;
    e->name.name=t.start;
    e->name.len=t.len;

    TokenKind kind_before = t.kind;
    lex_next(&P->lex);

    /* ---- Zero-arg builtin sugar: input_i64; input_line; -> auto call */
    if(is_zero_arg_builtin_token(kind_before)){
      Expr* callee = e;
      Expr* call = new_expr(P, EX_CALL);
      call->loc = loc;
      call->call.callee = callee;
      call->call.args = NULL;
      call->call.argc = 0;
      return call;
    }

    /* ---- Struct literal ONLY if identifier is a known struct type */
    if(tok_is(P,TK_LBRACE)){
      /* Only allow TypeName{...} if TypeName exists as a struct */
      if(kind_before==TK_IDENT){
        Type* maybe = tenv_find_struct(P->tenv, e->name.name, e->name.len);
        if(maybe){
          Expr* st=new_expr(P,EX_STRUCT_LIT);
          st->loc = loc;
          st->stlit.tname = e->name.name;
          st->stlit.tlen  = e->name.len;
          st->stlit.f=NULL; st->stlit.flen=NULL; st->stlit.v=NULL; st->stlit.n=0;

          lex_next(&P->lex); /* consume '{' */
          if(tok_is(P,TK_RBRACE)){ lex_next(&P->lex); free(e); return st; }

          for(;;){
            if(!tok_is(P,TK_IDENT)) perr(P,"expected field name in struct literal");
            Token fn=P->lex.cur;
            lex_next(&P->lex);
            eat(P,TK_COLON,"expected ':' in struct literal");
            Expr* val=parse_expr(P);

            int n=st->stlit.n;
            st->stlit.f=(const char**)xrealloc(st->stlit.f,sizeof(const char*)*(size_t)(n+1));
            st->stlit.flen=(int*)xrealloc(st->stlit.flen,sizeof(int)*(size_t)(n+1));
            st->stlit.v=(Expr**)xrealloc(st->stlit.v,sizeof(Expr*)*(size_t)(n+1));
            st->stlit.f[n]=fn.start;
            st->stlit.flen[n]=fn.len;
            st->stlit.v[n]=val;
            st->stlit.n++;

            if(tok_is(P,TK_COMMA)){ lex_next(&P->lex); continue; }
            eat(P,TK_RBRACE,"expected '}' or ',' in struct literal");
            break;
          }
          free(e);
          return st;
        }
      }
      /* else: not a struct type; leave it as a normal name expression */
    }

    return e;
  }

  perr(P,"expected expression");
  return new_expr(P,EX_NUM);
}

static Expr* parse_postfix(Parser* P){
  Expr* e=parse_primary(P);

  for(;;){
    /* call */
    if(tok_is(P,TK_LPAREN)){
      lex_next(&P->lex);
      Expr* call=new_expr(P,EX_CALL);
      call->call.callee=e;
      call->call.args=NULL;
      call->call.argc=0;

      if(tok_is(P,TK_RPAREN)){
        lex_next(&P->lex);
        e=call;
        continue;
      }

      for(;;){
        Expr* a=parse_expr(P);
        int n=call->call.argc;
        call->call.args=(Expr**)xrealloc(call->call.args,sizeof(Expr*)*(size_t)(n+1));
        call->call.args[n]=a;
        call->call.argc=n+1;

        if(tok_is(P,TK_COMMA)){ lex_next(&P->lex); continue; }
        eat(P,TK_RPAREN,"expected ')' or ',' in call");
        break;
      }
      e=call;
      continue;
    }

    /* index */
    if(tok_is(P,TK_LBRACKET)){
      lex_next(&P->lex);
      Expr* idx=parse_expr(P);
      eat(P,TK_RBRACKET,"expected ']'");
      Expr* ex=new_expr(P,EX_INDEX);
      ex->index.base=e;
      ex->index.idx=idx;
      e=ex;
      continue;
    }

    /* dot */
    if(tok_is(P,TK_DOT)){
      lex_next(&P->lex);
      if(!tok_is_name(P)) perr(P,"expected name after '.'");
      Token m=P->lex.cur;
      lex_next(&P->lex);
      Expr* d=new_expr(P,EX_DOT);
      d->dot.base=e;
      d->dot.mem=m.start;
      d->dot.memlen=m.len;
      e=d;
      continue;
    }

    if(tok_is(P, TK_AS)){
      lex_next(&P->lex);
      Type* to = parse_type(P);
      Expr* c=new_expr(P,EX_CAST);
      c->cast.e = e;
      c->cast.to = to;
      e = c;
      continue;
    }

    break;
  }

  return e;
}

static Expr* parse_unary(Parser* P){
  if(tok_is(P, TK_SIZEOF) || tok_is(P, TK_ALIGNOF)){
    int is_align = tok_is(P, TK_ALIGNOF);
    lex_next(&P->lex);
    Expr* e=new_expr(P, is_align?EX_ALIGNOF:EX_SIZEOF);
    e->siz.e = NULL;
    e->siz.ty = NULL;
    e->siz.is_type = 0;

    if(tok_is(P, TK_LPAREN) && looks_like_c_cast(P)){
      lex_next(&P->lex);
      e->siz.ty = parse_type(P);
      e->siz.is_type = 1;
      eat(P, TK_RPAREN, "expected ')' after type");
      return e;
    }

    e->siz.e = parse_unary(P);
    e->siz.is_type = 0;
    return e;
  }
  if(tok_is(P, TK_LPAREN) && looks_like_c_cast(P)){
    lex_next(&P->lex); /* '(' */
    Type* to = parse_type(P);
    eat(P, TK_RPAREN, "expected ')' after cast type");
    Expr* e=parse_unary(P);
    Expr* c=new_expr(P,EX_CAST);
    c->cast.e = e;
    c->cast.to = to;
    return c;
  }
  /* prefix builtins as unary-call sugar:
     malloc 64   -> malloc(64)
     free h      -> free(h)
  */
  if(tok_is(P, TK_MALLOC) || tok_is(P, TK_FREE)){
    SrcLoc loc = tok_loc(&P->lex);
    Token t = P->lex.cur;
    lex_next(&P->lex);

    Expr* callee=new_expr(P, EX_NAME);
    callee->loc = loc;
    callee->name.name = t.start;
    callee->name.len  = t.len;

    Expr* a0 = parse_unary(P); /* unary precedence */
    Expr** args = (Expr**)xmalloc(sizeof(Expr*));
    args[0]=a0;

    return mk_calln(P, loc, callee, args, 1);
  }

  if(tok_is(P,TK_PLUS)){ lex_next(&P->lex); Expr* e=parse_unary(P); Expr* u=new_expr(P,EX_UN); u->un.op=UN_POS; u->un.e=e; return u; }
  if(tok_is(P,TK_MINUS)){ lex_next(&P->lex); Expr* e=parse_unary(P); Expr* u=new_expr(P,EX_UN); u->un.op=UN_NEG; u->un.e=e; return u; }
  if(tok_is(P,TK_LNOT)){ lex_next(&P->lex); Expr* e=parse_unary(P); Expr* u=new_expr(P,EX_UN); u->un.op=UN_LNOT; u->un.e=e; return u; }
  if(tok_is(P,TK_BNOT)){ lex_next(&P->lex); Expr* e=parse_unary(P); Expr* u=new_expr(P,EX_UN); u->un.op=UN_BNOT; u->un.e=e; return u; }

  /* &lvalue */
  if(tok_is(P,TK_BAND)){
    lex_next(&P->lex);
    Expr* e=parse_unary(P);
    Expr* a=new_expr(P,EX_ADDR);
    a->un.e=e;
    return a;
  }
  /* *expr */
  if(tok_is(P,TK_STAR)){
    lex_next(&P->lex);
    Expr* e=parse_unary(P);
    Expr* d=new_expr(P,EX_DEREF);
    d->un.e=e;
    return d;
  }

  return parse_postfix(P);
}

static Expr* parse_mul(Parser* P){
  Expr* e=parse_unary(P);
  while(tok_is(P,TK_STAR)||tok_is(P,TK_SLASH)||tok_is(P,TK_PERCENT)){
    TokenKind op=P->lex.cur.kind; lex_next(&P->lex);
    Expr* r=parse_unary(P);
    e=bin(P,tok_to_binop(op),e,r);
  }
  return e;
}
static Expr* parse_add(Parser* P){
  Expr* e=parse_mul(P);
  while(tok_is(P,TK_PLUS)||tok_is(P,TK_MINUS)){
    TokenKind op=P->lex.cur.kind; lex_next(&P->lex);
    Expr* r=parse_mul(P);
    e=bin(P,tok_to_binop(op),e,r);
  }
  return e;
}
static Expr* parse_shift(Parser* P){
  Expr* e=parse_add(P);
  while(tok_is(P,TK_SHL)||tok_is(P,TK_SHR)){
    TokenKind op=P->lex.cur.kind; lex_next(&P->lex);
    Expr* r=parse_add(P);
    e=bin(P,tok_to_binop(op),e,r);
  }
  return e;
}
static Expr* parse_rel(Parser* P){
  Expr* e=parse_shift(P);
  while(tok_is(P,TK_LT)||tok_is(P,TK_LTE)||tok_is(P,TK_GT)||tok_is(P,TK_GTE)){
    TokenKind op=P->lex.cur.kind; lex_next(&P->lex);
    Expr* r=parse_shift(P);
    e=bin(P,tok_to_binop(op),e,r);
  }
  return e;
}
static Expr* parse_eq(Parser* P){
  Expr* e=parse_rel(P);
  while(tok_is(P,TK_EQ)||tok_is(P,TK_NEQ)){
    TokenKind op=P->lex.cur.kind; lex_next(&P->lex);
    Expr* r=parse_rel(P);
    e=bin(P,tok_to_binop(op),e,r);
  }
  return e;
}
static Expr* parse_band(Parser* P){
  Expr* e=parse_eq(P);
  while(tok_is(P,TK_BAND)){ lex_next(&P->lex); Expr* r=parse_eq(P); e=bin(P,OP_BAND,e,r); }
  return e;
}
static Expr* parse_bxor(Parser* P){
  Expr* e=parse_band(P);
  while(tok_is(P,TK_BXOR)){ lex_next(&P->lex); Expr* r=parse_band(P); e=bin(P,OP_BXOR,e,r); }
  return e;
}
static Expr* parse_bor(Parser* P){
  Expr* e=parse_bxor(P);
  while(tok_is(P,TK_BOR)){ lex_next(&P->lex); Expr* r=parse_bxor(P); e=bin(P,OP_BOR,e,r); }
  return e;
}
static Expr* parse_land(Parser* P){
  Expr* e=parse_bor(P);
  while(tok_is(P,TK_LAND)){ lex_next(&P->lex); Expr* r=parse_bor(P); e=bin(P,OP_LAND,e,r); }
  return e;
}
static Expr* parse_lor(Parser* P){
  Expr* e=parse_land(P);
  while(tok_is(P,TK_LOR)){ lex_next(&P->lex); Expr* r=parse_land(P); e=bin(P,OP_LOR,e,r); }
  return e;
}

static Expr* parse_expr(Parser* P){
  return parse_lor(P);
}

/* -------------------------------------------------
 * Statements
 * ------------------------------------------------- */
static Stmt* parse_stmt(Parser* P);
static Stmt* parse_block(Parser* P);

static Stmt* parse_block_after_colon(Parser* P){
  Stmt* b=new_stmt(P,ST_BLOCK);
  b->block.items=NULL; b->block.n=0;

  eat(P, TK_COLON, "expected ':'");
  if(tok_is(P, TK_NEWLINE)){
    eat_newlines(P);
    eat(P, TK_INDENT, "expected indent");

    while(!tok_is(P,TK_DEDENT) && !tok_is(P,TK_EOF)){
      if(tok_is(P, TK_NEWLINE)){ eat_newlines(P); continue; }
      if(tok_is(P, TK_INDENT)){ lex_next(&P->lex); continue; }
      Stmt* s=parse_stmt(P);
      if(!s) continue;
      b->block.items=(Stmt**)xrealloc(b->block.items,sizeof(Stmt*)*(size_t)(b->block.n+1));
      b->block.items[b->block.n++]=s;
    }
    eat(P,TK_DEDENT,"expected dedent");
  } else {
    Stmt* s=parse_stmt(P);
    if(s){
      b->block.items=(Stmt**)xrealloc(b->block.items,sizeof(Stmt*)*(size_t)(b->block.n+1));
      b->block.items[b->block.n++]=s;
    }
  }
  return b;
}

static Stmt* parse_block(Parser* P){
  if(tok_is(P, TK_COLON)) return parse_block_after_colon(P);
  Stmt* b=new_stmt(P,ST_BLOCK);
  b->block.items=NULL; b->block.n=0;
  eat(P,TK_LBRACE,"expected '{'");
  while(!tok_is(P,TK_RBRACE) && !tok_is(P,TK_EOF)){
    if(tok_is(P, TK_NEWLINE) || tok_is(P, TK_INDENT) || tok_is(P, TK_DEDENT)){
      lex_next(&P->lex);
      continue;
    }
    Stmt* s=parse_stmt(P);
    if(!s) continue;
    b->block.items=(Stmt**)xrealloc(b->block.items,sizeof(Stmt*)*(size_t)(b->block.n+1));
    b->block.items[b->block.n++]=s;
  }
  eat(P,TK_RBRACE,"expected '}'");
  return b;
}

static Stmt* parse_let(Parser* P){
  Stmt* s=new_stmt(P,ST_LET);
  eat(P,TK_LET,"");

  if(!tok_is_name(P)) perr(P,"expected name after let");
  Token n=P->lex.cur; lex_next(&P->lex);

  s->let.name=n.start; s->let.len=n.len;
  s->let.init=NULL;
  s->let.is_const=0;

  /* allow: let x = ...; (default i64) OR let x:T = ...; */
  s->let.ty = parse_opt_type(P);

  if(tok_is(P,TK_ASSIGN)){
    lex_next(&P->lex);
    s->let.init=parse_expr(P);
  }

  eat_stmt_end(P,"expected end of line after let");
  return s;
}

static int is_asg_op(TokenKind k){
  return k==TK_ASSIGN||k==TK_PLUSEQ||k==TK_MINUSEQ||k==TK_STAREQ||k==TK_SLASHEQ||k==TK_PERCENTEQ||
         k==TK_ANDEQ||k==TK_OREQ||k==TK_XOREQ||k==TK_SHLEQ||k==TK_SHREQ||
         k==TK_PLUSPLUS||k==TK_MINUSMINUS;
}

static Stmt* parse_ret(Parser* P){
  Stmt* s=new_stmt(P,ST_RET);
  eat(P,TK_RET,"");

  if(tok_is(P, TK_SEMI)){
    s->ret.e = NULL;
    lex_next(&P->lex);
    return s;
  }
  if(tok_is(P, TK_NEWLINE) || tok_is(P, TK_DEDENT) || tok_is(P, TK_RBRACE) || tok_is(P, TK_EOF)){
    s->ret.e = NULL;
    eat_stmt_end(P,"expected end of line after ret");
    return s;
  }

  s->ret.e=parse_expr(P);
  eat_stmt_end(P,"expected end of line after ret");
  return s;
}

/* else-if support */
static Stmt* parse_if(Parser* P){
  Stmt* s=new_stmt(P,ST_IF);
  eat(P,TK_IF,"");
  s->iff.cond=parse_expr(P);
  s->iff.thenb=parse_block(P);
  s->iff.elseb=NULL;

  if(tok_is(P,TK_ELSE)){
    lex_next(&P->lex);
    if(tok_is(P,TK_IF)){
      s->iff.elseb = parse_if(P); /* else if ... */
    } else {
      s->iff.elseb = parse_block(P); /* else { ... } */
    }
  }
  return s;
}

static Stmt* parse_while(Parser* P){
  Stmt* s=new_stmt(P,ST_WHILE);
  eat(P,TK_WHILE,"");
  s->wh.cond=parse_expr(P);
  s->wh.body=parse_block(P);
  return s;
}

static Stmt* parse_switch(Parser* P){
  Stmt* s=new_stmt(P,ST_SWITCH);
  eat(P, TK_SWITCH, "");
  s->sw.cond = parse_expr(P);
  s->sw.cases = NULL;
  s->sw.case_n = 0;

  if(tok_is(P, TK_COLON)){
    eat(P, TK_COLON, "");
    if(!tok_is(P, TK_NEWLINE)) perr(P,"expected newline after ':'");
    eat_newlines(P);
    eat(P, TK_INDENT, "expected indent");

    while(!tok_is(P, TK_DEDENT) && !tok_is(P, TK_EOF)){
      if(tok_is(P, TK_NEWLINE)){ eat_newlines(P); continue; }

      SwitchCase c; memset(&c,0,sizeof(c));
      if(tok_is(P, TK_CASE)){
        lex_next(&P->lex);
        c.val = parse_expr(P);
      } else if(tok_is(P, TK_DEFAULT)){
        lex_next(&P->lex);
        c.val = NULL;
      } else {
        perr(P,"expected case/default in switch");
      }

      if(tok_is(P, TK_COLON)){
        c.body = parse_block_after_colon(P);
      } else if(tok_is(P, TK_LBRACE)){
        c.body = parse_block(P);
      } else {
        perr(P,"expected ':' or '{' after case/default");
      }

      s->sw.cases=(SwitchCase*)xrealloc(s->sw.cases,sizeof(SwitchCase)*(size_t)(s->sw.case_n+1));
      s->sw.cases[s->sw.case_n++] = c;
    }
    eat(P, TK_DEDENT, "expected dedent after switch");
    return s;
  }

  if(tok_is(P, TK_LBRACE)){
    eat(P, TK_LBRACE, "");
    while(!tok_is(P, TK_RBRACE) && !tok_is(P, TK_EOF)){
      if(tok_is(P, TK_NEWLINE)){ eat_newlines(P); continue; }
      SwitchCase c; memset(&c,0,sizeof(c));
      if(tok_is(P, TK_CASE)){
        lex_next(&P->lex);
        c.val = parse_expr(P);
      } else if(tok_is(P, TK_DEFAULT)){
        lex_next(&P->lex);
        c.val = NULL;
      } else {
        perr(P,"expected case/default in switch");
      }
      if(tok_is(P, TK_COLON)){
        c.body = parse_block_after_colon(P);
      } else if(tok_is(P, TK_LBRACE)){
        c.body = parse_block(P);
      } else {
        perr(P,"expected ':' or '{' after case/default");
      }
      s->sw.cases=(SwitchCase*)xrealloc(s->sw.cases,sizeof(SwitchCase)*(size_t)(s->sw.case_n+1));
      s->sw.cases[s->sw.case_n++] = c;
    }
    eat(P, TK_RBRACE, "expected '}' after switch");
    return s;
  }

  perr(P,"expected ':' or '{' after switch expression");
  return s;
}

/* python-like say:
   say;
   say a;
   say a, b, c;
   say(a,b,c);
   say (5+6)*2;  (grouping)
*/
static Stmt* parse_say(Parser* P){
  SrcLoc loc = tok_loc(&P->lex);
  eat(P, TK_SAY, "");

  Expr** args=NULL;
  int argc=0;

  /* call-style: say( a, b, c ); only if it ends with ");" */
  if(tok_is(P, TK_LPAREN) && say_is_call_style(P)){
    lex_next(&P->lex); /* consume '(' */

      if(tok_is(P, TK_RPAREN)){
        lex_next(&P->lex);
        eat_stmt_end(P, "expected end of line after say()");
      } else {
        for(;;){
          Expr* e=parse_expr(P);
          args=(Expr**)xrealloc(args,sizeof(Expr*)*(size_t)(argc+1));
          args[argc++]=e;

          if(tok_is(P, TK_COMMA)){ lex_next(&P->lex); continue; }
          eat(P, TK_RPAREN, "expected ')' after say(...");
          eat_stmt_end(P, "expected end of line after say(...)");
          break;
        }
      }
    }
    /* expression-style: say expr;  OR say (5+6)*2; OR say a,b,c; OR say; */
    else {
    if(tok_is(P, TK_SEMI) || tok_is(P, TK_NEWLINE) || tok_is(P, TK_DEDENT) || tok_is(P, TK_RBRACE) || tok_is(P, TK_EOF)){
      eat_stmt_end(P, "expected end of line after say");
    } else {
      for(;;){
        Expr* e=parse_expr(P);
        args=(Expr**)xrealloc(args,sizeof(Expr*)*(size_t)(argc+1));
        args[argc++]=e;

        if(tok_is(P, TK_COMMA)){ lex_next(&P->lex); continue; }
        eat_stmt_end(P, "expected end of line after say");
        break;
      }
    }
  }

  Expr* callee = mk_name_lit(P, loc, "say");
  Expr* call   = mk_calln(P, loc, callee, args, argc);
  return mk_stmt_expr(P, loc, call);
}

/* say_str sugar as statement:
   say_str expr;
   say_str(expr);
*/
static Stmt* parse_say_str(Parser* P){
  SrcLoc loc = tok_loc(&P->lex);
  eat(P, TK_SAYSTR, "");

  Expr* arg=NULL;

  if(tok_is(P, TK_LPAREN)){
    lex_next(&P->lex);
    arg = parse_expr(P);
    eat(P, TK_RPAREN, "expected ')' after say_str(");
    eat_stmt_end(P, "expected end of line after say_str(...)");
  } else {
    arg = parse_expr(P);
    eat_stmt_end(P, "expected end of line after say_str");
  }

  Expr** args=(Expr**)xmalloc(sizeof(Expr*));
  args[0]=arg;

  Expr* callee = mk_name_lit(P, loc, "say_str");
  Expr* call   = mk_calln(P, loc, callee, args, 1);
  return mk_stmt_expr(P, loc, call);
}

static Stmt* parse_for(Parser* P){
  Stmt* s=new_stmt(P,ST_FOR);
  eat(P,TK_FOR,"");

  /* init part: until ';' */
  if(tok_is(P,TK_SEMI)){
    s->fr.init = NULL;
    lex_next(&P->lex);
  } else {
    if(tok_is(P,TK_LET)){
      s->fr.init = parse_let(P); /* consumes ';' */
    } else if(tok_is(P, TK_IDENT) && next_is_colon(P)){
      Stmt* d=new_stmt(P,ST_LET);
      Token n=P->lex.cur; lex_next(&P->lex);
      d->let.name=n.start; d->let.len=n.len;
      d->let.init=NULL;
      d->let.ty = parse_opt_type(P);
      if(tok_is(P, TK_ASSIGN)){
        lex_next(&P->lex);
        d->let.init=parse_expr(P);
      }
      eat(P,TK_SEMI,"expected ';' after for-init");
      s->fr.init=d;
    } else {
      Expr* lhs=parse_expr(P);

      if(tok_is(P,TK_PLUSPLUS)||tok_is(P,TK_MINUSMINUS)){
        Stmt* a=new_stmt(P,ST_ASSIGN);
        a->asg.lhs=lhs;
        a->asg.op=P->lex.cur.kind;
        a->asg.rhs=NULL;
        lex_next(&P->lex);
        eat(P,TK_SEMI,"expected ';' after for-init");
        s->fr.init=a;
      } else if(is_asg_op(P->lex.cur.kind)){
        TokenKind op=P->lex.cur.kind;
        lex_next(&P->lex);
        Expr* rhs=parse_expr(P);
        Stmt* a=new_stmt(P,ST_ASSIGN);
        a->asg.lhs=lhs; a->asg.op=op; a->asg.rhs=rhs;
        eat(P,TK_SEMI,"expected ';' after for-init");
        s->fr.init=a;
      } else {
        Stmt* e=new_stmt(P,ST_EXPR);
        e->expr.e=lhs;
        eat(P,TK_SEMI,"expected ';' after for-init");
        s->fr.init=e;
      }
    }
  }

  /* condition part: until ';' */
  if(tok_is(P,TK_SEMI)){
    s->fr.cond = NULL;
    lex_next(&P->lex);
  } else {
    s->fr.cond = parse_expr(P);
    eat(P,TK_SEMI,"expected ';' after for-condition");
  }

  /* step part: until block */
  if(tok_is(P,TK_LBRACE) || tok_is(P, TK_COLON)){
    s->fr.step = NULL;
  } else {
    Expr* lhs=parse_expr(P);

    if(tok_is(P,TK_PLUSPLUS)||tok_is(P,TK_MINUSMINUS)){
      Stmt* a=new_stmt(P,ST_ASSIGN);
      a->asg.lhs=lhs;
      a->asg.op=P->lex.cur.kind;
      a->asg.rhs=NULL;
      lex_next(&P->lex);
      s->fr.step=a;
    } else if(is_asg_op(P->lex.cur.kind)){
      TokenKind op=P->lex.cur.kind;
      lex_next(&P->lex);
      Expr* rhs=parse_expr(P);
      Stmt* a=new_stmt(P,ST_ASSIGN);
      a->asg.lhs=lhs; a->asg.op=op; a->asg.rhs=rhs;
      s->fr.step=a;
    } else {
      Stmt* e=new_stmt(P,ST_EXPR);
      e->expr.e=lhs;
      s->fr.step=e;
    }
  }

  s->fr.body = parse_block(P);
  return s;
}

static Stmt* parse_stmt(Parser* P){
  while(tok_is(P, TK_NEWLINE) || tok_is(P, TK_INDENT)){
    lex_next(&P->lex);
  }
  if(tok_is(P, TK_DEDENT) || tok_is(P, TK_EOF)) return NULL;

  if(tok_is(P, TK_CONST)){
    lex_next(&P->lex);
    if(!tok_is(P, TK_LET)) perr(P,"const must be followed by let");
    Stmt* s=parse_let(P);
    s->let.is_const=1;
    return s;
  }
  if(tok_is(P, TK_UNSAFE)){
    lex_next(&P->lex);
    if(tok_is(P, TK_FN)) perr(P, "unsafe fn only allowed at top level");
    Stmt* body = parse_block(P);
    Stmt* s = new_stmt(P, ST_UNSAFE);
    s->uns.body = body;
    return s;
  }
  if(tok_is(P, TK_STATIC) || tok_is(P, TK_EXTERN)){
    perr(P,"static/extern only allowed at top level");
  }

  if(tok_is(P,TK_SAY))    return parse_say(P);
  if(tok_is(P,TK_SAYSTR)) return parse_say_str(P);

  if(tok_is(P,TK_LBRACE)) return parse_block(P);
  if(tok_is(P,TK_LET)) return parse_let(P);
  if(tok_is(P,TK_RET)) return parse_ret(P);
  if(tok_is(P,TK_IF)) return parse_if(P);
  if(tok_is(P,TK_FOR)) return parse_for(P);
  if(tok_is(P,TK_WHILE)) return parse_while(P);
  if(tok_is(P,TK_SWITCH)) return parse_switch(P);

  if(tok_is(P, TK_IDENT) && next_is_colon(P)){
    Stmt* s=new_stmt(P,ST_LET);
    Token n=P->lex.cur; lex_next(&P->lex);
    s->let.name=n.start; s->let.len=n.len;
    s->let.init=NULL;
    s->let.ty = parse_opt_type(P);
    if(tok_is(P, TK_ASSIGN)){
      lex_next(&P->lex);
      s->let.init=parse_expr(P);
    }
    eat_stmt_end(P,"expected end of line after declaration");
    return s;
  }

  if(tok_is(P,TK_BREAK)){
    Stmt* s=new_stmt(P,ST_BREAK);
    lex_next(&P->lex);
    eat_stmt_end(P,"expected end of line after break");
    return s;
  }
  if(tok_is(P,TK_FALLTHROUGH)){
    Stmt* s=new_stmt(P,ST_FALLTHROUGH);
    lex_next(&P->lex);
    eat_stmt_end(P,"expected end of line after fallthrough");
    return s;
  }
  if(tok_is(P,TK_CONTINUE)){
    Stmt* s=new_stmt(P,ST_CONTINUE);
    lex_next(&P->lex);
    eat_stmt_end(P,"expected end of line after continue");
    return s;
  }

  /* assignment or expression statement */
  Expr* lhs=parse_expr(P);

  if(tok_is(P,TK_PLUSPLUS)||tok_is(P,TK_MINUSMINUS)){
    Stmt* s=new_stmt(P,ST_ASSIGN);
    s->asg.lhs=lhs;
    s->asg.op=P->lex.cur.kind;
    s->asg.rhs=NULL;
    lex_next(&P->lex);
    eat_stmt_end(P,"expected end of line");
    return s;
  }

  if(is_asg_op(P->lex.cur.kind)){
    TokenKind op=P->lex.cur.kind;
    lex_next(&P->lex);
    Expr* rhs=parse_expr(P);
    Stmt* s=new_stmt(P,ST_ASSIGN);
    s->asg.lhs=lhs; s->asg.op=op; s->asg.rhs=rhs;
    eat_stmt_end(P,"expected end of line");
    return s;
  }

  Stmt* s=new_stmt(P,ST_EXPR);
  s->expr.e=lhs;
  eat_stmt_end(P,"expected end of line");
  return s;
}

/* -------------------------------------------------
 * Top-level
 * ------------------------------------------------- */
static void module_name_from_path(Module* M){
  const char* p=M->path;
  const char* rel = p;
  if(strncmp(p, "lib/", 4)==0 || strncmp(p, "lib\\", 4)==0){
    rel = p + 4;
  } else if(strncmp(p, "./lib/", 6)==0 || strncmp(p, ".\\lib\\", 6)==0){
    rel = p + 6;
  }
  for(const char* q=p; q && *q; q++){
    if((q[0]=='/' || q[0]=='\\') && q[1]=='l' && q[2]=='i' && q[3]=='b' &&
       (q[4]=='/' || q[4]=='\\')){
      rel = q + 5;
    }
  }
  const char* end = rel + strlen(rel);
  if(end - rel >= 3 && end[-3]=='.' && end[-2]=='t' && end[-1]=='n'){
    end -= 3;
  } else {
    for(const char* q=rel; *q; q++){
      if(*q=='.'){ end = q; break; }
    }
  }
  int len = (int)(end - rel);
  if(len < 0) len = 0;
  char* name = (char*)xmalloc((size_t)len + 1);
  for(int i=0;i<len;i++){
    char c = rel[i];
    if(c=='/' || c=='\\') c='.';
    name[i] = c;
  }
  name[len] = 0;
  M->mname = name;
  M->mlen = len;
}

static void parse_import(Parser* P, Module* M){
  eat(P,TK_IMPORT,"");
  if(!tok_is(P,TK_STRING)) perr(P,"expected string after import");

  /* use lexer’s unescaped string */
  const char* ts = P->lex.cur.str ? P->lex.cur.str : "";
  char* s = dup_cstr(ts);

  lex_next(&P->lex);
  eat_stmt_end(P,"expected end of line after import");

  M->imports=(const char**)xrealloc(M->imports,sizeof(char*)*(size_t)(M->import_n+1));
  M->imports[M->import_n++]=s;
}

static void module_add_enum_const(Module* M, const char* name, int len, long long val, SrcLoc loc){
  for(int i=0;i<M->econst_n;i++){
    if(M->econsts[i].len==len && strncmp(M->econsts[i].name,name,(size_t)len)==0){
      err_at(loc, "duplicate enum constant");
    }
  }
  M->econsts=(EnumConst*)xrealloc(M->econsts,sizeof(*M->econsts)*(size_t)(M->econst_n+1));
  M->econsts[M->econst_n].name=name;
  M->econsts[M->econst_n].len=len;
  M->econsts[M->econst_n].val=val;
  M->econst_n++;
}

static void module_add_typedef(Module* M, TypedefDecl* td){
  M->typedefs=(TypedefDecl**)xrealloc(M->typedefs,sizeof(TypedefDecl*)*(size_t)(M->typedef_n+1));
  M->typedefs[M->typedef_n++] = td;
}

static void module_add_enum(Module* M, EnumDecl* ed){
  M->enums=(EnumDecl**)xrealloc(M->enums,sizeof(EnumDecl*)*(size_t)(M->enum_n+1));
  M->enums[M->enum_n++] = ed;
}

typedef struct {
  Attr* attrs;
  int n;
} AttrList;

static AttrList parse_attrs(Parser* P){
  AttrList L; memset(&L,0,sizeof(L));
  while(tok_is(P, TK_AT)){
    lex_next(&P->lex);
    if(!tok_is_name(P)) perr(P, "expected attribute name after '@'");
    Token nm = P->lex.cur;
    lex_next(&P->lex);
    const char* arg = NULL;
    int arg_len = 0;
    if(tok_is(P, TK_LPAREN)){
      lex_next(&P->lex);
      if(!tok_is(P, TK_STRING)) perr(P, "expected string attribute argument");
      const char* ts = P->lex.cur.str ? P->lex.cur.str : "";
      char* s = dup_cstr(ts);
      arg = s;
      arg_len = (int)strlen(s);
      lex_next(&P->lex);
      eat(P, TK_RPAREN, "expected ')' after attribute argument");
    }
    L.attrs = (Attr*)xrealloc(L.attrs, sizeof(Attr)*(size_t)(L.n + 1));
    L.attrs[L.n++] = (Attr){ nm.start, nm.len, arg, arg_len };
    eat_stmt_end(P, "expected end of line after attribute");
  }
  return L;
}

static void parse_global_var(Parser* P, Module* M, int is_extern, int is_static, AttrList attrs){
  SrcLoc loc = tok_loc(&P->lex);
  if(!tok_is(P, TK_LET)) perr(P,"expected let after storage qualifier");
  lex_next(&P->lex);

  if(!tok_is_name(P)) perr(P,"expected name after let");
  Token n=P->lex.cur; lex_next(&P->lex);

  Type* ty = parse_opt_type(P);
  Expr* init = NULL;
  if(tok_is(P, TK_ASSIGN)){
    lex_next(&P->lex);
    init = parse_expr(P);
  }
  eat_stmt_end(P,"expected end of line after global");

  M->globals=(struct GlobalVar*)xrealloc(M->globals,sizeof(*M->globals)*(size_t)(M->global_n+1));
  M->globals[M->global_n].name=n.start;
  M->globals[M->global_n].len=n.len;
  M->globals[M->global_n].ty=ty;
  M->globals[M->global_n].init=init;
  M->globals[M->global_n].is_extern=is_extern;
  M->globals[M->global_n].is_static=is_static;
  M->globals[M->global_n].attrs=attrs.attrs;
  M->globals[M->global_n].attr_n=attrs.n;
  M->global_n++;

  if(is_extern && init) err_at(loc, "extern global cannot have initializer");
}

static long long parse_enum_value(Parser* P){
  if(tok_is(P, TK_MINUS)){
    lex_next(&P->lex);
    if(!tok_is(P, TK_NUMBER)) perr(P,"expected number after '-' in enum value");
    long long v = -P->lex.cur.num;
    lex_next(&P->lex);
    return v;
  }
  if(!tok_is(P, TK_NUMBER)) perr(P,"expected number in enum value");
  long long v = P->lex.cur.num;
  lex_next(&P->lex);
  return v;
}

static void parse_enum(Parser* P, Module* M, AttrList attrs){
  SrcLoc loc=tok_loc(&P->lex);
  eat(P, TK_ENUM, "");

  EnumDecl* ed=(EnumDecl*)xmalloc(sizeof(EnumDecl));
  memset(ed,0,sizeof(*ed));
  ed->loc = loc;
  ed->attrs = attrs.attrs;
  ed->attr_n = attrs.n;

  if(tok_is(P, TK_IDENT)){
    Token nm = P->lex.cur;
    lex_next(&P->lex);
    ed->name = nm.start;
    ed->len = nm.len;
    tenv_add_alias(P->tenv, nm.start, nm.len, P->tenv->b.ty_i64, loc);
  }

  long long next_val = 0;
  EnumItem* items = NULL;
  int item_n = 0;

  if(tok_is(P, TK_COLON)){
    ed->is_brace = 0;
    eat(P, TK_COLON, "");
    if(!tok_is(P, TK_NEWLINE)) perr(P,"expected newline after ':'");
    eat_newlines(P);
    eat(P, TK_INDENT, "expected indent");

    while(!tok_is(P, TK_DEDENT) && !tok_is(P, TK_EOF)){
      if(tok_is(P, TK_NEWLINE)){ eat_newlines(P); continue; }
      if(!tok_is(P, TK_IDENT)) perr(P,"expected enum name");
      SrcLoc iloc = tok_loc(&P->lex);
      Token en = P->lex.cur; lex_next(&P->lex);
      long long v = next_val;
      int has_val = 0;
      if(tok_is(P, TK_ASSIGN)){
        lex_next(&P->lex);
        v = parse_enum_value(P);
        has_val = 1;
      }
      module_add_enum_const(M, en.start, en.len, v, iloc);
      next_val = v + 1;

      items=(EnumItem*)xrealloc(items, sizeof(EnumItem)*(size_t)(item_n+1));
      items[item_n++] = (EnumItem){ en.start, en.len, v, has_val, iloc };

      if(tok_is(P, TK_COMMA)){
        lex_next(&P->lex);
        if(tok_is(P, TK_NEWLINE)) eat_newlines(P);
        continue;
      }
      eat_stmt_end(P,"expected end of line after enum value");
    }
    eat(P, TK_DEDENT, "expected dedent after enum");
    ed->items = items;
    ed->item_n = item_n;
    module_add_enum(M, ed);
    return;
  }

  if(tok_is(P, TK_LBRACE)){
    ed->is_brace = 1;
    eat(P, TK_LBRACE, "");
    while(!tok_is(P, TK_RBRACE) && !tok_is(P, TK_EOF)){
      if(tok_is(P, TK_NEWLINE)){ eat_newlines(P); continue; }
      if(!tok_is(P, TK_IDENT)) perr(P,"expected enum name");
      SrcLoc iloc = tok_loc(&P->lex);
      Token en = P->lex.cur; lex_next(&P->lex);
      long long v = next_val;
      int has_val = 0;
      if(tok_is(P, TK_ASSIGN)){
        lex_next(&P->lex);
        v = parse_enum_value(P);
        has_val = 1;
      }
      module_add_enum_const(M, en.start, en.len, v, iloc);
      next_val = v + 1;

      items=(EnumItem*)xrealloc(items, sizeof(EnumItem)*(size_t)(item_n+1));
      items[item_n++] = (EnumItem){ en.start, en.len, v, has_val, iloc };

      if(tok_is(P, TK_COMMA)){ lex_next(&P->lex); continue; }
      if(tok_is(P, TK_RBRACE)) break;
      perr(P,"expected ',' or '}' in enum");
    }
    eat(P, TK_RBRACE, "expected '}' after enum");
    if(tok_is(P, TK_SEMI)) lex_next(&P->lex);
    ed->items = items;
    ed->item_n = item_n;
    module_add_enum(M, ed);
    return;
  }

  perr(P,"expected ':' or '{' after enum");
}

static void parse_typedef(Parser* P, Module* M, AttrList attrs){
  SrcLoc loc=tok_loc(&P->lex);
  eat(P, TK_TYPEDEF, "");
  Type* ty = parse_type(P);
  if(!tok_is(P, TK_IDENT)) perr(P,"expected name after typedef");
  Token nm = P->lex.cur; lex_next(&P->lex);
  tenv_add_alias(P->tenv, nm.start, nm.len, ty, loc);

  TypedefDecl* td=(TypedefDecl*)xmalloc(sizeof(TypedefDecl));
  memset(td,0,sizeof(*td));
  td->name = nm.start;
  td->len = nm.len;
  td->ty = ty;
  td->loc = loc;
  td->attrs = attrs.attrs;
  td->attr_n = attrs.n;
  module_add_typedef(M, td);

  eat_stmt_end(P,"expected end of line after typedef");
}

static void parse_struct(Parser* P, Module* M, AttrList attrs){
  SrcLoc loc=tok_loc(&P->lex);
  eat(P,TK_STRUCT,"");

  if(!tok_is(P,TK_IDENT)) perr(P,"expected struct name");
  Token nm=P->lex.cur; lex_next(&P->lex);

  Field* fields=NULL;
  int fn=0;

  if(tok_is(P, TK_COLON)){
    eat(P, TK_COLON, "");
    if(!tok_is(P, TK_NEWLINE)) perr(P,"expected newline after ':'");
    eat_newlines(P);
    eat(P, TK_INDENT, "expected indent");

    while(!tok_is(P,TK_DEDENT) && !tok_is(P,TK_EOF)){
      if(tok_is(P, TK_NEWLINE)){ eat_newlines(P); continue; }
      if(!tok_is(P,TK_IDENT)) perr(P,"expected field name");
      Token f=P->lex.cur; lex_next(&P->lex);
      eat(P,TK_COLON,"expected ':' after field");
      Type* ty=parse_type(P);
      eat_stmt_end(P,"expected end of line after field");

      fields=(Field*)xrealloc(fields,sizeof(Field)*(size_t)(fn+1));
      fields[fn]=(Field){ f.start, f.len, ty, 0 };
      fn++;
    }
    eat(P, TK_DEDENT, "expected dedent after struct");
  } else {
    eat(P,TK_LBRACE,"expected '{' after struct name");
    while(!tok_is(P,TK_RBRACE)){
      if(!tok_is(P,TK_IDENT)) perr(P,"expected field name");
      Token f=P->lex.cur; lex_next(&P->lex);
      eat(P,TK_COLON,"expected ':' after field");
      Type* ty=parse_type(P);
      eat(P,TK_SEMI,"expected ';' after field");

      fields=(Field*)xrealloc(fields,sizeof(Field)*(size_t)(fn+1));
      fields[fn]=(Field){ f.start, f.len, ty, 0 };
      fn++;
    }
    eat(P,TK_RBRACE,"expected '}' after struct");

    if(tok_is(P,TK_SEMI)) lex_next(&P->lex); /* optional ';' */
  }

  (void)tenv_add_struct(P->tenv, nm.start, nm.len, fields, fn, loc);

  StructDecl* sd=(StructDecl*)xmalloc(sizeof(StructDecl));
  sd->name=nm.start; sd->len=nm.len;
  sd->fields=fields;
  sd->field_n=fn;
  sd->loc=loc;
  sd->attrs=attrs.attrs;
  sd->attr_n=attrs.n;

  M->structs=(StructDecl**)xrealloc(M->structs,sizeof(StructDecl*)*(size_t)(M->struct_n+1));
  M->structs[M->struct_n++]=sd;
}

static FnDecl* parse_fn(Parser* P, int is_extern, int is_unsafe, int is_kernel, AttrList attrs){
  SrcLoc loc=tok_loc(&P->lex);
  eat(P,TK_FN,"");

  if(!tok_is_name(P)) perr(P,"expected function name");
  Token nm=P->lex.cur; lex_next(&P->lex);

  const char** pnames=NULL;
  int*  plens=NULL;
  Type** ptypes=NULL;
  int ar=0;

  /* params: (a,b) OR none */
  if(tok_is(P, TK_LPAREN)){
    lex_next(&P->lex);

    if(!tok_is(P, TK_RPAREN)){
      for(;;){
        if(!tok_is_name(P)) perr(P,"expected parameter name");
        Token pn=P->lex.cur; lex_next(&P->lex);

        Type* pt = parse_opt_type(P);

        pnames=(const char**)xrealloc((void*)pnames,sizeof(char*)*(size_t)(ar+1));
        plens =(int*) xrealloc(plens, sizeof(int)*(size_t)(ar+1));
        ptypes=(Type**)xrealloc(ptypes,sizeof(Type*)*(size_t)(ar+1));
        pnames[ar]=pn.start; plens[ar]=pn.len; ptypes[ar]=pt;
        ar++;

        if(tok_is(P,TK_COMMA)){ lex_next(&P->lex); continue; }
        break;
      }
    }
    eat(P,TK_RPAREN,"expected ')' after params");
  }

  /* return type: default i64; optional -> T */
  Type* ret = P->tenv->b.ty_i64;
  if(tok_is(P,TK_ARROW)){
    lex_next(&P->lex);
    ret = parse_type(P);
  }

  Stmt* body=NULL;
  if(is_extern){
    eat_stmt_end(P,"expected end of line after extern fn declaration");
  } else {
    body=parse_block(P);
  }

  FnDecl* fn=(FnDecl*)xmalloc(sizeof(FnDecl));
  memset(fn,0,sizeof(*fn));
  fn->name=nm.start; fn->len=nm.len;
  fn->pnames=pnames; fn->plens=plens; fn->ptypes=ptypes; fn->arity=ar;
  fn->ret=ret;
  fn->body=body;
  fn->loc=loc;
  fn->is_extern=is_extern;
  fn->is_unsafe=is_unsafe;
  fn->is_kernel=is_kernel;
  fn->fty = type_fn(P->tenv, ret, ptypes, ar);
  fn->attrs = attrs.attrs;
  fn->attr_n = attrs.n;
  return fn;
}

Module* parse_module(TypeEnv* tenv, const char* src, const char* path){
  Parser P;
  memset(&P,0,sizeof(P));
  P.tenv=tenv;
  P.src=src;
  P.path=path;
  lex_init(&P.lex, src, path);

  Module* M=(Module*)xmalloc(sizeof(Module));
  memset(M,0,sizeof(*M));
  M->path=path;
  M->src=src;
  module_name_from_path(M);

  while(!tok_is(&P,TK_EOF)){
    if(tok_is(&P,TK_NEWLINE) || tok_is(&P, TK_INDENT) || tok_is(&P, TK_DEDENT)){
      lex_next(&P.lex);
      continue;
    }
    AttrList attrs = parse_attrs(&P);
    if(tok_is(&P,TK_IMPORT)){
      if(attrs.n) perr(&P, "attributes not allowed on import");
      parse_import(&P,M);
      continue;
    }
    if(tok_is(&P,TK_TYPEDEF)){ parse_typedef(&P,M, attrs); continue; }
    if(tok_is(&P,TK_ENUM)){ parse_enum(&P,M, attrs); continue; }
    if(tok_is(&P,TK_STRUCT)){ parse_struct(&P,M, attrs); continue; }

    int is_extern = 0;
    int is_static = 0;
    int is_unsafe = 0;
    int is_kernel = 0;
    int had_mod = 0;
    for(;;){
      if(tok_is(&P, TK_EXTERN)){ is_extern = 1; had_mod = 1; lex_next(&P.lex); continue; }
      if(tok_is(&P, TK_STATIC)){ is_static = 1; had_mod = 1; lex_next(&P.lex); continue; }
      if(tok_is(&P, TK_UNSAFE)){ is_unsafe = 1; had_mod = 1; lex_next(&P.lex); continue; }
      if(tok_is(&P, TK_KERNEL)){ is_kernel = 1; had_mod = 1; lex_next(&P.lex); continue; }
      break;
    }

    if(tok_is(&P,TK_FN)){
      if(is_static) perr(&P, "static not supported for fn");
      FnDecl* fn=parse_fn(&P, is_extern, is_unsafe, is_kernel, attrs);
      M->fns=(FnDecl**)xrealloc(M->fns,sizeof(FnDecl*)*(size_t)(M->fn_n+1));
      M->fns[M->fn_n++]=fn;
      continue;
    }
    if(tok_is(&P, TK_LET)){
      if(is_unsafe || is_kernel) perr(&P, "unsafe/kernel only valid on fn or block");
      parse_global_var(&P, M, is_extern, is_static, attrs);
      continue;
    }
    if(had_mod){
      perr(&P, "unexpected modifiers at top level");
    }
    if(attrs.n){
      perr(&P, "attributes must precede a declaration");
    }
    perr(&P,"unexpected token at top level");
  }

  return M;
}
