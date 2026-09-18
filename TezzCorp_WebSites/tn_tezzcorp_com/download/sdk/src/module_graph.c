// src/module_graph.c
#include "vm.h"
#include "util.h"
#include "parser.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

static char* xstrdup(const char* s){
  if(!s) return NULL;
  size_t n = strlen(s);
  char* p = (char*)malloc(n + 1);
  if(!p) die("out of memory");
  memcpy(p, s, n + 1);
  return p;
}

static char* xstrndup(const char* s, size_t n){
  char* p = (char*)malloc(n + 1);
  if(!p) die("out of memory");
  memcpy(p, s, n);
  p[n] = 0;
  return p;
}

static int file_exists(const char* path){
  FILE* f = fopen(path, "rb");
  if(f){ fclose(f); return 1; }
  return 0;
}

static int has_sep(const char* s){
  if(!s) return 0;
  for(const char* p=s; *p; p++){
    if(*p=='/' || *p=='\\') return 1;
  }
  return 0;
}

static int has_ext_tn(const char* s){
  size_t n = strlen(s);
  if(n < 3) return 0;
  return (s[n-3]=='.' && s[n-2]=='t' && s[n-1]=='n');
}

static const char* env_nonempty(const char* key){
  const char* v = getenv(key);
  if(v && *v) return v;
  return NULL;
}

static char* path_join2(const char* a, const char* b){
  if(!a) return xstrdup(b ? b : "");
  if(!b) return xstrdup(a);
  size_t na = strlen(a);
  size_t nb = strlen(b);
  int need_sep = (na>0 && a[na-1] != '/' && a[na-1] != '\\');
  char* out = (char*)malloc(na + nb + (need_sep?2:1));
  if(!out) die("out of memory");
  memcpy(out, a, na);
  size_t off = na;
  if(need_sep) out[off++] = '/';
  memcpy(out + off, b, nb);
  out[off + nb] = 0;
  return out;
}

static char* ensure_tn_ext(const char* s){
  if(has_ext_tn(s)) return xstrdup(s);
  size_t n = strlen(s);
  char* out = (char*)malloc(n + 4);
  if(!out) die("out of memory");
  memcpy(out, s, n);
  out[n+0] = '.';
  out[n+1] = 't';
  out[n+2] = 'n';
  out[n+3] = 0;
  return out;
}

static char* dots_to_slash(const char* s){
  size_t n = strlen(s);
  char* out = (char*)malloc(n + 1);
  if(!out) die("out of memory");
  for(size_t i=0;i<n;i++){
    out[i] = (s[i]=='.') ? '/' : s[i];
  }
  out[n]=0;
  return out;
}

static char* dirname_of(const char* path){
  if(!path) return xstrdup(".");
  const char* last = path;
  for(const char* p=path; *p; p++){
    if(*p=='/' || *p=='\\') last = p;
  }
  if(last == path) return xstrdup(".");
  int n = (int)(last - path);
  char* out = (char*)malloc((size_t)n + 1);
  if(!out) die("out of memory");
  memcpy(out, path, (size_t)n);
  out[n] = 0;
  return out;
}

static int find_root_dir(const char* base_dir, char* out, int cap){
  if(!base_dir || !out || cap <= 0) return 0;
  char tmp[1024];
  strncpy(tmp, base_dir, sizeof(tmp)-1);
  tmp[sizeof(tmp)-1]=0;
  while(1){
    char* test = path_join2(tmp, "tezz.mod");
    int ok = file_exists(test);
    free(test);
    if(ok){
      strncpy(out, tmp, (size_t)cap-1);
      out[cap-1]=0;
      return 1;
    }
    int n = (int)strlen(tmp);
    int i = n-1;
    for(; i>=0; i--){
      if(tmp[i]=='/' || tmp[i]=='\\') break;
    }
    if(i <= 0) break;
    tmp[i] = 0;
  }
  return 0;
}

static void mg_add_dep(ModuleGraph* G, const char* name, const char* ver){
  if(!G || !name || !ver) return;
  if(G->dep_n == G->dep_cap){
    G->dep_cap = G->dep_cap ? G->dep_cap * 2 : 16;
    G->dep_names = (const char**)realloc(G->dep_names, sizeof(char*)*(size_t)G->dep_cap);
    G->dep_versions = (const char**)realloc(G->dep_versions, sizeof(char*)*(size_t)G->dep_cap);
    if(!G->dep_names || !G->dep_versions) die("out of memory");
  }
  G->dep_names[G->dep_n] = name;
  G->dep_versions[G->dep_n] = ver;
  G->dep_n++;
}

static void mg_add_lock(ModuleGraph* G, const char* name, const char* ver, const char* url){
  if(!G || !name || !ver) return;
  if(G->lock_n == G->lock_cap){
    G->lock_cap = G->lock_cap ? G->lock_cap * 2 : 32;
    G->lock_names = (const char**)realloc(G->lock_names, sizeof(char*)*(size_t)G->lock_cap);
    G->lock_versions = (const char**)realloc(G->lock_versions, sizeof(char*)*(size_t)G->lock_cap);
    G->lock_urls = (const char**)realloc(G->lock_urls, sizeof(char*)*(size_t)G->lock_cap);
    if(!G->lock_names || !G->lock_versions || !G->lock_urls) die("out of memory");
  }
  G->lock_names[G->lock_n] = name;
  G->lock_versions[G->lock_n] = ver;
  G->lock_urls[G->lock_n] = url;
  G->lock_n++;
}

static const char* mg_find_dep_ver(ModuleGraph* G, const char* name){
  if(!G || !name) return NULL;
  for(int i=0;i<G->dep_n;i++){
    if(strcmp(G->dep_names[i], name)==0) return G->dep_versions[i];
  }
  return NULL;
}

static int mg_lock_has(ModuleGraph* G, const char* name, const char* ver){
  if(!G || !name || !ver) return 0;
  for(int i=0;i<G->lock_n;i++){
    if(strcmp(G->lock_names[i], name)==0 && strcmp(G->lock_versions[i], ver)==0) return 1;
  }
  return 0;
}

static void mg_parse_mod(ModuleGraph* G, const char* mod_path){
  char* txt = read_file_cstr(mod_path);
  const char* p = txt;
  while(*p){
    while(*p==' ' || *p=='\t' || *p=='\r' || *p=='\n') p++;
    if(!*p) break;
    const char* line = p;
    while(*p && *p!='\n') p++;
    const char* end = p;
    if(*p=='\n') p++;
    while(line < end && (*line==' ' || *line=='\t')) line++;
    if(line>=end) continue;
    if(strncmp(line, "module_root", 11)==0){
      const char* q = line + 11;
      while(q<end && (*q==' ' || *q=='\t')) q++;
      if(q<end && *q=='=') q++;
      while(q<end && (*q==' ' || *q=='\t')) q++;
      const char* v = q;
      while(v<end && *v!='\r' && *v!='\t' && *v!=' ') v++;
      if(v>q){
        if(G->module_root) free(G->module_root);
        G->module_root = xstrndup(q, (size_t)(v - q));
      }
    } else if(strncmp(line, "dep.", 4)==0){
      const char* q = line + 4;
      const char* name_start = q;
      while(q<end && *q!=' ' && *q!='\t' && *q!='=') q++;
      const char* name_end = q;
      while(q<end && (*q==' ' || *q=='\t')) q++;
      if(q<end && *q=='=') q++;
      while(q<end && (*q==' ' || *q=='\t')) q++;
      const char* ver_start = q;
      while(q<end && *q!=' ' && *q!='\t' && *q!='\r') q++;
      const char* ver_end = q;
      if(name_end>name_start && ver_end>ver_start){
        char* n = xstrndup(name_start, (size_t)(name_end - name_start));
        char* v = xstrndup(ver_start, (size_t)(ver_end - ver_start));
        mg_add_dep(G, n, v);
      }
    }
  }
  free(txt);
}

static void mg_parse_lock(ModuleGraph* G, const char* lock_path){
  char* txt = read_file_cstr(lock_path);
  const char* p = txt;
  while(*p){
    while(*p==' ' || *p=='\t' || *p=='\r' || *p=='\n') p++;
    if(!*p) break;
    const char* line = p;
    while(*p && *p!='\n') p++;
    const char* end = p;
    if(*p=='\n') p++;
    while(line < end && (*line==' ' || *line=='\t')) line++;
    if(line>=end) continue;
    const char* tok_end = line;
    while(tok_end<end && *tok_end!=' ' && *tok_end!='\t') tok_end++;
    const char* url_start = tok_end;
    while(url_start<end && (*url_start==' ' || *url_start=='\t')) url_start++;
    const char* url_end = end;
    while(url_end>url_start && (url_end[-1]=='\r' || url_end[-1]=='\t' || url_end[-1]==' ')) url_end--;
    if(tok_end>line){
      const char* at = line;
      while(at<tok_end && *at!='@') at++;
      if(at<tok_end){
        char* name = xstrndup(line, (size_t)(at - line));
        char* ver = xstrndup(at + 1, (size_t)(tok_end - at - 1));
        char* url = (url_end>url_start) ? xstrndup(url_start, (size_t)(url_end - url_start)) : xstrdup("");
        mg_add_lock(G, name, ver, url);
      }
    }
  }
  free(txt);
}

static void mg_init_root(ModuleGraph* G, const char* base_dir){
  if(G->root_inited) return;
  G->root_inited = 1;
  char root_buf[1024];
  if(find_root_dir(base_dir, root_buf, (int)sizeof(root_buf)) ||
     find_root_dir(".", root_buf, (int)sizeof(root_buf))){
    G->root_dir = xstrdup(root_buf);
    char* mod_path = path_join2(root_buf, "tezz.mod");
    if(file_exists(mod_path)){
      mg_parse_mod(G, mod_path);
      G->has_manifest = 1;
    }
    free(mod_path);
    char* lock_path = path_join2(root_buf, "tezz.lock");
    if(file_exists(lock_path)){
      mg_parse_lock(G, lock_path);
      G->has_lock = 1;
    }
    free(lock_path);
  } else {
    G->root_dir = xstrdup(base_dir ? base_dir : ".");
  }
  if(!G->module_root) G->module_root = xstrdup("lib");
}

void mg_init(ModuleGraph* G){
  if(!G) return;
  memset(G, 0, sizeof(*G));
}

void mg_free(ModuleGraph* G){
  if(!G) return;
  // Module lifetime is managed by the compiler; no deep free here.
  free(G->mods);
  free(G->root_dir);
  free(G->module_root);
  for(int i=0;i<G->dep_n;i++){
    free((char*)G->dep_names[i]);
    free((char*)G->dep_versions[i]);
  }
  free(G->dep_names);
  free(G->dep_versions);
  for(int i=0;i<G->lock_n;i++){
    free((char*)G->lock_names[i]);
    free((char*)G->lock_versions[i]);
    free((char*)G->lock_urls[i]);
  }
  free(G->lock_names);
  free(G->lock_versions);
  free(G->lock_urls);
  memset(G, 0, sizeof(*G));
}

static const char* import_base_name(const char* import_path){
  if(!import_path) return NULL;
  const char* p = import_path;
  while(*p && *p!='.' && *p!='/' && *p!='\\') p++;
  return xstrndup(import_path, (size_t)(p - import_path));
}

static int split_import_ver(const char* import_path, char** out_name, char** out_ver){
  if(!import_path || !out_name || !out_ver) return 0;
  const char* at = strrchr(import_path, '@');
  if(!at) return 0;
  if(at == import_path || !at[1]) return -1;
  *out_name = xstrndup(import_path, (size_t)(at - import_path));
  *out_ver = xstrdup(at + 1);
  return 1;
}

static char* cache_module_path(const char* root, const char* dep_name, const char* dep_ver){
  if(!root || !dep_name || !dep_ver) return NULL;
  if(!*dep_name || !*dep_ver) return NULL;
  size_t nn = strlen(dep_name);
  size_t nv = strlen(dep_ver);
  char* file = (char*)malloc(nn + 1 + nv + 3 + 1); // name-ver.tn\0
  if(!file) die("out of memory");
  memcpy(file, dep_name, nn);
  file[nn] = '-';
  memcpy(file + nn + 1, dep_ver, nv);
  file[nn + 1 + nv + 0] = '.';
  file[nn + 1 + nv + 1] = 't';
  file[nn + 1 + nv + 2] = 'n';
  file[nn + 1 + nv + 3] = 0;

  char* c0 = path_join2(root, ".tezz/cache");
  char* c1 = path_join2(c0, file);
  free(c0);
  free(file);
  return c1;
}

static char* resolve_import_path(ModuleGraph* G, const char* base_dir, const char* import_path, const char* dep_name, const char* dep_ver){
  if(!import_path) return NULL;
  int is_rel = (import_path[0]=='.' || has_sep(import_path));
  if(is_rel){
    char* tmp = ensure_tn_ext(import_path);
    char* out = path_join2(base_dir ? base_dir : ".", tmp);
    free(tmp);
    return out;
  }
  char* mapped = dots_to_slash(import_path);
  char* with_ext = ensure_tn_ext(mapped);
  free(mapped);
  char* root = G->root_dir ? G->root_dir : (char*)".";
  char* mid = G->module_root ? G->module_root : (char*)"lib";
  char* p1 = path_join2(root, mid);
  char* p2 = path_join2(p1, with_ext);
  free(p1);
  if(file_exists(p2)){
    free(with_ext);
    return p2;
  }

  /* TEZZ_SDK_ROOT / TEZZ_SDK_DIR env override */
  const char* sdk_root = env_nonempty("TEZZ_SDK_ROOT");
  if(!sdk_root) sdk_root = env_nonempty("TEZZ_SDK_DIR");
  if(sdk_root){
    char* s1 = path_join2(sdk_root, "lib");
    char* s2 = path_join2(s1, with_ext);
    free(s1);
    if(file_exists(s2)){
      free(with_ext); free(p2);
      return s2;
    }
    free(s2);
    /* sdk_root itself might already be the lib dir */
    char* s3 = path_join2(sdk_root, with_ext);
    if(file_exists(s3)){
      free(with_ext); free(p2);
      return s3;
    }
    free(s3);
  }

  /* TEZZ_HOME env var — set by installer to install root */
  const char* tezz_home = env_nonempty("TEZZ_HOME");
  if(tezz_home){
    char* h1 = path_join2(tezz_home, "lib");
    char* h2 = path_join2(h1, with_ext);
    free(h1);
    if(file_exists(h2)){
      free(with_ext); free(p2);
      return h2;
    }
    free(h2);
  }

  /* Project cache lane: .tezz/cache/<dep>-<ver>.tn */
  if(dep_name && dep_ver){
    char* cmod = cache_module_path(root, dep_name, dep_ver);
    if(cmod && file_exists(cmod)){
      free(with_ext); free(p2);
      return cmod;
    }
    free(cmod);
  }

  /* Standard install locations — checked in priority order */
  {
    /* 1) beside tezzc.exe itself (most reliable after PATH install) */
#ifdef _WIN32
    {
      char self[4096] = {0};
      DWORD n = GetModuleFileNameA(NULL, self, (DWORD)sizeof(self)-1);
      if(n>0){
        char* sep = strrchr(self, '\\');
        if(!sep) sep = strrchr(self, '/');
        if(sep){ *sep = 0; }
        char* e1 = path_join2(self, "lib");
        char* e2 = path_join2(e1, with_ext);
        free(e1);
        if(file_exists(e2)){
          free(with_ext); free(p2);
          return e2;
        }
        free(e2);
      }
    }
#endif
    /* 2) C:\Program Files\TezzNative\lib  (Windows installer default) */
    const char* pf = env_nonempty("ProgramFiles");
    if(!pf) pf = env_nonempty("PROGRAMFILES");
    if(!pf) pf = "C:\\Program Files";
    if(pf){
      char* pfa = path_join2(pf, "TezzNative\\lib");
      char* pfb = path_join2(pfa, with_ext);
      free(pfa);
      if(file_exists(pfb)){
        free(with_ext); free(p2);
        return pfb;
      }
      free(pfb);
      /* legacy: TezzNative/sdk/lib */
      char* pfc = path_join2(pf, "TezzNative\\sdk\\lib");
      char* pfd = path_join2(pfc, with_ext);
      free(pfc);
      if(file_exists(pfd)){
        free(with_ext); free(p2);
        return pfd;
      }
      free(pfd);
    }
    /* 3) ~/TezzNative/lib  (user-level install) */
    const char* home = env_nonempty("USERPROFILE");
    if(!home) home = env_nonempty("HOME");
    if(home){
      char* ua = path_join2(home, "TezzNative\\lib");
      char* ub = path_join2(ua, with_ext);
      free(ua);
      if(file_exists(ub)){
        free(with_ext); free(p2);
        return ub;
      }
      free(ub);
    }
  }

  free(with_ext);
  return p2;
}

static void normalize_path_inplace(char* p){
  if(!p) return;
  for(char* s=p; *s; s++){
    if(*s=='\\') *s = '/';
  }

  char* in = p;
  char* out = p;
  char prefix[4] = {0,0,0,0};
  int prelen = 0;

  // Windows drive prefix (e.g. C:/)
  if(((in[0]>='A' && in[0]<='Z') || (in[0]>='a' && in[0]<='z')) && in[1]==':'){
    prefix[0] = in[0];
    prefix[1] = ':';
    prelen = 2;
    in += 2;
    if(*in=='/'){
      prefix[2] = '/';
      prelen = 3;
      in += 1;
    }
  } else if(*in=='/'){
    prefix[0] = '/';
    prelen = 1;
    in += 1;
  }

  if(prelen){
    memcpy(out, prefix, (size_t)prelen);
    out += prelen;
  }

  char* segs[256];
  int sp = 0;
  char* root_out = p + prelen;

  while(*in){
    while(*in=='/') in++;
    if(!*in) break;
    char* seg = in;
    while(*in && *in!='/') in++;
    int len = (int)(in - seg);

    if(len==1 && seg[0]=='.') continue;
    if(len==2 && seg[0]=='.' && seg[1]=='.'){
      if(sp>0){
        out = segs[--sp];
        if(out > root_out && out[-1]=='/') out--;
      } else if(prelen==0){
        if(out!=p && out[-1]!='/') *out++ = '/';
        memcpy(out, "..", 2);
        out += 2;
      }
      continue;
    }

    if(out!=p && out[-1]!='/') *out++ = '/';
    char* seg_start = out;
    memcpy(out, seg, (size_t)len);
    out += len;
    if(sp<256) segs[sp++] = seg_start;
  }

  if(out==p){
    *out++ = '.';
  }
  *out = 0;
}

static int path_is_abs(const char* p){
  if(!p || !*p) return 0;
  if(p[0]=='/' || p[0]=='\\') return 1;
  if(((p[0]>='A' && p[0]<='Z') || (p[0]>='a' && p[0]<='z')) && p[1]==':') return 1;
  return 0;
}

static char* cwd_dup(void){
  char buf[4096];
  if(!getcwd(buf, (int)sizeof(buf))){
    return xstrdup(".");
  }
  return xstrdup(buf);
}

static char* to_abs_norm(const char* p){
  if(!p || !*p) return xstrdup(".");
  char* out = NULL;
  if(path_is_abs(p)){
    out = xstrdup(p);
  } else {
    char* cwd = cwd_dup();
    out = path_join2(cwd, p);
    free(cwd);
  }
  normalize_path_inplace(out);
  return out;
}

static int path_is_under(const char* path, const char* parent){
  if(!path || !parent) return 0;
  size_t np = strlen(path);
  size_t nr = strlen(parent);
  if(nr == 0) return 1;
  if(np < nr) return 0;
  for(size_t i=0;i<nr;i++){
    char a = path[i];
    char b = parent[i];
#ifdef _WIN32
    if(a>='A' && a<='Z') a = (char)(a - 'A' + 'a');
    if(b>='A' && b<='Z') b = (char)(b - 'A' + 'a');
#endif
    if(a != b) return 0;
  }
  if(np == nr) return 1;
  if(parent[nr-1] == '/') return 1;
  return path[nr] == '/';
}

Module* mg_load(ModuleGraph* G, TypeEnv* tenv, const char* base_dir, const char* import_path){
  if(!G || !tenv || !import_path) return NULL;
  mg_init_root(G, base_dir ? base_dir : ".");

  int is_rel = (import_path[0]=='.' || has_sep(import_path) || has_ext_tn(import_path));
  char* imp_name = NULL;
  char* imp_ver = NULL;
  char* dep_base = NULL;
  const char* selected_ver = NULL;
  if(!is_rel){
    int ver_ok = split_import_ver(import_path, &imp_name, &imp_ver);
    if(ver_ok < 0){
      fprintf(stderr, "tezzc error: invalid versioned import \"%s\"\n", import_path);
      exit(1);
    }
  } else {
    if(strchr(import_path, '@') != NULL){
      fprintf(stderr, "tezzc error: versioned import not allowed for path \"%s\"\n", import_path);
      exit(1);
    }
    imp_name = xstrdup(import_path);
  }

  // Dependency enforcement for module imports:
  // enforce only for imports originating from project source tree.
  int enforce_project_deps = 1;
  {
    char* root_norm = to_abs_norm(G->root_dir ? G->root_dir : ".");
    char* base_norm = to_abs_norm(base_dir ? base_dir : ".");
    if(!path_is_under(base_norm, root_norm)){
      enforce_project_deps = 0;
    }
    free(root_norm);
    free(base_norm);
  }

  // dependency enforcement for module imports
  if(!is_rel){
    const char* modname = imp_name ? imp_name : import_path;
    dep_base = (char*)import_base_name(modname);
    if(G->has_manifest && enforce_project_deps){
      const char* ver = mg_find_dep_ver(G, dep_base);
      if(!ver){
        fprintf(stderr, "tezzc error: missing dep.%s in tezz.mod for import \"%s\"\n", dep_base, import_path);
        exit(1);
      }
      if(imp_ver && strcmp(ver, imp_ver) != 0){
        fprintf(stderr, "tezzc error: dep.%s version mismatch (requested %s, pinned %s)\n", dep_base, imp_ver, ver);
        exit(1);
      }
      const char* use_ver = imp_ver ? imp_ver : ver;
      selected_ver = use_ver;
      if(G->has_lock && G->lock_n > 0 && !mg_lock_has(G, dep_base, use_ver)){
        fprintf(stderr, "tezzc error: missing %s@%s in tezz.lock for import \"%s\"\n", dep_base, use_ver, import_path);
        exit(1);
      }
    } else if(imp_ver){
      selected_ver = imp_ver;
    }
  }

  const char* resolve_name = imp_name ? imp_name : import_path;
  char* path = resolve_import_path(G, base_dir, resolve_name, dep_base, selected_ver);
  if(!path) return NULL;
  normalize_path_inplace(path);

  for(int i=0;i<G->mod_n;i++){
    Module* M = G->mods[i];
    if(M && M->path && strcmp(M->path, path)==0){
      free(path);
      if(imp_name) free(imp_name);
      if(imp_ver) free(imp_ver);
      if(dep_base) free(dep_base);
      return M;
    }
  }

  if(!file_exists(path)){
    fprintf(stderr, "tezzc error: import not found: %s\n", path);
    free(path);
    if(imp_name) free(imp_name);
    if(imp_ver) free(imp_ver);
    if(dep_base) free(dep_base);
    exit(1);
  }
  char* src = read_file_cstr(path);
  Module* M = parse_module(tenv, src, path);
  if(!M){
    free(path);
    if(imp_name) free(imp_name);
    if(imp_ver) free(imp_ver);
    if(dep_base) free(dep_base);
    return NULL;
  }

  // override module name for dotted imports
  if(!is_rel && resolve_name && strchr(resolve_name, '.') != NULL){
    M->mname = xstrdup(resolve_name);
    M->mlen = (int)strlen(resolve_name);
  }

  // detect ambiguous module names (same name, different path)
  if(M->mname && M->mlen > 0){
    for(int i=0;i<G->mod_n;i++){
      Module* E = G->mods[i];
      if(!E || !E->mname || E->mlen != M->mlen) continue;
      if(strncmp(E->mname, M->mname, (size_t)M->mlen) == 0){
        if(E->path && M->path && strcmp(E->path, M->path) != 0){
          fprintf(stderr, "tezzc error: ambiguous module name '%.*s' loaded from '%s' and '%s'\n",
                  M->mlen, M->mname, E->path, M->path);
          exit(1);
        }
      }
    }
  }

  if(G->mod_n == G->mod_cap){
    G->mod_cap = G->mod_cap ? G->mod_cap * 2 : 16;
    G->mods = (Module**)realloc(G->mods, sizeof(Module*)*(size_t)G->mod_cap);
    if(!G->mods) die("out of memory");
  }
  G->mods[G->mod_n++] = M;

  // recursively load imports
  char* mod_base = dirname_of(path);
  for(int i=0;i<M->import_n;i++){
    (void)mg_load(G, tenv, mod_base, M->imports[i]);
  }
  free(mod_base);

  if(imp_name) free(imp_name);
  if(imp_ver) free(imp_ver);
  if(dep_base) free(dep_base);

  return M;
}
