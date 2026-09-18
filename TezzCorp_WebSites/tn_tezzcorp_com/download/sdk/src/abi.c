// src/abi.c - C header generation + ABI checks
#include "abi.h"
#include "vm.h"
#include "types.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

typedef struct {
  const char** name;
  int* len;
  int n, cap;
} NameSet;

static void* xrealloc(void* p, size_t n){ void* q=realloc(p,n); if(!q) die("out of memory"); return q; }

static int nameset_has(NameSet* s, const char* name, int len){
  for(int i=0;i<s->n;i++){
    if(s->len[i]==len && strncmp(s->name[i], name, (size_t)len)==0) return 1;
  }
  return 0;
}

static void nameset_add(NameSet* s, const char* name, int len){
  if(!name || len<=0) return;
  if(nameset_has(s, name, len)) return;
  if(s->n==s->cap){
    s->cap = s->cap? s->cap*2 : 32;
    s->name = (const char**)xrealloc(s->name, sizeof(char*)*(size_t)s->cap);
    s->len  = (int*)xrealloc(s->len, sizeof(int)*(size_t)s->cap);
  }
  s->name[s->n] = name;
  s->len[s->n] = len;
  s->n++;
}

static void nameset_free(NameSet* s){
  if(!s) return;
  if(s->name) free(s->name);
  if(s->len) free(s->len);
  s->name = NULL;
  s->len = NULL;
  s->n = 0;
  s->cap = 0;
}

static int is_c_ident_name(const char* s, int len){
  if(!s || len<=0 || len>128) return 0;
  char c0 = s[0];
  int head_ok = ((c0>='a'&&c0<='z') || (c0>='A'&&c0<='Z') || c0=='_');
  if(!head_ok) return 0;
  for(int i=1;i<len;i++){
    char c = s[i];
    int ok = ((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_');
    if(!ok) return 0;
  }
  return 1;
}

static void emit_ident(FILE* out, const char* name){
  if(!out || !name) return;
  char c0 = name[0];
  int head_ok = ((c0>='a'&&c0<='z') || (c0>='A'&&c0<='Z') || c0=='_');
  if(!head_ok) return;
  fputc(c0, out);
  int i=1;
  while(name[i]){
    char c = name[i];
    int ok = ((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_');
    if(!ok) break;
    fputc(c, out);
    i++;
  }
}

static void emit_c_base(FILE* out, Type* t){
  if(!t){ fputs("int64_t", out); return; }
  switch(t->k){
    case TY_I8:  fputs("int8_t", out); return;
    case TY_I16: fputs("int16_t", out); return;
    case TY_I32: fputs("int32_t", out); return;
    case TY_I64: fputs("int64_t", out); return;
    case TY_U8:  fputs("uint8_t", out); return;
    case TY_U16: fputs("uint16_t", out); return;
    case TY_U32: fputs("uint32_t", out); return;
    case TY_U64: fputs("uint64_t", out); return;
    case TY_F64: fputs("double", out); return;
    case TY_VOID: fputs("void", out); return;
    case TY_STRUCT:
      if(t->sname) fprintf(out, "%.*s", t->sname_len, t->sname);
      else fputs("void", out);
      return;
    default:
      fputs("void", out);
      return;
  }
}

static void emit_fn_ptr(FILE* out, Type* fnty, const char* name){
  if(!fnty || fnty->k!=TY_FN){
    fputs("void (*", out);
    emit_ident(out, name);
    fputs(")(void)", out);
    return;
  }
  emit_c_base(out, fnty->fret);
  fputs(" (*", out);
  emit_ident(out, name);
  fputs(")(", out);
  for(int i=0;i<fnty->fparam_n;i++){
    if(i) fputs(", ", out);
    Type* pt = fnty->fparams[i];
    if(pt && pt->k==TY_ARRAY){
      emit_c_base(out, pt->elem);
      fputs(" *", out);
    } else if(pt && pt->k==TY_PTR && pt->elem && pt->elem->k==TY_FN){
      emit_fn_ptr(out, pt->elem, NULL);
    } else if(pt && pt->k==TY_PTR){
      // pointer params
      Type* base = pt->elem;
      emit_c_base(out, base);
      fputs(" *", out);
    } else {
      emit_c_base(out, pt);
    }
  }
  if(fnty->fparam_n==0) fputs("void", out);
  fputs(")", out);
}

static void emit_c_decl(FILE* out, Type* t, const char* name, int as_param){
  if(!t){
    fputs("int64_t", out);
    if(name){ fputc(' ', out); emit_ident(out, name); }
    return;
  }

  // function pointer (ptr to fn)
  if(t->k==TY_PTR && t->elem && t->elem->k==TY_FN){
    emit_fn_ptr(out, t->elem, name);
    return;
  }

  if(t->k==TY_ARRAY && !as_param){
    emit_c_base(out, t->elem);
    if(name){ fputc(' ', out); emit_ident(out, name); }
    fprintf(out, "[%lld]", t->len);
    return;
  }

  int ptr = 0;
  Type* base = t;
  if(base->k==TY_ARRAY && as_param){
    ptr = 1;
    base = base->elem;
  }
  while(base && base->k==TY_PTR && base->elem && base->elem->k!=TY_FN){
    ptr++;
    base = base->elem;
  }
  emit_c_base(out, base);
  for(int i=0;i<ptr;i++) fputs(" *", out);
  if(name){ fputc(' ', out); emit_ident(out, name); }
}

static void emit_structs(FILE* out, Module* M, NameSet* done, TypeEnv* tenv){
  if(!M) return;
  for(int i=0;i<M->struct_n;i++){
    StructDecl* st = M->structs[i];
    if(!st || !st->name || st->len<=0) continue;
    if(!is_c_ident_name(st->name, st->len)) continue;
    if(nameset_has(done, st->name, st->len)) continue;
    nameset_add(done, st->name, st->len);

    fprintf(out, "typedef struct %.*s {\n", st->len, st->name);
    for(int f=0; f<st->field_n; f++){
      fputs("  ", out);
      emit_c_decl(out, st->fields[f].ty, st->fields[f].name, 0);
      fputs(";\n", out);
    }
    fprintf(out, "} %.*s;\n", st->len, st->name);

    Type* t = tenv_find_struct(tenv, st->name, st->len);
    if(t){
      fprintf(out, "_Static_assert(sizeof(%.*s) == %d, \"ABI size mismatch for %.*s\");\n",
              st->len, st->name, type_size(t), st->len, st->name);
      fprintf(out, "_Static_assert(_Alignof(%.*s) == %d, \"ABI align mismatch for %.*s\");\n",
              st->len, st->name, type_align(t), st->len, st->name);
    }
    fputc('\n', out);
  }
}

static void emit_typedefs(FILE* out, Module* M, NameSet* done){
  if(!M) return;
  for(int i=0;i<M->typedef_n;i++){
    TypedefDecl* td = M->typedefs[i];
    if(!td || !td->name || td->len<=0) continue;
    if(!is_c_ident_name(td->name, td->len)) continue;
    if(nameset_has(done, td->name, td->len)) continue;
    nameset_add(done, td->name, td->len);
    fputs("typedef ", out);
    emit_c_decl(out, td->ty, td->name, 0);
    fputs(";\n", out);
  }
  if(M->typedef_n>0) fputc('\n', out);
}

static void emit_enums(FILE* out, Module* M, NameSet* done){
  if(!M) return;
  for(int i=0;i<M->enum_n;i++){
    EnumDecl* ed = M->enums[i];
    if(!ed) continue;
    if(ed->len>0 && !is_c_ident_name(ed->name, ed->len)) continue;
    if(ed->len>0 && !nameset_has(done, ed->name, ed->len)){
      nameset_add(done, ed->name, ed->len);
      fprintf(out, "typedef int64_t %.*s;\n", ed->len, ed->name);
    }
    for(int j=0;j<ed->item_n;j++){
      EnumItem* it = &ed->items[j];
      if(!is_c_ident_name(it->name, it->len)) continue;
      fprintf(out, "#define %.*s ((int64_t)%lld)\n", it->len, it->name, it->val);
    }
    fputc('\n', out);
  }
}

static void emit_globals(FILE* out, Module* M){
  if(!M) return;
  for(int i=0;i<M->global_n;i++){
    struct GlobalVar* g = &M->globals[i];
    if(!g || g->is_static || g->is_extern) continue;
    fputs("extern ", out);
    emit_c_decl(out, g->ty, g->name, 0);
    fputs(";\n", out);
  }
  if(M->global_n>0) fputc('\n', out);
}

static void emit_fns(FILE* out, Module* M){
  if(!M) return;
  for(int i=0;i<M->fn_n;i++){
    FnDecl* fn = M->fns[i];
    if(!fn || fn->is_extern) continue;
    fputs("TEZZ_API ", out);
    emit_c_decl(out, fn->ret, fn->name, 1);
    fputc('(', out);
    for(int p=0;p<fn->arity;p++){
      if(p) fputs(", ", out);
      emit_c_decl(out, fn->ptypes[p], fn->pnames[p], 1);
    }
    if(fn->arity==0) fputs("void", out);
    fputs(");\n", out);
  }
  if(M->fn_n>0) fputc('\n', out);
}

static int pyext_is_ident(const char* s, int len){
  return is_c_ident_name(s, len);
}

static void pyext_sanitize_name(char* out, int cap, const char* name, int len){
  if(!out || cap <= 0) return;
  out[0] = 0;
  if(!name || len <= 0){
    snprintf(out, (size_t)cap, "tezznative_ext");
    return;
  }
  int w = 0;
  char c0 = name[0];
  if(!((c0>='a'&&c0<='z') || (c0>='A'&&c0<='Z') || c0=='_')){
    if(w < cap - 1) out[w++] = 't';
    if(w < cap - 1) out[w++] = 'n';
    if(w < cap - 1) out[w++] = '_';
  }
  for(int i=0;i<len && name[i] && w<cap-1;i++){
    char c = name[i];
    if((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_'){
      out[w++] = c;
    } else {
      out[w++] = '_';
    }
  }
  out[w] = 0;
  if(w == 0) snprintf(out, (size_t)cap, "tezznative_ext");
}

static void pyext_join_path(char* out, size_t cap, const char* dir, const char* file){
  if(!out || cap == 0) return;
  if(!dir || !dir[0]){
    snprintf(out, cap, "%s", file ? file : "");
    return;
  }
  size_t n = strlen(dir);
  const char* sep = (n > 0 && (dir[n-1] == '/' || dir[n-1] == '\\')) ? "" : "/";
  snprintf(out, cap, "%s%s%s", dir, sep, file ? file : "");
}

static void pyext_make_dir(const char* path){
  if(!path || !path[0]) return;
  char buf[512];
  snprintf(buf, sizeof(buf), "%s", path);
  size_t n = strlen(buf);
  while(n > 0 && (buf[n - 1] == '/' || buf[n - 1] == '\\')){
    buf[n - 1] = 0;
    n--;
  }
  if(n == 0) return;
  size_t start = 1;
#ifdef _WIN32
  if(n > 2 && buf[1] == ':' && (buf[2] == '/' || buf[2] == '\\')) start = 3;
#endif
  for(size_t i=start;i<n;i++){
    if(buf[i] == '/' || buf[i] == '\\'){
      char save = buf[i];
      buf[i] = 0;
#ifdef _WIN32
      if(_mkdir(buf) != 0 && errno != EEXIST) die("abi_emit_pyext: cannot create output directory");
#else
      if(mkdir(buf, 0777) != 0 && errno != EEXIST) die("abi_emit_pyext: cannot create output directory");
#endif
      buf[i] = save;
    }
  }
#ifdef _WIN32
  if(_mkdir(buf) != 0 && errno != EEXIST) die("abi_emit_pyext: cannot create output directory");
#else
  if(mkdir(buf, 0777) != 0 && errno != EEXIST) die("abi_emit_pyext: cannot create output directory");
#endif
}

static int pyext_is_u8_ptr(Type* t){
  return t && t->k == TY_PTR && t->elem && t->elem->k == TY_U8;
}

static const char* pyext_type_label(Type* t, int is_ret){
  if(!t) return "i64";
  const char* builtin = type_builtin_name(t);
  if(builtin) return (t->k == TY_VOID && !is_ret) ? "unsupported" : builtin;
  switch(t->k){
    case TY_PTR:
      if(pyext_is_u8_ptr(t)) return "*u8-buffer";
      return "unsupported-pointer";
    default: return "unsupported";
  }
}

static int pyext_type_supported(Type* t, int is_ret){
  if(!t) return 1;
  if(type_is_int(t) || t->k == TY_F64) return 1;
  if(is_ret && t->k == TY_VOID) return 1;
  if(!is_ret && pyext_is_u8_ptr(t)) return 1;
  return 0;
}

static int pyext_fn_supported(FnDecl* fn, char* reason, size_t reason_cap){
  if(reason && reason_cap) reason[0] = 0;
  if(!fn){
    snprintf(reason, reason_cap, "missing function");
    return 0;
  }
  if(fn->is_extern){
    snprintf(reason, reason_cap, "extern functions are not wrapped");
    return 0;
  }
  if(fn->is_unsafe){
    snprintf(reason, reason_cap, "unsafe functions are not wrapped");
    return 0;
  }
  if(fn->len == 4 && strncmp(fn->name, "main", 4) == 0){
    snprintf(reason, reason_cap, "entrypoint main is not wrapped");
    return 0;
  }
  if(!pyext_is_ident(fn->name, fn->len)){
    snprintf(reason, reason_cap, "function name is not a Python/C identifier");
    return 0;
  }
  if(!pyext_type_supported(fn->ret, 1)){
    snprintf(reason, reason_cap, "unsupported return type %s", pyext_type_label(fn->ret, 1));
    return 0;
  }
  for(int i=0;i<fn->arity;i++){
    if(!pyext_type_supported(fn->ptypes[i], 0)){
      snprintf(reason, reason_cap, "unsupported parameter %d type %s", i + 1, pyext_type_label(fn->ptypes[i], 0));
      return 0;
    }
  }
  return 1;
}

static void pyext_emit_c_type(FILE* out, Type* t){
  emit_c_decl(out, t, NULL, 1);
}

static void pyext_emit_header_decl(FILE* out, FnDecl* fn){
  fputs("extern ", out);
  emit_c_decl(out, fn->ret, fn->name, 1);
  fputc('(', out);
  for(int p=0;p<fn->arity;p++){
    if(p) fputs(", ", out);
    emit_c_decl(out, fn->ptypes[p], fn->pnames[p], 1);
  }
  if(fn->arity == 0) fputs("void", out);
  fputs(");\n", out);
}

static void pyext_emit_arg_tuple_format(FILE* out, int n){
  fputc('"', out);
  for(int i=0;i<n;i++) fputc('O', out);
  fputc('"', out);
}

static void pyext_emit_release_buffers(FILE* out, FnDecl* fn){
  for(int p=0;p<fn->arity;p++){
    if(pyext_is_u8_ptr(fn->ptypes[p])){
      fprintf(out, "  if(buf%d_ready) PyBuffer_Release(&buf%d);\n", p, p);
    }
  }
}

static long long pyext_signed_min(Type* t){
  int bits = type_int_bits(t);
  if(bits >= 64) return (long long)(~0ull << 63);
  return -(1LL << (bits - 1));
}

static unsigned long long pyext_signed_max(Type* t){
  int bits = type_int_bits(t);
  if(bits >= 64) return 0x7FFFFFFFFFFFFFFFull;
  return (1ULL << (bits - 1)) - 1ULL;
}

static unsigned long long pyext_unsigned_max(Type* t){
  int bits = type_int_bits(t);
  if(bits >= 64) return 0xFFFFFFFFFFFFFFFFull;
  return (1ULL << bits) - 1ULL;
}

static void pyext_emit_wrapper(FILE* out, FnDecl* fn){
  fprintf(out, "static PyObject* py_%.*s(PyObject* self, PyObject* args){\n", fn->len, fn->name);
  fputs("  (void)self;\n", out);
  for(int p=0;p<fn->arity;p++){
    fprintf(out, "  PyObject* arg%d = NULL;\n", p);
  }
  for(int p=0;p<fn->arity;p++){
    if(pyext_is_u8_ptr(fn->ptypes[p])){
      fprintf(out, "  Py_buffer buf%d;\n  memset(&buf%d, 0, sizeof(buf%d));\n  int buf%d_ready = 0;\n", p, p, p, p);
    }
  }
  if(fn->arity > 0){
    fputs("  if(!PyArg_ParseTuple(args, ", out);
    pyext_emit_arg_tuple_format(out, fn->arity);
    for(int p=0;p<fn->arity;p++) fprintf(out, ", &arg%d", p);
    fputs(")) return NULL;\n", out);
  } else {
    fputs("  if(!PyArg_ParseTuple(args, \"\")) return NULL;\n", out);
  }
  for(int p=0;p<fn->arity;p++){
    Type* t = fn->ptypes[p];
    if(!t || type_is_signed_int(t)){
      fprintf(out, "  long long v%d_ll = PyLong_AsLongLong(arg%d);\n", p, p);
      fprintf(out, "  if(PyErr_Occurred()) goto error;\n");
      if(t && type_int_bits(t) < 64){
        fprintf(out, "  if(v%d_ll < %lldLL || v%d_ll > %lluLL){ PyErr_SetString(PyExc_ValueError, \"%s argument out of range\"); goto error; }\n",
                p, pyext_signed_min(t), p, pyext_signed_max(t), pyext_type_label(t, 0));
      }
      fputs("  ", out);
      pyext_emit_c_type(out, t ? t : NULL);
      fprintf(out, " v%d = (", p);
      pyext_emit_c_type(out, t ? t : NULL);
      fprintf(out, ")v%d_ll;\n", p);
    } else if(type_is_unsigned_int(t)){
      fprintf(out, "  unsigned long long v%d_ull = PyLong_AsUnsignedLongLong(arg%d);\n", p, p);
      fprintf(out, "  if(PyErr_Occurred()) goto error;\n");
      if(type_int_bits(t) < 64){
        fprintf(out, "  if(v%d_ull > %lluULL){ PyErr_SetString(PyExc_ValueError, \"%s argument out of range\"); goto error; }\n",
                p, pyext_unsigned_max(t), pyext_type_label(t, 0));
      }
      fputs("  ", out);
      pyext_emit_c_type(out, t);
      fprintf(out, " v%d = (", p);
      pyext_emit_c_type(out, t);
      fprintf(out, ")v%d_ull;\n", p);
    } else if(t->k == TY_F64){
      fprintf(out, "  double v%d = PyFloat_AsDouble(arg%d);\n", p, p);
      fprintf(out, "  if(PyErr_Occurred()) goto error;\n");
    } else if(pyext_is_u8_ptr(t)){
      fprintf(out, "  if(PyObject_GetBuffer(arg%d, &buf%d, PyBUF_CONTIG_RO) < 0) goto error;\n", p, p);
      fprintf(out, "  buf%d_ready = 1;\n", p);
      fprintf(out, "  uint8_t* v%d = (uint8_t*)buf%d.buf;\n", p, p);
    }
  }

  fputs("  ", out);
  if(fn->ret && fn->ret->k != TY_VOID){
    pyext_emit_c_type(out, fn->ret);
    fputs(" result = ", out);
  }
  fprintf(out, "%.*s(", fn->len, fn->name);
  for(int p=0;p<fn->arity;p++){
    if(p) fputs(", ", out);
    fprintf(out, "v%d", p);
  }
  fputs(");\n", out);
  pyext_emit_release_buffers(out, fn);

  if(!fn->ret || type_is_signed_int(fn->ret)){
    fputs("  return PyLong_FromLongLong((long long)result);\n", out);
  } else if(type_is_unsigned_int(fn->ret)){
    fputs("  return PyLong_FromUnsignedLongLong((unsigned long long)result);\n", out);
  } else if(fn->ret->k == TY_F64){
    fputs("  return PyFloat_FromDouble((double)result);\n", out);
  } else if(fn->ret->k == TY_VOID){
    fputs("  Py_RETURN_NONE;\n", out);
  }
  fputs("error:\n", out);
  pyext_emit_release_buffers(out, fn);
  fputs("  return NULL;\n", out);
  fputs("}\n\n", out);
}

static void pyext_emit_type_list(FILE* out, FnDecl* fn){
  for(int p=0;p<fn->arity;p++){
    if(p) fputc(',', out);
    fputs(pyext_type_label(fn->ptypes[p], 0), out);
  }
}

void abi_emit_pyext(struct ModuleGraph* G, TypeEnv* tenv, struct Module* root, const char* out_dir, const char* module_name){
  (void)G;
  (void)tenv;
  if(!root || !out_dir) die("abi_emit_pyext: invalid args");
  pyext_make_dir(out_dir);

  char mod[128];
  if(module_name && module_name[0]){
    pyext_sanitize_name(mod, (int)sizeof(mod), module_name, (int)strlen(module_name));
  } else {
    pyext_sanitize_name(mod, (int)sizeof(mod), root->mname, root->mlen);
  }

  char c_name[192];
  char h_name[192];
  snprintf(c_name, sizeof(c_name), "%s_pyext.c", mod);
  snprintf(h_name, sizeof(h_name), "%s.h", mod);

  char c_path[512], h_path[512], setup_path[512], readme_path[512], manifest_path[512];
  pyext_join_path(c_path, sizeof(c_path), out_dir, c_name);
  pyext_join_path(h_path, sizeof(h_path), out_dir, h_name);
  pyext_join_path(setup_path, sizeof(setup_path), out_dir, "setup.py");
  pyext_join_path(readme_path, sizeof(readme_path), out_dir, "README.md");
  pyext_join_path(manifest_path, sizeof(manifest_path), out_dir, "pyext_manifest.tnx");

  FILE* h = fopen(h_path, "wb");
  if(!h) die("abi_emit_pyext: cannot open header output");
  fprintf(h, "/* Generated by tezzc pyext for %s */\n", mod);
  fputs("#pragma once\n#include <stdint.h>\n#include <stddef.h>\n\n", h);
  fputs("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n", h);
  int wrapped = 0;
  int skipped = 0;
  char reason[160];
  for(int i=0;i<root->fn_n;i++){
    FnDecl* fn = root->fns[i];
    if(pyext_fn_supported(fn, reason, sizeof(reason))){
      pyext_emit_header_decl(h, fn);
      wrapped++;
    } else {
      skipped++;
    }
  }
  fputs("\n#ifdef __cplusplus\n}\n#endif\n", h);
  fclose(h);
  if(wrapped <= 0) die("abi_emit_pyext: no supported functions to wrap");

  FILE* c = fopen(c_path, "wb");
  if(!c) die("abi_emit_pyext: cannot open C output");
  fprintf(c, "/* Generated by tezzc pyext for %s. */\n", mod);
  fputs("#define PY_SSIZE_T_CLEAN\n#include <Python.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <string.h>\n", c);
  fprintf(c, "#include \"%s\"\n\n", h_name);
  for(int i=0;i<root->fn_n;i++){
    FnDecl* fn = root->fns[i];
    if(pyext_fn_supported(fn, reason, sizeof(reason))){
      pyext_emit_wrapper(c, fn);
    }
  }
  fprintf(c, "static PyMethodDef %s_methods[] = {\n", mod);
  for(int i=0;i<root->fn_n;i++){
    FnDecl* fn = root->fns[i];
    if(pyext_fn_supported(fn, reason, sizeof(reason))){
      fprintf(c, "  {\"%.*s\", py_%.*s, METH_VARARGS, \"TezzNative function %.*s.\"},\n",
              fn->len, fn->name, fn->len, fn->name, fn->len, fn->name);
    }
  }
  fputs("  {NULL, NULL, 0, NULL}\n};\n\n", c);
  fprintf(c, "static struct PyModuleDef %s_module = {\n", mod);
  fputs("  PyModuleDef_HEAD_INIT,\n", c);
  fprintf(c, "  \"%s\",\n", mod);
  fputs("  \"TezzNative CPython extension generated by tezzc pyext.\",\n", c);
  fputs("  -1,\n", c);
  fprintf(c, "  %s_methods\n};\n\n", mod);
  fprintf(c, "PyMODINIT_FUNC PyInit_%s(void){\n  return PyModule_Create(&%s_module);\n}\n", mod, mod);
  fclose(c);

  FILE* setup = fopen(setup_path, "wb");
  if(!setup) die("abi_emit_pyext: cannot open setup.py output");
  fprintf(setup,
          "from setuptools import Extension, setup\n"
          "import os\n\n"
          "objects = [p for p in os.environ.get('TEZZ_NATIVE_OBJECTS', '').split(os.pathsep) if p]\n"
          "libraries = [p for p in os.environ.get('TEZZ_NATIVE_LIBRARIES', '').split(os.pathsep) if p]\n"
          "setup(\n"
          "    name='%s',\n"
          "    version='0.1.0',\n"
          "    ext_modules=[Extension('%s', sources=['%s'], extra_objects=objects, libraries=libraries)],\n"
          ")\n",
          mod, mod, c_name);
  fclose(setup);

  FILE* readme = fopen(readme_path, "wb");
  if(!readme) die("abi_emit_pyext: cannot open README output");
  fprintf(readme,
          "# %s Python Extension\n\n"
          "Generated by `tezzc pyext`.\n\n"
          "## Ownership Rules\n\n"
          "- `i64`, `u8`, and `f64` parameters map to Python `int`/`float` values.\n"
          "- `*u8` parameters map to borrowed contiguous Python buffer views for the duration of the call.\n"
          "- Returned `i64`, `u8`, and `f64` values become Python objects; `void` returns `None`.\n"
          "- Pointer returns and structs are intentionally not wrapped by this starter bridge.\n\n"
          "## Build\n\n"
          "Build or provide the TezzNative ABI object/library for the same source, then run:\n\n"
          "```bash\n"
          "TEZZ_NATIVE_OBJECTS=/path/to/module.o python -m pip install .\n"
          "```\n\n"
          "The generated `setup.py` also accepts `TEZZ_NATIVE_LIBRARIES` as an OS-path-separated list.\n",
          mod);
  fclose(readme);

  FILE* manifest = fopen(manifest_path, "wb");
  if(!manifest) die("abi_emit_pyext: cannot open manifest output");
  fprintf(manifest, "schema=tezznative.pyext.v1\nmodule=%s\nsource=%s\nwrapped=%d\nskipped=%d\n", mod, root->path ? root->path : "", wrapped, skipped);
  for(int i=0;i<root->fn_n;i++){
    FnDecl* fn = root->fns[i];
    if(!fn) continue;
    if(pyext_fn_supported(fn, reason, sizeof(reason))){
      fprintf(manifest, "fn.%.*s=wrapped ret=%s params=", fn->len, fn->name, pyext_type_label(fn->ret, 1));
      pyext_emit_type_list(manifest, fn);
      fputc('\n', manifest);
    } else {
      fprintf(manifest, "fn.%.*s=skipped reason=%s\n", fn->len, fn->name, reason);
    }
  }
  fclose(manifest);
}

static void emit_tnx_str(FILE* out, const char* s, int len){
  fputc('"', out);
  for(int i=0;i<len;i++){
    char c = s[i];
    if(c == '\"') fputs("\\\"", out);
    else if(c == '\\') fputs("\\\\", out);
    else if(c == '\n') fputs("\\n", out);
    else if(c == '\r') fputs("\\r", out);
    else if(c == '\t') fputs("\\t", out);
    else fputc(c, out);
  }
  fputc('"', out);
}

static const char* ty_kind_name(Type* t){
  if(!t) return "i64";
  const char* builtin = type_builtin_name(t);
  if(builtin) return builtin;
  switch(t->k){
    case TY_PTR: return "ptr";
    case TY_ARRAY: return "array";
    case TY_STRUCT: return "struct";
    case TY_FN: return "fn";
    default: return "unknown";
  }
}

static void emit_tnx_type(FILE* out, Type* t){
  const char* k = ty_kind_name(t);
  fputs("{\"kind\":", out);
  emit_tnx_str(out, k, (int)strlen(k));
  if(t && (t->k==TY_PTR || t->k==TY_ARRAY)){
    fputs(",\"elem\":", out);
    emit_tnx_type(out, t->elem);
  }
  if(t && t->k==TY_ARRAY){
    fprintf(out, ",\"len\":%lld", t->len);
  }
  if(t && t->k==TY_STRUCT && t->sname){
    fputs(",\"name\":", out);
    emit_tnx_str(out, t->sname, t->sname_len);
  }
  fputc('}', out);
}

static Field* abi_find_layout_field(Type* st, const char* name, int len, int ordinal){
  if(st && st->k == TY_STRUCT && st->fields){
    for(int i=0; i<st->field_n; i++){
      Field* f = &st->fields[i];
      if(f->name_len == len && strncmp(f->name, name, (size_t)len) == 0){
        return f;
      }
    }
    if(ordinal >= 0 && ordinal < st->field_n){
      return &st->fields[ordinal];
    }
  }
  return NULL;
}

void abi_emit_tnx(struct ModuleGraph* G, TypeEnv* tenv, struct Module* root, const char* out_path){
  if(!root || !out_path) die("abi_emit_tnx: invalid args");
  FILE* out = fopen(out_path, "wb");
  if(!out) die("abi_emit_tnx: cannot open output");

  NameSet struct_set = {0};

  fputs("{\"schema\":\"tezznative.abi.v1\",\"structs\":[", out);
  int first = 1;
  for(int mi=0; mi<(G ? G->mod_n : 0); mi++){
    Module* M = G->mods[mi];
    if(!M) continue;
    for(int i=0;i<M->struct_n;i++){
      StructDecl* st = M->structs[i];
      if(!st || !st->name || st->len<=0) continue;
      if(!is_c_ident_name(st->name, st->len)) continue;
      if(nameset_has(&struct_set, st->name, st->len)) continue;
      nameset_add(&struct_set, st->name, st->len);
      Type* t = tenv_find_struct(tenv, st->name, st->len);
      if(!t || t->incomplete) continue;
      if(!first) fputc(',', out);
      first = 0;
      fputs("{\"name\":", out);
      emit_tnx_str(out, st->name, st->len);
      fprintf(out, ",\"size\":%d,\"align\":%d,\"fields\":[", type_size(t), type_align(t));
      for(int f=0; f<st->field_n; f++){
        Field* decl_field = &st->fields[f];
        Field* layout_field = abi_find_layout_field(t, decl_field->name, decl_field->name_len, f);
        Type* field_ty = layout_field && layout_field->ty ? layout_field->ty : decl_field->ty;
        int field_off = layout_field ? layout_field->off : decl_field->off;
        if(f) fputc(',', out);
        fputs("{\"name\":", out);
        emit_tnx_str(out, decl_field->name, decl_field->name_len);
        fprintf(out, ",\"off\":%d,\"size\":%d,\"align\":%d,\"type\":",
                field_off, type_size(field_ty), type_align(field_ty));
        emit_tnx_type(out, field_ty);
        fputc('}', out);
      }
      fputs("]}", out);
    }
  }
  fputs("],\"globals\":[", out);
  int gfirst = 1;
  for(int i=0;i<root->global_n;i++){
    struct GlobalVar* g = &root->globals[i];
    if(!g || g->is_static) continue;
    if(!gfirst) fputc(',', out);
    gfirst = 0;
    fputs("{\"name\":", out);
    emit_tnx_str(out, g->name, g->len);
    fputs(",\"type\":", out);
    emit_tnx_type(out, g->ty);
    fputc('}', out);
  }
  fputs("],\"fns\":[", out);
  int ffirst = 1;
  for(int i=0;i<root->fn_n;i++){
    FnDecl* fn = root->fns[i];
    if(!fn) continue;
    if(!ffirst) fputc(',', out);
    ffirst = 0;
    fputs("{\"name\":", out);
    emit_tnx_str(out, fn->name, fn->len);
    fputs(",\"extern\":", out);
    fputs(fn->is_extern ? "true" : "false", out);
    fputs(",\"ret\":", out);
    emit_tnx_type(out, fn->ret);
    fputs(",\"params\":[", out);
    for(int p=0;p<fn->arity;p++){
      if(p) fputc(',', out);
      emit_tnx_type(out, fn->ptypes[p]);
    }
    fputs("]}", out);
  }
  fputs("]}", out);
  fclose(out);

  nameset_free(&struct_set);
}

void abi_emit_header(struct ModuleGraph* G, TypeEnv* tenv, struct Module* root, const char* out_path){
  if(!root || !out_path) die("abi_emit_header: invalid args");
  FILE* out = fopen(out_path, "wb");
  if(!out) die("abi_emit_header: cannot open output");

  fputs("// Generated by tezzc cheader\n", out);
  fputs("#pragma once\n", out);
  fputs("#include <stdint.h>\n", out);
  fputs("#include <stddef.h>\n\n", out);
  fputs("#ifndef TEZZ_API\n", out);
  fputs("#ifdef _WIN32\n", out);
  fputs("#define TEZZ_API __declspec(dllimport)\n", out);
  fputs("#else\n", out);
  fputs("#define TEZZ_API\n", out);
  fputs("#endif\n", out);
  fputs("#endif\n\n", out);

  fputs("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n", out);

  NameSet struct_set = {0};
  NameSet typedef_set = {0};
  NameSet enum_set = {0};

  // forward declares for all structs
  for(int mi=0; mi<(G ? G->mod_n : 0); mi++){
    Module* M = G->mods[mi];
    if(!M) continue;
    for(int i=0;i<M->struct_n;i++){
      StructDecl* st = M->structs[i];
      if(!st || !st->name || st->len<=0) continue;
      if(nameset_has(&struct_set, st->name, st->len)) continue;
      nameset_add(&struct_set, st->name, st->len);
      fprintf(out, "typedef struct %.*s %.*s;\n", st->len, st->name, st->len, st->name);
    }
  }
  if(struct_set.n>0) fputc('\n', out);

  nameset_free(&struct_set);
  struct_set.name = NULL; struct_set.len = NULL; struct_set.n = 0; struct_set.cap = 0;

  // typedefs + enums + structs are emitted from root only.
  // This keeps C interop headers scoped to the package API surface and
  // avoids leaking imported stdlib/internal declarations.
  emit_typedefs(out, root, &typedef_set);
  emit_enums(out, root, &enum_set);
  emit_structs(out, root, &struct_set, tenv);

  // globals + fns from root module only
  emit_globals(out, root);
  emit_fns(out, root);

  fputs("#ifdef __cplusplus\n}\n#endif\n", out);
  fclose(out);

  nameset_free(&struct_set);
  nameset_free(&typedef_set);
  nameset_free(&enum_set);
}
