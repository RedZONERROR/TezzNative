#include "lint.h"
#include "util.h"
#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

typedef enum {
  LINT_UNUSED_VAR = 0,
  LINT_UNUSED_PARAM,
  LINT_UNUSED_IMPORT,
  LINT_SHADOWED_VAR,
  LINT_DEAD_CODE,
  LINT_EMPTY_BLOCK,
  LINT_NO_EMPTY_SWITCH,
  LINT_MAGIC_NUMBER,
  LINT_NAMING,
  LINT_RULE_COUNT
} LintRule;

static const char* lint_rule_name(LintRule r){
  switch(r){
    case LINT_UNUSED_VAR: return "unused-var";
    case LINT_UNUSED_PARAM: return "unused-param";
    case LINT_UNUSED_IMPORT: return "unused-import";
    case LINT_SHADOWED_VAR: return "shadowed-var";
    case LINT_DEAD_CODE: return "dead-code";
    case LINT_EMPTY_BLOCK: return "empty-block";
    case LINT_NO_EMPTY_SWITCH: return "no-empty-switch";
    case LINT_MAGIC_NUMBER: return "magic-number";
    case LINT_NAMING: return "naming-convention";
    default: return "unknown";
  }
}

static unsigned int lint_rule_mask_all(void){
  return (LINT_RULE_COUNT >= 32) ? 0xFFFFFFFFu : ((1u << LINT_RULE_COUNT) - 1u);
}

typedef struct {
  unsigned int* line_mask;
  int line_n;
} LintConfig;

typedef struct {
  const char* name;
  int len;
  SrcLoc loc;
  int used;
  int is_param;
} VarInfo;

typedef struct {
  VarInfo* vars;
  int n, cap;
} Scope;

typedef struct {
  Module* mod;
  LintConfig cfg;
  Scope* scopes;
  int scount, scap;
  int warn_count;
  const char** import_name;
  int* import_len;
  unsigned char* import_used;
  int import_n;
} LintState;

static void* xmalloc(size_t n){ void* p=malloc(n); if(!p) die("out of memory"); return p; }
static void* xrealloc(void* p, size_t n){ void* q=realloc(p,n); if(!q) die("out of memory"); return q; }

static char* lint_import_to_modname(const char* s, int* out_len){
  if(!s) s = "";
  int len = (int)strlen(s);
  int end = len;
  for(int i=0;i<end;i++){
    if(s[i]=='@'){ end = i; break; }
  }
  if(end >= 3 && s[end-3]=='.' && s[end-2]=='t' && s[end-1]=='n'){
    end -= 3;
  }
  int start = 0;
  if(end >= 2 && s[0]=='.' && (s[1]=='/' || s[1]=='\\')) start = 2;
  if(end - start >= 4 && s[start]=='l' && s[start+1]=='i' && s[start+2]=='b' &&
     (s[start+3]=='/' || s[start+3]=='\\')){
    start += 4;
  }
  int n = end - start;
  if(n < 0) n = 0;
  char* out = (char*)xmalloc((size_t)n + 1);
  for(int i=0;i<n;i++){
    char c = s[start + i];
    if(c=='/' || c=='\\') c='.';
    out[i] = c;
  }
  out[n] = 0;
  if(out_len) *out_len = n;
  return out;
}

static int lint_flatten_mod_name(Expr* e, char** out, int* out_len){
  if(!e || !out || !out_len) return 0;
  if(e->k==EX_NAME){
    int n = e->name.len;
    char* s = (char*)xmalloc((size_t)n + 1);
    memcpy(s, e->name.name, (size_t)n);
    s[n] = 0;
    *out = s;
    *out_len = n;
    return 1;
  }
  if(e->k==EX_DOT){
    char* left = NULL;
    int llen = 0;
    if(!lint_flatten_mod_name(e->dot.base, &left, &llen)) return 0;
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

static int is_ignored_name(const char* name, int len){
  return (len > 0 && name && name[0] == '_');
}

static const char* find_comment_start(const char* line, int len){
  for(int i=0;i+1<len;i++){
    if(line[i]=='/' && line[i+1]=='/') return line + i;
    if(line[i]=='/' && line[i+1]=='*') return line + i;
  }
  return NULL;
}

static const char* find_in_line(const char* line, int len, const char* pat){
  int plen = (int)strlen(pat);
  if(plen<=0 || len<plen) return NULL;
  for(int i=0;i+plen<=len;i++){
    if(memcmp(line+i, pat, (size_t)plen)==0) return line+i;
  }
  return NULL;
}

static int lint_rule_from_name(const char* s, int len){
  if(len==0) return -1;
  if(len==3 && memcmp(s,"all",3)==0) return -2;
  if(len==1 && s[0]=='*') return -2;
  if(len==6 && memcmp(s,"unused",6)==0) return LINT_UNUSED_VAR;
  if(len==10 && memcmp(s,"unused-var",10)==0) return LINT_UNUSED_VAR;
  if(len==5 && memcmp(s,"param",5)==0) return LINT_UNUSED_PARAM;
  if(len==11 && memcmp(s,"unused-param",11)==0) return LINT_UNUSED_PARAM;
  if(len==12 && memcmp(s,"unused-import",12)==0) return LINT_UNUSED_IMPORT;
  if(len==13 && memcmp(s,"unused_import",13)==0) return LINT_UNUSED_IMPORT;
  if(len==8 && memcmp(s,"shadowed",8)==0) return LINT_SHADOWED_VAR;
  if(len==12 && memcmp(s,"shadowed-var",12)==0) return LINT_SHADOWED_VAR;
  if(len==4 && memcmp(s,"dead",4)==0) return LINT_DEAD_CODE;
  if(len==9 && memcmp(s,"dead-code",9)==0) return LINT_DEAD_CODE;
  if(len==5 && memcmp(s,"empty",5)==0) return LINT_EMPTY_BLOCK;
  if(len==11 && memcmp(s,"empty-block",11)==0) return LINT_EMPTY_BLOCK;
  if(len==15 && memcmp(s,"no-empty-switch",15)==0) return LINT_NO_EMPTY_SWITCH;
  if(len==12 && memcmp(s,"empty-switch",12)==0) return LINT_NO_EMPTY_SWITCH;
  if(len==5 && memcmp(s,"magic",5)==0) return LINT_MAGIC_NUMBER;
  if(len==12 && memcmp(s,"magic-number",12)==0) return LINT_MAGIC_NUMBER;
  if(len==6 && memcmp(s,"naming",6)==0) return LINT_NAMING;
  if(len==17 && memcmp(s,"naming-convention",17)==0) return LINT_NAMING;
  if(len==10 && memcmp(s,"snake_case",10)==0) return LINT_NAMING;
  if(len==10 && memcmp(s,"pascalcase",10)==0) return LINT_NAMING;
  return -1;
}

static unsigned int lint_parse_rule_list(const char* s, int len){
  unsigned int mask = 0;
  int i = 0;
  while(i < len){
    while(i < len && (s[i]==' ' || s[i]=='\t' || s[i]==',')) i++;
    int start = i;
    while(i < len && s[i] != ',' && s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n') i++;
    int toklen = i - start;
    if(toklen <= 0) continue;
    int r = lint_rule_from_name(s + start, toklen);
    if(r == -2) return lint_rule_mask_all();
    if(r >= 0) mask |= (1u << r);
  }
  return mask;
}

static void lint_config_init(LintConfig* cfg, const char* src){
  cfg->line_mask = NULL;
  cfg->line_n = 0;
  if(!src){
    cfg->line_n = 1;
    cfg->line_mask = (unsigned int*)xmalloc(sizeof(unsigned int) * 2);
    cfg->line_mask[0] = 0;
    cfg->line_mask[1] = 0;
    return;
  }
  int lines = 1;
  for(const char* p=src; *p; p++) if(*p=='\n') lines++;
  cfg->line_n = lines;
  cfg->line_mask = (unsigned int*)xmalloc(sizeof(unsigned int) * (size_t)(lines + 1));

  unsigned int cur_mask = 0; // all enabled by default
  int line = 1;
  const char* line_start = src;
  const char* p = src;
  while(1){
    if(*p=='\n' || *p==0){
      int len = (int)(p - line_start);
      const char* cstart = find_comment_start(line_start, len);
      if(cstart){
        int clen = (int)(line_start + len - cstart);
        const char* lint = find_in_line(cstart, clen, "lint:");
        if(lint){
          const char* q = lint + 5;
          const char* end = line_start + len;
          while(q < end && (*q==' ' || *q=='\t')) q++;
          int is_disable = 0;
          if((end - q) >= 7 && memcmp(q, "disable", 7)==0){ is_disable = 1; q += 7; }
          else if((end - q) >= 6 && memcmp(q, "enable", 6)==0){ is_disable = 0; q += 6; }
          while(q < end && (*q==' ' || *q=='\t')) q++;
          unsigned int list = lint_parse_rule_list(q, (int)(end - q));
          if(list == 0) list = lint_rule_mask_all();
          if(is_disable) cur_mask |= list;
          else cur_mask &= ~list;
        }
      }
      if(line <= lines) cfg->line_mask[line] = cur_mask;
      if(*p==0) break;
      line++;
      p++;
      line_start = p;
      continue;
    }
    p++;
  }
}

static void lint_config_free(LintConfig* cfg){
  if(cfg->line_mask) free(cfg->line_mask);
  cfg->line_mask = NULL;
  cfg->line_n = 0;
}

static void lint_imports_init(LintState* L, Module* m){
  L->import_name = NULL;
  L->import_len = NULL;
  L->import_used = NULL;
  L->import_n = 0;
  if(!m || m->import_n<=0) return;

  L->import_n = m->import_n;
  L->import_name = (const char**)xmalloc(sizeof(char*)*(size_t)L->import_n);
  L->import_len = (int*)xmalloc(sizeof(int)*(size_t)L->import_n);
  L->import_used = (unsigned char*)xmalloc((size_t)L->import_n);
  memset(L->import_used, 0, (size_t)L->import_n);

  for(int i=0;i<m->import_n;i++){
    const char* s = m->imports[i] ? m->imports[i] : "";
    int nlen = 0;
    char* name = lint_import_to_modname(s, &nlen);
    L->import_name[i] = name;
    L->import_len[i] = nlen;
  }
}

static void lint_imports_free(LintState* L){
  if(!L) return;
  for(int i=0;i<L->import_n;i++){
    if(L->import_name && L->import_name[i]) free((void*)L->import_name[i]);
  }
  if(L->import_name) free(L->import_name);
  if(L->import_len) free(L->import_len);
  if(L->import_used) free(L->import_used);
  L->import_name = NULL;
  L->import_len = NULL;
  L->import_used = NULL;
  L->import_n = 0;
}

static int lint_import_index(LintState* L, const char* name, int len){
  if(!L || !name || len<=0) return -1;
  for(int i=0;i<L->import_n;i++){
    if(L->import_len[i]==len && memcmp(L->import_name[i], name, (size_t)len)==0) return i;
  }
  return -1;
}

static void lint_mark_import_used(LintState* L, const char* name, int len){
  int idx = lint_import_index(L, name, len);
  if(idx>=0) L->import_used[idx] = 1;
}

static SrcLoc lint_import_loc(Module* m, const char* imp){
  SrcLoc loc;
  memset(&loc, 0, sizeof(loc));
  loc.path = m ? m->path : "<mem>";
  loc.src = m ? m->src : NULL;
  loc.line = 1;
  loc.col = 1;
  if(!m || !m->src || !imp) return loc;
  const char* p = strstr(m->src, imp);
  if(!p) return loc;
  int line = 1;
  const char* line_start = m->src;
  for(const char* q=m->src; q<p; q++){
    if(*q=='\n'){
      line++;
      line_start = q + 1;
    }
  }
  loc.line = line;
  loc.col = (int)(p - line_start) + 1;
  return loc;
}

static int lint_rule_disabled(LintConfig* cfg, LintRule rule, SrcLoc loc){
  if(!cfg || !cfg->line_mask) return 0;
  if(loc.line <= 0 || loc.line > cfg->line_n) return 0;
  unsigned int mask = cfg->line_mask[loc.line];
  return (mask & (1u << rule)) != 0;
}

static void lint_warn(LintState* L, LintRule rule, SrcLoc loc, const char* fmt, ...){
  if(lint_rule_disabled(&L->cfg, rule, loc)) return;
  char msg[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  char full[320];
  snprintf(full, sizeof(full), "lint[%s]: %s", lint_rule_name(rule), msg);
  warn_at2(loc.path, loc.src, loc.line, loc.col, full);
  L->warn_count++;
}

static void lint_push_scope(LintState* L){
  if(L->scount==L->scap){
    L->scap = L->scap ? L->scap*2 : 16;
    L->scopes = (Scope*)xrealloc(L->scopes, sizeof(Scope)*(size_t)L->scap);
  }
  L->scopes[L->scount].vars = NULL;
  L->scopes[L->scount].n = 0;
  L->scopes[L->scount].cap = 0;
  L->scount++;
}

static void lint_pop_scope(LintState* L){
  if(L->scount<=0) return;
  Scope* sc = &L->scopes[L->scount-1];
  for(int i=0;i<sc->n;i++){
    VarInfo* v = &sc->vars[i];
    if(v->used) continue;
    if(is_ignored_name(v->name, v->len)) continue;
    if(v->is_param){
      lint_warn(L, LINT_UNUSED_PARAM, v->loc, "unused parameter '%.*s'", v->len, v->name);
    } else {
      lint_warn(L, LINT_UNUSED_VAR, v->loc, "unused variable '%.*s'", v->len, v->name);
    }
  }
  if(sc->vars) free(sc->vars);
  sc->vars = NULL;
  sc->n = 0;
  sc->cap = 0;
  L->scount--;
}

static void lint_add_var(LintState* L, const char* name, int len, SrcLoc loc, int is_param){
  if(!name || len<=0) return;
  // shadowing check: look in outer scopes
  for(int si=L->scount-2; si>=0; si--){
    Scope* sc = &L->scopes[si];
    for(int i=0;i<sc->n;i++){
      VarInfo* v = &sc->vars[i];
      if(v->len==len && memcmp(v->name, name, (size_t)len)==0){
        lint_warn(L, LINT_SHADOWED_VAR, loc, "shadowing '%.*s'", len, name);
        si = -1; // break outer
        break;
      }
    }
  }

  Scope* cur = &L->scopes[L->scount-1];
  if(cur->n==cur->cap){
    cur->cap = cur->cap ? cur->cap*2 : 16;
    cur->vars = (VarInfo*)xrealloc(cur->vars, sizeof(VarInfo)*(size_t)cur->cap);
  }
  cur->vars[cur->n++] = (VarInfo){ name, len, loc, 0, is_param };
}

static void lint_mark_used(LintState* L, const char* name, int len){
  for(int si=L->scount-1; si>=0; si--){
    Scope* sc = &L->scopes[si];
    for(int i=0;i<sc->n;i++){
      VarInfo* v = &sc->vars[i];
      if(v->len==len && memcmp(v->name, name, (size_t)len)==0){
        v->used = 1;
        return;
      }
    }
  }
}

static int lint_is_local_name(LintState* L, const char* name, int len){
  for(int si=L->scount-1; si>=0; si--){
    Scope* sc = &L->scopes[si];
    for(int i=0;i<sc->n;i++){
      VarInfo* v = &sc->vars[i];
      if(v->len==len && memcmp(v->name, name, (size_t)len)==0) return 1;
    }
  }
  return 0;
}

static Expr* lint_leftmost_name(Expr* e){
  Expr* cur = e;
  while(cur && cur->k==EX_DOT) cur = cur->dot.base;
  return cur;
}

static void lint_expr_ctx(LintState* L, Expr* e, int allow_lit);

static void lint_expr_lvalue(LintState* L, Expr* e){
  if(!e) return;
  switch(e->k){
    case EX_NAME:
      return;
    case EX_INDEX:
      lint_expr_ctx(L, e->index.base, 0);
      lint_expr_ctx(L, e->index.idx, 0);
      return;
    case EX_DOT:
      lint_expr_ctx(L, e->dot.base, 0);
      return;
    case EX_DEREF:
    case EX_ADDR:
      lint_expr_ctx(L, e->un.e, 0);
      return;
    default:
      lint_expr_ctx(L, e, 0);
      return;
  }
}

static int lint_allow_simple_number(long long v){
  return v==0 || v==1;
}

static int is_snake_case(const char* s, int len, int allow_upper){
  if(!s || len<=0) return 0;
  int i = 0;
  if(s[0]=='_') i = 1;
  int saw_alpha = 0;
  for(; i<len; i++){
    unsigned char c = (unsigned char)s[i];
    if(c=='_') continue;
    if(isdigit(c)) continue;
    if(isalpha(c)){
      saw_alpha = 1;
      if(!allow_upper && isupper(c)) return 0;
      continue;
    }
    return 0;
  }
  return saw_alpha;
}

static int is_upper_snake(const char* s, int len){
  if(!s || len<=0) return 0;
  int i = 0;
  if(s[0]=='_') i = 1;
  int saw_alpha = 0;
  for(; i<len; i++){
    unsigned char c = (unsigned char)s[i];
    if(c=='_') continue;
    if(isdigit(c)) continue;
    if(isupper(c)){ saw_alpha = 1; continue; }
    return 0;
  }
  return saw_alpha;
}

static int is_pascal_case(const char* s, int len){
  if(!s || len<=0) return 0;
  unsigned char c0 = (unsigned char)s[0];
  if(!isupper(c0)) return 0;
  for(int i=0;i<len;i++){
    unsigned char c = (unsigned char)s[i];
    if(c=='_') return 0;
    if(isalnum(c)) continue;
    return 0;
  }
  return 1;
}

static void lint_check_naming(LintState* L, SrcLoc loc, const char* name, int len, int want_pascal, int allow_upper_snake){
  if(!name || len<=0) return;
  if(is_ignored_name(name, len)) return;
  if(lint_rule_disabled(&L->cfg, LINT_NAMING, loc)) return;
  if(want_pascal){
    if(!is_pascal_case(name, len)){
      lint_warn(L, LINT_NAMING, loc, "name '%.*s' should be PascalCase", len, name);
    }
  } else {
    if(is_snake_case(name, len, 0)) return;
    if(allow_upper_snake && is_upper_snake(name, len)) return;
    lint_warn(L, LINT_NAMING, loc, "name '%.*s' should be snake_case", len, name);
  }
}

static void lint_check_magic(LintState* L, Expr* e, int allow_lit){
  if(!e) return;
  if(allow_lit) return;
  if(lint_rule_disabled(&L->cfg, LINT_MAGIC_NUMBER, e->loc)) return;

  if(e->k==EX_NUM){
    if(lint_allow_simple_number(e->num)) return;
    lint_warn(L, LINT_MAGIC_NUMBER, e->loc, "magic number %lld", e->num);
    return;
  }
  if(e->k==EX_FLOAT){
    if(e->f==0.0 || e->f==1.0) return;
    lint_warn(L, LINT_MAGIC_NUMBER, e->loc, "magic float");
    return;
  }
  if(e->k==EX_UN && e->un.op==UN_NEG && e->un.e){
    Expr* inner = e->un.e;
    if(inner->k==EX_NUM){
      long long v = -inner->num;
      if(v==-1) return;
      lint_warn(L, LINT_MAGIC_NUMBER, e->loc, "magic number %lld", v);
      return;
    }
    if(inner->k==EX_FLOAT){
      double v = -inner->f;
      if(v==-1.0) return;
      lint_warn(L, LINT_MAGIC_NUMBER, e->loc, "magic float");
      return;
    }
  }
}

static void lint_expr_ctx(LintState* L, Expr* e, int allow_lit){
  if(!e) return;
  lint_check_magic(L, e, allow_lit);
  switch(e->k){
    case EX_NAME:
      lint_mark_used(L, e->name.name, e->name.len);
      return;
    case EX_BIN:
      lint_expr_ctx(L, e->bin.a, 0);
      lint_expr_ctx(L, e->bin.b, 0);
      return;
    case EX_UN:
      lint_expr_ctx(L, e->un.e, 0);
      return;
    case EX_CALL:
      lint_expr_ctx(L, e->call.callee, 0);
      for(int i=0;i<e->call.argc;i++) lint_expr_ctx(L, e->call.args[i], 0);
      return;
    case EX_INDEX:
      lint_expr_ctx(L, e->index.base, 0);
      lint_expr_ctx(L, e->index.idx, 0);
      return;
    case EX_DOT:
      if(e->dot.base){
        Expr* left = lint_leftmost_name(e->dot.base);
        if(left && left->k==EX_NAME){
          const char* nm = left->name.name;
          int nlen = left->name.len;
          if(!lint_is_local_name(L, nm, nlen)){
            char* mod = NULL;
            int mlen = 0;
            if(lint_flatten_mod_name(e->dot.base, &mod, &mlen)){
              lint_mark_import_used(L, mod, mlen);
              free(mod);
            } else {
              lint_mark_import_used(L, nm, nlen);
            }
          }
        }
      }
      lint_expr_ctx(L, e->dot.base, 0);
      return;
    case EX_ADDR:
    case EX_DEREF:
      lint_expr_ctx(L, e->un.e, 0);
      return;
    case EX_ARRAY_LIT:
      for(int i=0;i<e->arr.n;i++) lint_expr_ctx(L, e->arr.items[i], 1);
      return;
    case EX_STRUCT_LIT:
      for(int i=0;i<e->stlit.n;i++) lint_expr_ctx(L, e->stlit.v[i], 1);
      return;
    case EX_CAST:
      lint_expr_ctx(L, e->cast.e, 0);
      return;
    case EX_SIZEOF:
    case EX_ALIGNOF:
      if(!e->siz.is_type) lint_expr_ctx(L, e->siz.e, 0);
      return;
    default:
      return;
  }
}

static int stmt_is_terminator(Stmt* s){
  if(!s) return 0;
  return s->k==ST_RET || s->k==ST_BREAK || s->k==ST_CONTINUE || s->k==ST_FALLTHROUGH;
}

static void lint_stmt(LintState* L, Stmt* s);

static void lint_block(LintState* L, Stmt* blk){
  if(!blk || blk->k!=ST_BLOCK) return;
  lint_push_scope(L);
  if(blk->block.n==0){
    lint_warn(L, LINT_EMPTY_BLOCK, blk->loc, "empty block");
  }
  int terminated = 0;
  for(int i=0;i<blk->block.n;i++){
    Stmt* it = blk->block.items[i];
    if(terminated){
      lint_warn(L, LINT_DEAD_CODE, it->loc, "unreachable statement");
      continue;
    }
    lint_stmt(L, it);
    if(stmt_is_terminator(it)) terminated = 1;
  }
  lint_pop_scope(L);
}

static void lint_stmt(LintState* L, Stmt* s){
  if(!s) return;
  switch(s->k){
    case ST_BLOCK:
      lint_block(L, s);
      return;
    case ST_LET:
      lint_check_naming(L, s->loc, s->let.name, s->let.len, 0, s->let.is_const ? 1 : 0);
      if(s->let.init) lint_expr_ctx(L, s->let.init, s->let.is_const ? 1 : 0);
      lint_add_var(L, s->let.name, s->let.len, s->loc, 0);
      return;
    case ST_ASSIGN:
      if(s->asg.op==TK_ASSIGN){
        lint_expr_lvalue(L, s->asg.lhs);
        if(s->asg.rhs) lint_expr_ctx(L, s->asg.rhs, 0);
      } else {
        lint_expr_ctx(L, s->asg.lhs, 0);
        if(s->asg.rhs) lint_expr_ctx(L, s->asg.rhs, 0);
      }
      return;
    case ST_EXPR:
      lint_expr_ctx(L, s->expr.e, 0);
      return;
    case ST_IF:
      lint_expr_ctx(L, s->iff.cond, 0);
      lint_stmt(L, s->iff.thenb);
      if(s->iff.elseb) lint_stmt(L, s->iff.elseb);
      return;
    case ST_WHILE:
      lint_expr_ctx(L, s->wh.cond, 0);
      lint_stmt(L, s->wh.body);
      return;
    case ST_FOR:
      lint_push_scope(L);
      if(s->fr.init) lint_stmt(L, s->fr.init);
      if(s->fr.cond) lint_expr_ctx(L, s->fr.cond, 0);
      if(s->fr.step) lint_stmt(L, s->fr.step);
      lint_stmt(L, s->fr.body);
      lint_pop_scope(L);
      return;
    case ST_RET:
      if(s->ret.e) lint_expr_ctx(L, s->ret.e, 0);
      return;
    case ST_SWITCH:
      lint_expr_ctx(L, s->sw.cond, 0);
      if(s->sw.case_n == 0){
        lint_warn(L, LINT_NO_EMPTY_SWITCH, s->loc, "switch has no cases");
      }
      for(int i=0;i<s->sw.case_n;i++){
        SwitchCase* c = &s->sw.cases[i];
        if(c->val) lint_expr_ctx(L, c->val, 1);
        lint_stmt(L, c->body);
      }
      return;
    case ST_UNSAFE:
      lint_stmt(L, s->uns.body);
      return;
    default:
      return;
  }
}

static void lint_fn(LintState* L, FnDecl* fn){
  if(!fn || fn->is_extern || !fn->body) return;
  lint_check_naming(L, fn->loc, fn->name, fn->len, 0, 0);
  lint_push_scope(L);
  for(int i=0;i<fn->arity;i++){
    lint_check_naming(L, fn->loc, fn->pnames[i], fn->plens[i], 0, 0);
    lint_add_var(L, fn->pnames[i], fn->plens[i], fn->loc, 1);
  }
  lint_block(L, fn->body);
  lint_pop_scope(L);
}

int lint_module(Module* m){
  if(!m) return 0;
  LintState L;
  memset(&L, 0, sizeof(L));
  L.mod = m;
  lint_config_init(&L.cfg, m->src);
  lint_imports_init(&L, m);

  for(int i=0;i<m->struct_n;i++){
    StructDecl* st = m->structs[i];
    lint_check_naming(&L, st->loc, st->name, st->len, 1, 0);
  }
  for(int i=0;i<m->typedef_n;i++){
    TypedefDecl* td = m->typedefs[i];
    lint_check_naming(&L, td->loc, td->name, td->len, 1, 0);
  }
  for(int i=0;i<m->enum_n;i++){
    EnumDecl* ed = m->enums[i];
    if(ed->len > 0) lint_check_naming(&L, ed->loc, ed->name, ed->len, 1, 0);
    for(int j=0;j<ed->item_n;j++){
      EnumItem* it = &ed->items[j];
      if(is_ignored_name(it->name, it->len)) continue;
      if(lint_rule_disabled(&L.cfg, LINT_NAMING, it->loc)) continue;
      if(is_pascal_case(it->name, it->len) || is_upper_snake(it->name, it->len)) continue;
      lint_warn(&L, LINT_NAMING, it->loc, "enum item '%.*s' should be PascalCase or UPPER_SNAKE", it->len, it->name);
    }
  }

  for(int i=0;i<m->fn_n;i++){
    lint_fn(&L, m->fns[i]);
  }

  for(int i=0;i<L.import_n;i++){
    if(!L.import_used[i]){
      SrcLoc loc = lint_import_loc(m, m->imports[i]);
      lint_warn(&L, LINT_UNUSED_IMPORT, loc, "unused import '%s'", m->imports[i]);
    }
  }

  while(L.scount > 0) lint_pop_scope(&L);
  if(L.scopes) free(L.scopes);
  lint_imports_free(&L);
  lint_config_free(&L.cfg);
  return L.warn_count;
}
