// src/main.c
#include "vm.h"
#include "ir_codegen_gas.h"
#include "ir_codegen_pe.h"
#include "ir_codegen_elf.h"
#include "ir_codegen_macho.h"
#include "ir_codegen_arm64.h"
#include "hlsl_codegen.h"
#include "bc_vm.h"
#include "lint.h"
#include "abi.h"
#include "util.h"
#include "ir.h"
#include "sema.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <process.h>
#include <direct.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#endif

static const char* tkname(TokenKind k){
  switch(k){
    case TK_EOF: return "EOF";
    case TK_IDENT: return "IDENT";
    case TK_NUMBER: return "NUMBER";
    case TK_FLOAT: return "FLOAT";
    case TK_STRING: return "STRING";
    case TK_LET: return "LET";
    case TK_ASSIGN: return "ASSIGN";
    case TK_COLON: return "COLON";
    case TK_LBRACKET: return "LBRACKET";
    case TK_RBRACKET: return "RBRACKET";
    case TK_SEMI: return "SEMI";
    case TK_COMMA: return "COMMA";
    case TK_FN: return "FN";
    case TK_RET: return "RET";
    case TK_LPAREN: return "LPAREN";
    case TK_RPAREN: return "RPAREN";
    case TK_LBRACE: return "LBRACE";
    case TK_RBRACE: return "RBRACE";
    case TK_DOT: return "DOT";
    case TK_SAY: return "SAY";
    case TK_SAYSTR: return "SAYSTR";
    case TK_NEWLINE: return "NEWLINE";
    case TK_INDENT: return "INDENT";
    case TK_DEDENT: return "DEDENT";
    case TK_ENUM: return "ENUM";
    case TK_TYPEDEF: return "TYPEDEF";
    case TK_SWITCH: return "SWITCH";
    case TK_CASE: return "CASE";
    case TK_DEFAULT: return "DEFAULT";
    case TK_AS: return "AS";
    case TK_FALLTHROUGH: return "FALLTHROUGH";
    case TK_SIZEOF: return "SIZEOF";
    case TK_ALIGNOF: return "ALIGNOF";
    case TK_CONST: return "CONST";
    case TK_UNSAFE: return "UNSAFE";
    case TK_KERNEL: return "KERNEL";
    case TK_STATIC: return "STATIC";
    case TK_EXTERN: return "EXTERN";
    default: return "OTHER";
  }
}

static void dump_tokens(const char* src, const char* path){
  Lexer L;
  lex_init(&L, src, path);
  for(;;){
    Token t = L.cur;
    printf("%4d:%-3d  %-10s  len=%d  text='", t.line, t.col, tkname(t.kind), t.len);
    for(int i=0;i<t.len;i++){
      char c = t.start[i];
      if(c=='\n') printf("\\n");
      else if(c=='\r') printf("\\r");
      else if(c=='\t') printf("\\t");
      else putchar(c);
    }
    printf("'\n");
    if(t.kind==TK_EOF) break;
    lex_next(&L);
  }
}

static const char* norm_input_path(const char* p){
  // Do NOT prefix anything. Caller passes correct relative/absolute path.
  return p;
}

static int is_gpu_target_name(const char* t){
  if(!t) return 0;
  return strcmp(t, "cuda")==0 || strcmp(t, "metal")==0 || strcmp(t, "vulkan")==0 ||
         strcmp(t, "directml")==0 || strcmp(t, "hlsl")==0 || strcmp(t, "dxil")==0;
}

static const char* base_dir_for(const char* entry_path, char* buf, int cap);
static void scan_imports(const char* src, const char* path, const char*** out_list, int* out_n);
static int import_list_has(const char** list, int n, const char* name);
static void import_list_add(const char*** list, int* n, const char* name);
static int path_is_stdlib(const char* path);
static void maybe_add_std_prelude(const char*** list, int* n, int freestanding, const char* path);

static void usage(void){
  printf(
    "tezzc usage:\n"
    "  tezzc run     <file.tn> [--debug] [--native|--fast] [--bc]\n"
    "  tezzc check   <file.tn>\n"
    "  tezzc fmt     <file.tn>\n"
    "  tezzc lint    <file.tn>\n"
    "  tezzc cheader <file.tn> <out.h>\n"
    "  tezzc abidump <file.tn> <out.tnx>\n"
    "  tezzc abiverify <file.tn> <expected.tnx>\n"
    "  tezzc pyext <file.tn> [out_dir] [--module <name>]\n"
    "  tezzc ir      <file.tn> [--repro]\n"
    "  tezzc buildir <file.tn> <out.s> [--repro] [--target <name>]\n"
    "  tezzc buildexe <file.tn> <out.exe> [--target <name>] [--verify]\n"
    "  tezzc buildexe --status\n"
    "  flags: --freestanding --async=zero --vectorize|--no-vectorize --target <name> --experimental-gpu\n"
  );
}

static void ensure_dir(const char* path){
#ifdef _WIN32
  (void)_mkdir(path);
#else
  (void)mkdir(path, 0777);
#endif
}

static int run_exe_process(const char* exe_path, int argc, char** argv){
#ifdef _WIN32
  int n = argc + 2;
  const char** av = (const char**)malloc(sizeof(char*)*(size_t)n);
  if(!av) die("out of memory");
  av[0] = exe_path;
  for(int i=0;i<argc;i++) av[i+1] = argv[i];
  av[argc+1] = NULL;
  int rc = _spawnv(_P_WAIT, exe_path, av);
  free(av);
  return rc;
#else
  int n = argc + 2;
  char** av = (char**)malloc(sizeof(char*)*(size_t)n);
  if(!av) die("out of memory");
  av[0] = (char*)exe_path;
  for(int i=0;i<argc;i++) av[i+1] = argv[i];
  av[argc+1] = NULL;
  pid_t pid = fork();
  if(pid == 0){
    execv(exe_path, av);
    _exit(127);
  }
  int status = 0;
  if(pid > 0) waitpid(pid, &status, 0);
  free(av);
  if(WIFEXITED(status)) return WEXITSTATUS(status);
  return 1;
#endif
}


static int buildexe_from_path(const char* path, const char* out_path, const char* build_target, int repro, int do_verify){
  char basebuf[1024];
  const char* base = base_dir_for(path, basebuf, (int)sizeof(basebuf));

  TypeEnv tenv; tenv_init(&tenv);
  ModuleGraph G; mg_init(&G);

  char* src = read_file_cstr(path);

  const char** pre_imports = NULL;
  int pre_n = 0;
  scan_imports(src, path, &pre_imports, &pre_n);
  maybe_add_std_prelude(&pre_imports, &pre_n, 0, path);
  for(int i=0;i<pre_n;i++){
    (void)mg_load(&G, &tenv, base, pre_imports[i]);
  }

  Module* root = parse_module(&tenv, src, path);

  for(int i=0;i<root->import_n;i++){
    (void)mg_load(&G, &tenv, base, root->imports[i]);
  }

  sema_set_target(build_target);
  sema_check_graph(&G, &tenv, root, 1);

  if(G.mod_n==G.mod_cap){
    G.mod_cap*=2;
    G.mods=(Module**)realloc(G.mods,sizeof(Module*)*G.mod_cap);
    if(!G.mods) die("out of memory");
  }
  G.mods[G.mod_n++]=root;

  ir_set_repro(repro);
  IRModule* irm = ir_build_from_graph(&G, &tenv, root);
  ir_optimize(irm);
  int r = -1;
  int is_win_target = (strcmp(build_target, "x86_64")==0 || strcmp(build_target, "native")==0 ||
                       strcmp(build_target, "x86")==0 || strcmp(build_target, "arm64")==0);
  if(strcmp(build_target, "linux")==0 || strcmp(build_target, "elf")==0){
    r = ir_compile_to_elf_exe(irm, out_path);
  } else if(strcmp(build_target, "macos")==0 || strcmp(build_target, "macho")==0){
    r = ir_compile_to_macho_exe(irm, out_path);
  } else if(strcmp(build_target, "linux-arm64")==0 || strcmp(build_target, "arm64-linux")==0){
    r = ir_compile_to_elf_arm64_exe(irm, out_path);
  } else if(strcmp(build_target, "macos-arm64")==0 || strcmp(build_target, "arm64-macos")==0){
    r = ir_compile_to_macho_arm64_exe(irm, out_path);
  } else if(is_win_target){
    r = ir_compile_to_pe_exe(irm, out_path, build_target);
  } else {
    fprintf(stderr, "buildexe: unsupported target '%s'\n", build_target);
    r = -1;
  }
  ir_free(irm);
  mg_free(&G);
  tenv_free(&tenv);

  if(r==0 && do_verify){
    if(strcmp(build_target, "macos")==0 || strcmp(build_target, "macho")==0 ||
       strcmp(build_target, "macos-arm64")==0 || strcmp(build_target, "arm64-macos")==0){
      if(ir_verify_macho_exe(out_path) != 0){
        fprintf(stderr, "buildexe verify failed\n");
        return 1;
      }
    } else if(strcmp(build_target, "linux")==0 || strcmp(build_target, "elf")==0 ||
              strcmp(build_target, "linux-arm64")==0 || strcmp(build_target, "arm64-linux")==0){
      if(ir_verify_elf_exe(out_path) != 0){
        fprintf(stderr, "buildexe verify failed\n");
        return 1;
      }
    } else {
      if(ir_verify_pe_exe(out_path) != 0){
        fprintf(stderr, "buildexe verify failed\n");
        return 1;
      }
    }
  }
  return (r==0) ? 0 : 1;
}

static void buildexe_status(void){
  printf("buildexe status:\n");
  printf("  - Windows PE/COFF: native codegen (x86_64, x86) + arm64 codegen (verify-only)\n");
  printf("  - IR->machine code lowering: x86_64 IR v4 (full current op set)\n");
  printf("  - Sections (.text/.rdata/.data): globals + strings emitted on x86_64/x86/arm64\n");
  printf("  - Imports/IAT: ExitProcess + WriteFile + lstrlenA/lstrcmpA\n");
  printf("  - Windows targets: x86_64/x86 native; arm64 verify-only stub emitter\n");
  printf("  - ELF (Linux): x86_64 native (syscall-based)\n");
  printf("  - Mach-O (macOS): x86_64 native (syscall-based)\n");
  printf("  - Verifiers: PE/ELF/Mach-O (bounds + entry checks)\n");
  printf("See docs/NATIVE_BACKEND.md for the current backend matrix.\n");
}

static void path_dirname(const char* path, char* out, int cap){
  // Minimal dirname:
  // "examples/full_test.tn" -> "examples"
  // "full_test.tn" -> "."
  int n=(int)strlen(path);
  int i=n-1;
  for(; i>=0; i--){
    if(path[i]=='/' || path[i]=='\\') break;
  }
  if(i<0){
    // no slash => current dir
    if(cap>2){ out[0]='.'; out[1]=0; }
    return;
  }
  int k=i;
  if(k<=0){
    // root like "/x.tn" => "/"
    if(cap>2){ out[0]=path[0]; out[1]=0; }
    return;
  }
  if(k>=cap) k=cap-1;
  memcpy(out, path, (size_t)k);
  out[k]=0;
}

static int is_tools_script(const char* path){
  if(!path) return 0;
  if(strstr(path, "tools/tezz.tn")) return 1;
  if(strstr(path, "tools\\tezz.tn")) return 1;
  if(strstr(path, "tools/tezz_frontend.tn")) return 1;
  if(strstr(path, "tools\\tezz_frontend.tn")) return 1;
  return 0;
}

static int entry_uses_freestanding(const char* path){
  if(!path) return 0;
  char* src = read_file_cstr(path);
  const char** imports = NULL;
  int n = 0;
  int found = 0;
  scan_imports(src, path, &imports, &n);
  for(int i=0;i<n;i++){
    const char* imp = imports[i] ? imports[i] : "";
    if(strcmp(imp, "os")==0 || strcmp(imp, "sys")==0){
      found = 1;
      break;
    }
  }
  for(int i=0;i<n;i++){
    free((void*)imports[i]);
  }
  free((void*)imports);
  free(src);
  return found;
}

static const char* base_dir_for(const char* entry_path, char* buf, int cap){
  path_dirname(entry_path, buf, cap);
  return buf;
}

static char* dup_cstr(const char* s){
  if(!s) s="";
  size_t n=strlen(s);
  char* out=(char*)malloc(n+1);
  if(!out) die("out of memory");
  memcpy(out,s,n+1);
  return out;
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
  char* s = dup_cstr(name);
  const char** items = (const char**)realloc((void*)(*list), sizeof(char*)*(size_t)(*n+1));
  if(!items) die("out of memory");
  items[*n] = s;
  *list = items;
  *n = *n + 1;
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

static void maybe_add_std_prelude(const char*** list, int* n, int freestanding, const char* path){
  if(freestanding) return;
  if(path_is_stdlib(path)) return;
  import_list_add(list, n, "std");
  if(getenv("TEZZ_DEBUG_PRELUDE")){
    fprintf(stderr, "prelude: added std for %s\n", path ? path : "(null)");
  }
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
        items = (const char**)realloc((void*)items, sizeof(char*)*(size_t)(n+1));
        if(!items) die("out of memory");
        items[n++] = s;
      }
    }
    if(L.cur.kind == TK_EOF) break;
    lex_next(&L);
  }
  *out_list = items;
  *out_n = n;
}

/* -------------------------------------------------
 * Formatter (AST -> canonical source)
 * ------------------------------------------------- */
static void fmt_indent(FILE* out, int n){
  for(int i=0;i<n;i++) fputc(' ', out);
}

static void fmt_type(FILE* out, Type* t);

static void fmt_string(FILE* out, const char* s){
  fputc('"', out);
  for(const char* p=s ? s : ""; *p; p++){
    char c = *p;
    if(c=='\\') fputs("\\\\", out);
    else if(c=='"') fputs("\\\"", out);
    else if(c=='\n') fputs("\\n", out);
    else if(c=='\r') fputs("\\r", out);
    else if(c=='\t') fputs("\\t", out);
    else fputc(c, out);
  }
  fputc('"', out);
}

static int bin_prec(BinOp op){
  switch(op){
    case OP_LOR:  return 1;
    case OP_LAND: return 2;
    case OP_BOR:  return 3;
    case OP_BXOR: return 4;
    case OP_BAND: return 5;
    case OP_EQ: case OP_NEQ: return 6;
    case OP_LT: case OP_LTE: case OP_GT: case OP_GTE: return 7;
    case OP_SHL: case OP_SHR: return 8;
    case OP_ADD: case OP_SUB: return 9;
    case OP_MUL: case OP_DIV: case OP_MOD: return 10;
  }
  return 10;
}

static const char* bin_op_str(BinOp op){
  switch(op){
    case OP_ADD: return "+";
    case OP_SUB: return "-";
    case OP_MUL: return "*";
    case OP_DIV: return "/";
    case OP_MOD: return "%";
    case OP_EQ: return "==";
    case OP_NEQ: return "!=";
    case OP_LT: return "<";
    case OP_LTE: return "<=";
    case OP_GT: return ">";
    case OP_GTE: return ">=";
    case OP_LAND: return "&&";
    case OP_LOR: return "||";
    case OP_BAND: return "&";
    case OP_BOR: return "|";
    case OP_BXOR: return "^";
    case OP_SHL: return "<<";
    case OP_SHR: return ">>";
  }
  return "?";
}

static int expr_prec(Expr* e){
  if(!e) return 100;
  switch(e->k){
    case EX_BIN: return bin_prec(e->bin.op);
    case EX_UN:
    case EX_ADDR:
    case EX_DEREF:
    case EX_CAST:
    case EX_SIZEOF:
    case EX_ALIGNOF: return 11;
    case EX_CALL:
    case EX_INDEX:
    case EX_DOT: return 12;
    default: return 13;
  }
}

static void fmt_expr(FILE* out, Expr* e, int parent_prec);

static void fmt_expr(FILE* out, Expr* e, int parent_prec){
  if(!e){ fputs("0", out); return; }
  int prec = expr_prec(e);
  if(prec < parent_prec) fputc('(', out);
  switch(e->k){
    case EX_NUM:
      fprintf(out, "%lld", e->num);
      break;
    case EX_FLOAT:
      fprintf(out, "%.17g", e->f);
      break;
    case EX_STR:
      fmt_string(out, e->str);
      break;
    case EX_NAME:
      fprintf(out, "%.*s", e->name.len, e->name.name);
      break;
    case EX_BIN:
      fmt_expr(out, e->bin.a, prec);
      fprintf(out, " %s ", bin_op_str(e->bin.op));
      fmt_expr(out, e->bin.b, prec + 1);
      break;
    case EX_UN:{
      const char* op = (e->un.op==UN_NEG) ? "-" :
                       (e->un.op==UN_LNOT) ? "!" :
                       (e->un.op==UN_BNOT) ? "~" : "+";
      fputs(op, out);
      fmt_expr(out, e->un.e, prec);
      break;
    }
    case EX_ADDR:
      fputc('&', out);
      fmt_expr(out, e->un.e, prec);
      break;
    case EX_DEREF:
      fputc('*', out);
      fmt_expr(out, e->un.e, prec);
      break;
    case EX_CALL:
      fmt_expr(out, e->call.callee, 12);
      fputc('(', out);
      for(int i=0;i<e->call.argc;i++){
        if(i) fputs(", ", out);
        fmt_expr(out, e->call.args[i], 0);
      }
      fputc(')', out);
      break;
    case EX_INDEX:
      fmt_expr(out, e->index.base, 12);
      fputc('[', out);
      fmt_expr(out, e->index.idx, 0);
      fputc(']', out);
      break;
    case EX_DOT:
      fmt_expr(out, e->dot.base, 12);
      fputc('.', out);
      fprintf(out, "%.*s", e->dot.memlen, e->dot.mem);
      break;
    case EX_ARRAY_LIT:
      fputc('[', out);
      for(int i=0;i<e->arr.n;i++){
        if(i) fputs(", ", out);
        fmt_expr(out, e->arr.items[i], 0);
      }
      fputc(']', out);
      break;
    case EX_STRUCT_LIT:
      fprintf(out, "%.*s{", e->stlit.tlen, e->stlit.tname);
      for(int i=0;i<e->stlit.n;i++){
        if(i) fputs(", ", out);
        fprintf(out, "%.*s: ", e->stlit.flen[i], e->stlit.f[i]);
        fmt_expr(out, e->stlit.v[i], 0);
      }
      fputc('}', out);
      break;
    case EX_CAST:
      fmt_expr(out, e->cast.e, prec);
      fputs(" as ", out);
      fmt_type(out, e->cast.to);
      break;
    case EX_SIZEOF:
    case EX_ALIGNOF:{
      const char* kw = (e->k==EX_SIZEOF) ? "sizeof" : "alignof";
      fputs(kw, out);
      if(e->siz.is_type){
        fputc('(', out);
        fmt_type(out, e->siz.ty);
        fputc(')', out);
      } else {
        fputc(' ', out);
        fmt_expr(out, e->siz.e, 0);
      }
      break;
    }
  }
  if(prec < parent_prec) fputc(')', out);
}

static void fmt_type(FILE* out, Type* t){
  if(!t){ fputs("i64", out); return; }
  const char* builtin = type_builtin_name(t);
  if(builtin){ fputs(builtin, out); return; }
  if(t->k==TY_PTR && t->elem && t->elem->k==TY_FN){
    Type* f = t->elem;
    fputs("fn(", out);
    for(int i=0;i<f->fparam_n;i++){
      if(i) fputs(", ", out);
      fmt_type(out, f->fparams[i]);
    }
    fputc(')', out);
    if(f->fret && f->fret->k != TY_I64){
      fputs(" -> ", out);
      fmt_type(out, f->fret);
    }
    return;
  }
  if(t->k==TY_PTR){
    fputc('*', out);
    fmt_type(out, t->elem);
    return;
  }
  if(t->k==TY_ARRAY){
    fputc('[', out);
    fmt_type(out, t->elem);
    fprintf(out, ";%lld]", t->len);
    return;
  }
  if(t->k==TY_STRUCT){
    if(t->sname) fprintf(out, "%.*s", t->sname_len, t->sname);
    else fputs("struct", out);
    return;
  }
  if(t->k==TY_FN){
    fputs("fn(", out);
    for(int i=0;i<t->fparam_n;i++){
      if(i) fputs(", ", out);
      fmt_type(out, t->fparams[i]);
    }
    fputc(')', out);
    if(t->fret && t->fret->k != TY_I64){
      fputs(" -> ", out);
      fmt_type(out, t->fret);
    }
    return;
  }
  fputs("i64", out);
}

static const char* asg_op_str(TokenKind k){
  switch(k){
    case TK_ASSIGN: return "=";
    case TK_PLUSEQ: return "+=";
    case TK_MINUSEQ: return "-=";
    case TK_STAREQ: return "*=";
    case TK_SLASHEQ: return "/=";
    case TK_PERCENTEQ: return "%=";
    case TK_ANDEQ: return "&=";
    case TK_OREQ: return "|=";
    case TK_XOREQ: return "^=";
    case TK_SHLEQ: return "<<=";
    case TK_SHREQ: return ">>=";
    default: return "=";
  }
}

static void fmt_stmt(FILE* out, Stmt* s, int indent);

static void fmt_block(FILE* out, Stmt* blk, int indent){
  if(!blk || blk->k!=ST_BLOCK) return;
  for(int i=0;i<blk->block.n;i++){
    fmt_stmt(out, blk->block.items[i], indent);
  }
}

static void fmt_attrs(FILE* out, Attr* attrs, int n){
  for(int i=0;i<n;i++){
    fprintf(out, "@%.*s", attrs[i].len, attrs[i].name);
    if(attrs[i].arg && attrs[i].arg_len > 0){
      fprintf(out, "(\"%.*s\")", attrs[i].arg_len, attrs[i].arg);
    }
    fputc('\n', out);
  }
}

static void fmt_for_part(FILE* out, Stmt* s){
  if(!s) return;
  if(s->k==ST_LET){
    if(s->let.is_const) fputs("const ", out);
    fputs("let ", out);
    fprintf(out, "%.*s", s->let.len, s->let.name);
    if(s->let.ty && s->let.ty->k!=TY_I64){
      fputc(':', out);
      fmt_type(out, s->let.ty);
    }
    if(s->let.init){
      fputs(" = ", out);
      fmt_expr(out, s->let.init, 0);
    }
    return;
  }
  if(s->k==ST_ASSIGN){
    fmt_expr(out, s->asg.lhs, 0);
    if(s->asg.op==TK_PLUSPLUS){ fputs("++", out); return; }
    if(s->asg.op==TK_MINUSMINUS){ fputs("--", out); return; }
    fprintf(out, " %s ", asg_op_str(s->asg.op));
    if(s->asg.rhs) fmt_expr(out, s->asg.rhs, 0);
    return;
  }
  if(s->k==ST_EXPR){
    fmt_expr(out, s->expr.e, 0);
    return;
  }
  // fallback
  fmt_expr(out, s->expr.e, 0);
}

static void fmt_stmt(FILE* out, Stmt* s, int indent){
  if(!s) return;
  switch(s->k){
    case ST_BLOCK:
      fmt_indent(out, indent);
      fputs("{\n", out);
      fmt_block(out, s, indent + 2);
      fmt_indent(out, indent);
      fputs("}\n", out);
      return;
    case ST_LET:
      fmt_indent(out, indent);
      if(s->let.is_const) fputs("const ", out);
      fputs("let ", out);
      fprintf(out, "%.*s", s->let.len, s->let.name);
      if(s->let.ty && s->let.ty->k!=TY_I64){
        fputc(':', out);
        fmt_type(out, s->let.ty);
      }
      if(s->let.init){
        fputs(" = ", out);
        fmt_expr(out, s->let.init, 0);
      }
      fputc('\n', out);
      return;
    case ST_ASSIGN:
      fmt_indent(out, indent);
      fmt_expr(out, s->asg.lhs, 0);
      if(s->asg.op==TK_PLUSPLUS){ fputs("++\n", out); return; }
      if(s->asg.op==TK_MINUSMINUS){ fputs("--\n", out); return; }
      fprintf(out, " %s ", asg_op_str(s->asg.op));
      if(s->asg.rhs) fmt_expr(out, s->asg.rhs, 0);
      fputc('\n', out);
      return;
    case ST_EXPR:
      fmt_indent(out, indent);
      fmt_expr(out, s->expr.e, 0);
      fputc('\n', out);
      return;
    case ST_IF:
      fmt_indent(out, indent);
      fputs("if ", out);
      fmt_expr(out, s->iff.cond, 0);
      fputs(":\n", out);
      fmt_block(out, s->iff.thenb, indent + 2);
      if(s->iff.elseb){
        fmt_indent(out, indent);
        fputs("else:\n", out);
        fmt_block(out, s->iff.elseb, indent + 2);
      }
      return;
    case ST_WHILE:
      fmt_indent(out, indent);
      fputs("while ", out);
      fmt_expr(out, s->wh.cond, 0);
      fputs(":\n", out);
      fmt_block(out, s->wh.body, indent + 2);
      return;
    case ST_FOR:
      fmt_indent(out, indent);
      fputs("for ", out);
      fmt_for_part(out, s->fr.init);
      fputs("; ", out);
      if(s->fr.cond) fmt_expr(out, s->fr.cond, 0);
      fputs("; ", out);
      fmt_for_part(out, s->fr.step);
      fputs(":\n", out);
      fmt_block(out, s->fr.body, indent + 2);
      return;
    case ST_BREAK:
      fmt_indent(out, indent);
      fputs("break\n", out);
      return;
    case ST_CONTINUE:
      fmt_indent(out, indent);
      fputs("continue\n", out);
      return;
    case ST_RET:
      fmt_indent(out, indent);
      fputs("ret", out);
      if(s->ret.e){
        fputc(' ', out);
        fmt_expr(out, s->ret.e, 0);
      }
      fputc('\n', out);
      return;
    case ST_FALLTHROUGH:
      fmt_indent(out, indent);
      fputs("fallthrough\n", out);
      return;
    case ST_SWITCH:
      fmt_indent(out, indent);
      fputs("switch ", out);
      fmt_expr(out, s->sw.cond, 0);
      fputs(":\n", out);
      for(int i=0;i<s->sw.case_n;i++){
        SwitchCase* c = &s->sw.cases[i];
        fmt_indent(out, indent + 2);
        if(c->val){
          fputs("case ", out);
          fmt_expr(out, c->val, 0);
          fputs(":\n", out);
        } else {
          fputs("default:\n", out);
        }
        fmt_block(out, c->body, indent + 4);
      }
      return;
    case ST_UNSAFE:
      fmt_indent(out, indent);
      fputs("unsafe:\n", out);
      fmt_block(out, s->uns.body, indent + 2);
      return;
  }
}

static void fmt_module(FILE* out, Module* m){
  if(!m) return;

  for(int i=0;i<m->import_n;i++){
    fprintf(out, "import \"%s\"\n", m->imports[i]);
  }
  if(m->import_n>0) fputc('\n', out);

  for(int i=0;i<m->typedef_n;i++){
    TypedefDecl* td = m->typedefs[i];
    if(td->attr_n) fmt_attrs(out, td->attrs, td->attr_n);
    fputs("typedef ", out);
    fmt_type(out, td->ty);
    fputc(' ', out);
    fprintf(out, "%.*s\n", td->len, td->name);
  }
  if(m->typedef_n>0) fputc('\n', out);

  for(int i=0;i<m->enum_n;i++){
    EnumDecl* ed = m->enums[i];
    if(ed->attr_n) fmt_attrs(out, ed->attrs, ed->attr_n);
    fputs("enum", out);
    if(ed->len>0){
      fputc(' ', out);
      fprintf(out, "%.*s", ed->len, ed->name);
    }
    if(ed->is_brace){
      fputs(" {\n", out);
      for(int j=0;j<ed->item_n;j++){
        EnumItem* it = &ed->items[j];
        fmt_indent(out, 2);
        fprintf(out, "%.*s", it->len, it->name);
        if(it->has_val){
          fprintf(out, " = %lld", it->val);
        }
        if(j + 1 < ed->item_n) fputc(',', out);
        fputc('\n', out);
      }
      fputs("}\n\n", out);
    } else {
      fputs(":\n", out);
      for(int j=0;j<ed->item_n;j++){
        EnumItem* it = &ed->items[j];
        fmt_indent(out, 2);
        fprintf(out, "%.*s", it->len, it->name);
        if(it->has_val){
          fprintf(out, " = %lld", it->val);
        }
        fputc('\n', out);
      }
      fputc('\n', out);
    }
  }

  for(int i=0;i<m->struct_n;i++){
    StructDecl* st = m->structs[i];
    if(st->attr_n) fmt_attrs(out, st->attrs, st->attr_n);
    fprintf(out, "struct %.*s:\n", st->len, st->name);
    for(int f=0; f<st->field_n; f++){
      fmt_indent(out, 2);
      fprintf(out, "%.*s:", st->fields[f].name_len, st->fields[f].name);
      fmt_type(out, st->fields[f].ty);
      fputc('\n', out);
    }
    fputc('\n', out);
  }

  for(int i=0;i<m->global_n;i++){
    struct GlobalVar* g = &m->globals[i];
    if(g->attr_n) fmt_attrs(out, g->attrs, g->attr_n);
    if(g->is_extern) fputs("extern ", out);
    if(g->is_static) fputs("static ", out);
    fputs("let ", out);
    fprintf(out, "%.*s", g->len, g->name);
    if(g->ty && g->ty->k!=TY_I64){
      fputc(':', out);
      fmt_type(out, g->ty);
    }
    if(g->init){
      fputs(" = ", out);
      fmt_expr(out, g->init, 0);
    }
    fputc('\n', out);
  }
  if(m->global_n>0) fputc('\n', out);

  for(int i=0;i<m->fn_n;i++){
    FnDecl* fn = m->fns[i];
    if(fn->attr_n) fmt_attrs(out, fn->attrs, fn->attr_n);
    if(fn->is_extern) fputs("extern ", out);
    if(fn->is_unsafe) fputs("unsafe ", out);
    if(fn->is_kernel) fputs("kernel ", out);
    fputs("fn ", out);
    fprintf(out, "%.*s", fn->len, fn->name);
    fputc('(', out);
    for(int p=0;p<fn->arity;p++){
      if(p) fputs(", ", out);
      fprintf(out, "%.*s:", fn->plens[p], fn->pnames[p]);
      fmt_type(out, fn->ptypes[p]);
    }
    fputc(')', out);
    if(fn->ret && fn->ret->k!=TY_I64){
      fputs(" -> ", out);
      fmt_type(out, fn->ret);
    }
    if(fn->is_extern){
      fputc('\n', out);
      continue;
    }
    fputs(":\n", out);
    fmt_block(out, fn->body, 2);
    fputc('\n', out);
  }
}

int main(int argc, char** argv){
  if(argc < 2){ usage(); return 1; }

  if(strcmp(argv[1],"lex")==0){
    if(argc < 3){ usage(); return 1; }
    const char* path = argv[2];
    char* src = read_file_cstr(path);
    dump_tokens(src, path);
    return 0;
  }

  if(argc < 3){ usage(); return 1; }

  int debug = 0;
  int repro = 0;
  int freestanding = 0;
  int async_zero = 0;
  int vectorize = -1;
  int allow_gpu = 0;
  // Production default: native execution unless --bc is requested.
  int run_native = 1;
  int run_bc = 0;
  const char* target = "native";
  for(int i=1;i<argc;i++){
    if(strcmp(argv[i],"--")==0) break;
    if(strcmp(argv[i],"--debug")==0) debug=1;
    if(strcmp(argv[i],"--repro")==0 || strcmp(argv[i],"--deterministic")==0) repro=1;
    if(strcmp(argv[i],"--freestanding")==0 || strcmp(argv[i],"--kernel")==0) freestanding=1;
    if(strcmp(argv[i],"--async=zero")==0 || strcmp(argv[i],"--async=zero-cost")==0) async_zero=1;
    if(strcmp(argv[i],"--vectorize")==0) vectorize=1;
    if(strcmp(argv[i],"--no-vectorize")==0) vectorize=0;
    if(strcmp(argv[i],"--experimental-gpu")==0) allow_gpu=1;
    if(strcmp(argv[i],"--native")==0 || strcmp(argv[i],"--buildexe")==0 || strcmp(argv[i],"--fast")==0){ run_native=1; run_bc=0; }
    if(strcmp(argv[i],"--bc")==0 || strcmp(argv[i],"--bytecode")==0){ run_bc=1; run_native=0; }
    if(strcmp(argv[i],"--target")==0 && i+1<argc){ target = argv[i+1]; i++; continue; }
    if(strncmp(argv[i],"--target=",9)==0){ target = argv[i] + 9; continue; }
  }
  if(vectorize < 0){
    vectorize = 0;
    if(strcmp(argv[1], "run")==0 && run_native){
      vectorize = 1;
    } else if(strcmp(argv[1], "buildexe")==0){
      vectorize = 1;
    }
  }
  sema_set_freestanding(freestanding);
  sema_set_async_zero(async_zero);
  sema_set_target(target);
  vm_set_async_zero(async_zero);
  ir_set_async_zero(async_zero);
  ir_set_vectorize(vectorize);

  // ---------------- run (VM) ----------------
  if(strcmp(argv[1],"run")==0){
    const char* run_path = NULL;
    int dashdash = -1;
    for(int i=2;i<argc;i++){
      if(strcmp(argv[i],"--")==0){ dashdash = i; break; }
    }
    int scan_end = (dashdash >= 0) ? dashdash : argc;
    for(int i=2;i<scan_end;i++){
      if(strcmp(argv[i],"--target")==0 && i+1<scan_end){ i++; continue; }
      if(strncmp(argv[i],"--target=",9)==0) continue;
      if(argv[i][0]=='-') continue;
      run_path = argv[i];
      break;
    }
    if(!run_path){
      usage();
      return 1;
    }
    if(is_gpu_target_name(target) && !allow_gpu){
      fprintf(stderr, "gpu targets are experimental; pass --experimental-gpu to proceed\n");
      return 1;
    }
    if(run_native && is_tools_script(run_path)){
      run_native = 0;
      run_bc = 1;
    }
    if(freestanding || entry_uses_freestanding(run_path)){
      fprintf(stderr,
        "tezzc run: '%s' is freestanding (imports os/sys). Use tools/limine_build.ps1 or OS build scripts.\n",
        run_path);
      return 1;
    }
    int prog_argc = 0;
    char** prog_argv = NULL;
    if(dashdash >= 0 && dashdash + 1 < argc){
      prog_argc = argc - (dashdash + 1);
      prog_argv = &argv[dashdash + 1];
    } else {
      // args after the input file are forwarded
      for(int i=2;i<argc;i++){
        if(argv[i] == run_path){
          if(i + 1 < argc){
            prog_argc = argc - (i + 1);
            prog_argv = &argv[i + 1];
          }
          break;
        }
      }
    }
    if(run_native){
      char out_path[512];
#ifdef _WIN32
      ensure_dir("build");
      snprintf(out_path, sizeof(out_path), "build\\tezz_run.exe");
#else
      ensure_dir("build");
      snprintf(out_path, sizeof(out_path), "build/tezz_run");
#endif
      if(buildexe_from_path(run_path, out_path, target, repro, 0) != 0){
        return 1;
      }
      return run_exe_process(out_path, prog_argc, prog_argv);
    }
    if(run_bc){
      return bc_vm_run_file(run_path, debug, prog_argc, prog_argv);
    }
      return 1;
    }

  // ---------------- check (parse+imports+sema) ----------------
  if(strcmp(argv[1],"check")==0){
    const char* path = norm_input_path(argv[2]);

    char basebuf[1024];
    const char* base = base_dir_for(path, basebuf, (int)sizeof(basebuf));

    TypeEnv tenv; tenv_init(&tenv);
    ModuleGraph G; mg_init(&G);

    char* src = read_file_cstr(path);

    // pre-load imports so types are available during parsing
    const char** pre_imports = NULL;
    int pre_n = 0;
    scan_imports(src, path, &pre_imports, &pre_n);
    maybe_add_std_prelude(&pre_imports, &pre_n, freestanding, path);
    for(int i=0;i<pre_n;i++){
      (void)mg_load(&G, &tenv, base, pre_imports[i]);
    }

    Module* root = parse_module(&tenv, src, path);

    // load imports relative to the entry file directory
    for(int i=0;i<root->import_n;i++){
      (void)mg_load(&G, &tenv, base, root->imports[i]);
    }

    sema_check_graph(&G, &tenv, root, 1);

    printf("OK: %s\n", path);

    mg_free(&G);
    tenv_free(&tenv);
    return 0;
  }

  // ---------------- fmt (AST pretty-printer) ----------------
  if(strcmp(argv[1],"fmt")==0){
    const char* path = norm_input_path(argv[2]);
    char* src = read_file_cstr(path);
    TypeEnv tenv; tenv_init(&tenv);
    Module* root = parse_module(&tenv, src, path);
    FILE* out = fopen(path, "wb");
    if(!out) die("cannot open output file");
    fmt_module(out, root);
    fclose(out);
    tenv_free(&tenv);
    free(src);
    printf("OK: %s\n", path);
    return 0;
  }

  // ---------------- cheader (C header + ABI checks) ----------------
  if(strcmp(argv[1],"cheader")==0){
    if(argc < 4){ usage(); return 1; }
    const char* path = norm_input_path(argv[2]);
    const char* out_h = argv[3];

    char basebuf[1024];
    const char* base = base_dir_for(path, basebuf, (int)sizeof(basebuf));

    TypeEnv tenv; tenv_init(&tenv);
    ModuleGraph G; mg_init(&G);

    char* src = read_file_cstr(path);

    const char** pre_imports = NULL;
    int pre_n = 0;
    scan_imports(src, path, &pre_imports, &pre_n);
    maybe_add_std_prelude(&pre_imports, &pre_n, freestanding, path);
    for(int i=0;i<pre_n;i++){
      (void)mg_load(&G, &tenv, base, pre_imports[i]);
    }

    Module* root = parse_module(&tenv, src, path);
    for(int i=0;i<root->import_n;i++){
      (void)mg_load(&G, &tenv, base, root->imports[i]);
    }

    sema_check_graph(&G, &tenv, root, 1);

    if(G.mod_n==G.mod_cap){
      G.mod_cap*=2;
      G.mods=(Module**)realloc(G.mods,sizeof(Module*)*G.mod_cap);
      if(!G.mods) die("out of memory");
    }
    G.mods[G.mod_n++]=root;

    abi_emit_header(&G, &tenv, root, out_h);
    printf("OK: %s -> %s\n", path, out_h);

    mg_free(&G);
    tenv_free(&tenv);
    return 0;
  }

  // ---------------- abidump (TNX ABI manifest) ----------------
  if(strcmp(argv[1],"abidump")==0){
    if(argc < 4){ usage(); return 1; }
    const char* path = norm_input_path(argv[2]);
    const char* out_tnx = argv[3];

    char basebuf[1024];
    const char* base = base_dir_for(path, basebuf, (int)sizeof(basebuf));

    TypeEnv tenv; tenv_init(&tenv);
    ModuleGraph G; mg_init(&G);

    char* src = read_file_cstr(path);

    const char** pre_imports = NULL;
    int pre_n = 0;
    scan_imports(src, path, &pre_imports, &pre_n);
    maybe_add_std_prelude(&pre_imports, &pre_n, freestanding, path);
    for(int i=0;i<pre_n;i++){
      (void)mg_load(&G, &tenv, base, pre_imports[i]);
    }

    Module* root = parse_module(&tenv, src, path);
    for(int i=0;i<root->import_n;i++){
      (void)mg_load(&G, &tenv, base, root->imports[i]);
    }

    sema_check_graph(&G, &tenv, root, 1);

    if(G.mod_n==G.mod_cap){
      G.mod_cap*=2;
      G.mods=(Module**)realloc(G.mods,sizeof(Module*)*G.mod_cap);
      if(!G.mods) die("out of memory");
    }
    G.mods[G.mod_n++]=root;

    abi_emit_tnx(&G, &tenv, root, out_tnx);
    printf("OK: %s -> %s\n", path, out_tnx);

    mg_free(&G);
    tenv_free(&tenv);
    return 0;
  }

  // ---------------- abiverify (compare ABI manifest) ----------------
  if(strcmp(argv[1],"abiverify")==0){
    if(argc < 4){ usage(); return 1; }
    const char* path = norm_input_path(argv[2]);
    const char* expect = argv[3];
    ensure_dir("build");
    char tmp[512];
#ifdef _WIN32
    snprintf(tmp, sizeof(tmp), "build\\abi_tmp_%ld_%ld.tnx", (long)_getpid(), (long)clock());
#else
    snprintf(tmp, sizeof(tmp), "build/abi_tmp_%ld_%ld.tnx", (long)getpid(), (long)clock());
#endif

    char basebuf[1024];
    const char* base = base_dir_for(path, basebuf, (int)sizeof(basebuf));

    TypeEnv tenv; tenv_init(&tenv);
    ModuleGraph G; mg_init(&G);

    char* src = read_file_cstr(path);

    const char** pre_imports = NULL;
    int pre_n = 0;
    scan_imports(src, path, &pre_imports, &pre_n);
    maybe_add_std_prelude(&pre_imports, &pre_n, freestanding, path);
    for(int i=0;i<pre_n;i++){
      (void)mg_load(&G, &tenv, base, pre_imports[i]);
    }

    Module* root = parse_module(&tenv, src, path);
    for(int i=0;i<root->import_n;i++){
      (void)mg_load(&G, &tenv, base, root->imports[i]);
    }

    sema_check_graph(&G, &tenv, root, 1);

    if(G.mod_n==G.mod_cap){
      G.mod_cap*=2;
      G.mods=(Module**)realloc(G.mods,sizeof(Module*)*G.mod_cap);
      if(!G.mods) die("out of memory");
    }
    G.mods[G.mod_n++]=root;

    abi_emit_tnx(&G, &tenv, root, tmp);

    char* got = read_file_cstr(tmp);
    char* exp = read_file_cstr(expect);
    int ok = 0;
    if(got && exp && strcmp(got, exp)==0) ok = 1;
    if(got) free(got);
    if(exp) free(exp);
    remove(tmp);

    mg_free(&G);
    tenv_free(&tenv);

    if(ok){
      printf("ABI VERIFY OK: %s\n", path);
      return 0;
    }
    fprintf(stderr, "ABI VERIFY FAILED: %s\n", path);
    return 1;
  }

  // ---------------- pyext (CPython extension wrapper scaffold) ----------------
  if(strcmp(argv[1],"pyext")==0){
    if(argc < 3){ usage(); return 1; }
    const char* path = norm_input_path(argv[2]);
    const char* out_dir = "build/pyext";
    const char* module_name = NULL;
    for(int i=3;i<argc;i++){
      if(strcmp(argv[i], "--module")==0){
        if(i + 1 >= argc) die("pyext: missing module name after --module");
        module_name = argv[++i];
      } else {
        out_dir = argv[i];
      }
    }

    ensure_dir("build");
    ensure_dir(out_dir);

    char basebuf[1024];
    const char* base = base_dir_for(path, basebuf, (int)sizeof(basebuf));

    TypeEnv tenv; tenv_init(&tenv);
    ModuleGraph G; mg_init(&G);

    char* src = read_file_cstr(path);

    const char** pre_imports = NULL;
    int pre_n = 0;
    scan_imports(src, path, &pre_imports, &pre_n);
    maybe_add_std_prelude(&pre_imports, &pre_n, freestanding, path);
    for(int i=0;i<pre_n;i++){
      (void)mg_load(&G, &tenv, base, pre_imports[i]);
    }

    Module* root = parse_module(&tenv, src, path);
    for(int i=0;i<root->import_n;i++){
      (void)mg_load(&G, &tenv, base, root->imports[i]);
    }

    sema_check_graph(&G, &tenv, root, 1);

    if(G.mod_n==G.mod_cap){
      G.mod_cap*=2;
      G.mods=(Module**)realloc(G.mods,sizeof(Module*)*G.mod_cap);
      if(!G.mods) die("out of memory");
    }
    G.mods[G.mod_n++]=root;

    abi_emit_pyext(&G, &tenv, root, out_dir, module_name);
    printf("OK: %s -> %s\n", path, out_dir);

    mg_free(&G);
    tenv_free(&tenv);
    return 0;
  }

  // ---------------- lint (AST lints) ----------------
  if(strcmp(argv[1],"lint")==0){
    const char* path = norm_input_path(argv[2]);

    char basebuf[1024];
    const char* base = base_dir_for(path, basebuf, (int)sizeof(basebuf));

    TypeEnv tenv; tenv_init(&tenv);
    ModuleGraph G; mg_init(&G);

    char* src = read_file_cstr(path);

    const char** pre_imports = NULL;
    int pre_n = 0;
    scan_imports(src, path, &pre_imports, &pre_n);
    maybe_add_std_prelude(&pre_imports, &pre_n, freestanding, path);
    for(int i=0;i<pre_n;i++){
      (void)mg_load(&G, &tenv, base, pre_imports[i]);
    }

    Module* root = parse_module(&tenv, src, path);

    for(int i=0;i<root->import_n;i++){
      (void)mg_load(&G, &tenv, base, root->imports[i]);
    }

    sema_check_graph(&G, &tenv, root, 1);

    int warns = lint_module(root);
    if(warns == 0) printf("OK: %s\n", path);
    else printf("WARN: %s (%d warnings)\n", path, warns);

    mg_free(&G);
    tenv_free(&tenv);
    return warns ? 1 : 0;
  }

  // ---------------- ir (dump IR) ----------------
  if(strcmp(argv[1],"ir")==0){
    const char* path = norm_input_path(argv[2]);

    char basebuf[1024];
    const char* base = base_dir_for(path, basebuf, (int)sizeof(basebuf));

    TypeEnv tenv; tenv_init(&tenv);
    ModuleGraph G; mg_init(&G);

    char* src = read_file_cstr(path);

    const char** pre_imports = NULL;
    int pre_n = 0;
    scan_imports(src, path, &pre_imports, &pre_n);
    maybe_add_std_prelude(&pre_imports, &pre_n, freestanding, path);
    for(int i=0;i<pre_n;i++){
      (void)mg_load(&G, &tenv, base, pre_imports[i]);
    }

    Module* root = parse_module(&tenv, src, path);

    // load imports relative to the entry file directory
    for(int i=0;i<root->import_n;i++){
      (void)mg_load(&G, &tenv, base, root->imports[i]);
    }

    sema_check_graph(&G, &tenv, root, 1);

    if(G.mod_n==G.mod_cap){
      G.mod_cap*=2;
      G.mods=(Module**)realloc(G.mods,sizeof(Module*)*G.mod_cap);
      if(!G.mods) die("out of memory");
    }
    G.mods[G.mod_n++]=root;

    ir_set_repro(repro);
    IRModule* irm = ir_build_from_module(root, &tenv);
    ir_optimize(irm);
    ir_dump(irm);
    ir_free(irm);

    mg_free(&G);
    tenv_free(&tenv);
    return 0;
  }

  // ---------------- buildir (IR -> GAS asm) ----------------
  if(strcmp(argv[1],"buildir")==0){
    if(argc < 4){ usage(); return 1; }
    const char* path = norm_input_path(argv[2]);
    const char* build_target = target;
    for(int i=4;i<argc;i++){
      if(strcmp(argv[i],"--target")==0 && i+1<argc){
        build_target = argv[i+1];
        i++;
        continue;
      }
      if(strncmp(argv[i],"--target=",9)==0){
        build_target = argv[i] + 9;
        continue;
      }
    }
    if(is_gpu_target_name(build_target) && !allow_gpu){
      fprintf(stderr, "gpu targets are experimental; pass --experimental-gpu to proceed\n");
      return 1;
    }

    char basebuf[1024];
    const char* base = base_dir_for(path, basebuf, (int)sizeof(basebuf));

    TypeEnv tenv; tenv_init(&tenv);
    ModuleGraph G; mg_init(&G);

    char* src = read_file_cstr(path);

    const char** pre_imports = NULL;
    int pre_n = 0;
    scan_imports(src, path, &pre_imports, &pre_n);
    maybe_add_std_prelude(&pre_imports, &pre_n, freestanding, path);
    for(int i=0;i<pre_n;i++){
      (void)mg_load(&G, &tenv, base, pre_imports[i]);
    }

    Module* root = parse_module(&tenv, src, path);

    // load imports relative to the entry file directory
    for(int i=0;i<root->import_n;i++){
      (void)mg_load(&G, &tenv, base, root->imports[i]);
    }

    sema_set_target(build_target);
    int require_main = 1;
    if(strcmp(build_target, "hlsl")==0 || strcmp(build_target, "dxil")==0){
      require_main = 0;
    }
    sema_check_graph(&G, &tenv, root, require_main);

    if(G.mod_n==G.mod_cap){
      G.mod_cap*=2;
      G.mods=(Module**)realloc(G.mods,sizeof(Module*)*G.mod_cap);
      if(!G.mods) die("out of memory");
    }
    G.mods[G.mod_n++]=root;

    if(strcmp(build_target, "hlsl")==0 || strcmp(build_target, "dxil")==0){
      hlsl_emit_kernels(&G, &tenv, root, argv[3]);
      mg_free(&G);
      tenv_free(&tenv);
      printf("OK: %s -> %s\n", path, argv[3]);
      return 0;
    }

    ir_set_repro(repro);
    IRModule* irm = ir_build_from_graph(&G, &tenv, root);
    ir_optimize(irm);
    ir_compile_to_gas_target(irm, argv[3], build_target);
    ir_free(irm);
    mg_free(&G);
    tenv_free(&tenv);

    printf("OK: %s -> %s\n", path, argv[3]);
    return 0;
  }

  // ---------------- buildexe (IR -> native exe) ----------------
  if(strcmp(argv[1],"buildexe")==0){
    if(argc >= 3 && strcmp(argv[2],"--status")==0){
      buildexe_status();
      return 0;
    }
    if(argc < 4){ usage(); return 1; }
    const char* path = norm_input_path(argv[2]);
    if(entry_uses_freestanding(path)){
      fprintf(stderr,
        "buildexe: '%s' imports os/sys (freestanding). Use tools/limine_build.ps1 or OS build scripts.\n",
        path);
      return 1;
    }
    const char* build_target = "x86_64";
    int do_verify = 0;
    for(int i=3;i<argc;i++){
      if(strcmp(argv[i],"--target")==0 && i+1<argc){
        build_target = argv[i+1];
        i++;
        continue;
      }
      if(strncmp(argv[i],"--target=",9)==0){
        build_target = argv[i] + 9;
        continue;
      }
      if(strcmp(argv[i],"--verify")==0){
        do_verify = 1;
        continue;
      }
    }

    int r = buildexe_from_path(path, argv[3], build_target, repro, do_verify);
    if(r==0){
      printf("OK: %s -> %s\n", path, argv[3]);
      if(do_verify) printf("VERIFY: %s OK\n", argv[3]);
      return 0;
    }
    return 1;
  }

  usage();
  return 1;
}
