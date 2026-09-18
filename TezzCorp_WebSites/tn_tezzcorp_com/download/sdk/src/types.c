#include "types.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

static void* xmalloc(size_t n){ void* p=malloc(n); if(!p) die("out of memory"); return p; }

static int align_up(int x, int a){ return (x + (a-1)) & ~(a-1); }

static void tenv_ensure(TypeEnv* E){
  if(E->count==E->cap){
    E->cap*=2;
    E->items=(Type**)realloc(E->items,sizeof(Type*)*E->cap);
    if(!E->items) die("out of memory");
  }
}

static Type* new_prim(TyKind k, int size, int align){
  Type* t = (Type*)xmalloc(sizeof(Type));
  memset(t, 0, sizeof(*t));
  t->k = k;
  t->size = size;
  t->align = align;
  return t;
}

void tenv_init(TypeEnv* E){
  memset(E,0,sizeof(*E));
  E->cap = 64;
  E->items = (Type**)xmalloc(sizeof(Type*)*E->cap);

  E->b.ty_i8  = new_prim(TY_I8,  1, 1);
  E->b.ty_i16 = new_prim(TY_I16, 2, 2);
  E->b.ty_i32 = new_prim(TY_I32, 4, 4);
  E->b.ty_i64 = new_prim(TY_I64, 8, 8);
  E->b.ty_u8  = new_prim(TY_U8,  1, 1);
  E->b.ty_u16 = new_prim(TY_U16, 2, 2);
  E->b.ty_u32 = new_prim(TY_U32, 4, 4);
  E->b.ty_u64 = new_prim(TY_U64, 8, 8);
  E->b.ty_f64 = new_prim(TY_F64, 8, 8);
  E->b.ty_void = new_prim(TY_VOID, 0, 1);

  E->items[E->count++] = E->b.ty_i8;
  E->items[E->count++] = E->b.ty_i16;
  E->items[E->count++] = E->b.ty_i32;
  E->items[E->count++] = E->b.ty_i64;
  E->items[E->count++] = E->b.ty_u8;
  E->items[E->count++] = E->b.ty_u16;
  E->items[E->count++] = E->b.ty_u32;
  E->items[E->count++] = E->b.ty_u64;
  E->items[E->count++] = E->b.ty_f64;
  E->items[E->count++] = E->b.ty_void;

  E->alias_cap = 32;
  E->alias_name = (const char**)xmalloc(sizeof(char*)*(size_t)E->alias_cap);
  E->alias_len  = (int*)xmalloc(sizeof(int)*(size_t)E->alias_cap);
  E->alias_ty   = (Type**)xmalloc(sizeof(Type*)*(size_t)E->alias_cap);

  // User-facing aliases (2026 style)
  tenv_add_alias(E, "int", 3, E->b.ty_i64, (SrcLoc){0});
  tenv_add_alias(E, "float", 5, E->b.ty_f64, (SrcLoc){0});
  tenv_add_alias(E, "char", 4, E->b.ty_u8, (SrcLoc){0});
  Type* pchar = type_ptr(E, E->b.ty_u8);
  tenv_add_alias(E, "str", 3, pchar, (SrcLoc){0});
}
void tenv_free(TypeEnv* E){
  for(int i=0;i<E->count;i++){
    Type* t=E->items[i];
    if(t->k==TY_STRUCT && t->fields) free(t->fields);
    if(t->k==TY_FN && t->fparams) free(t->fparams);
    free(t);
  }
  free(E->items);
  free(E->alias_name);
  free(E->alias_len);
  free(E->alias_ty);
}

Type* type_ptr(TypeEnv* E, Type* elem){
  Type* t=(Type*)xmalloc(sizeof(Type));
  memset(t,0,sizeof(*t));
  t->k=TY_PTR;
  t->elem=elem;
  t->size=8;
  t->align=8;
  t->incomplete=0;
  tenv_ensure(E);
  E->items[E->count++] = t;
  return t;
}
Type* type_array(TypeEnv* E, Type* elem, long long n){
  if(n<=0) die("array length must be > 0");
  Type* t=(Type*)xmalloc(sizeof(Type));
  int es = type_size(elem);
  int al = type_align(elem);
  long long sz = (long long)es * n;
  if(sz > (1LL<<30)) die("array too large");
  memset(t,0,sizeof(*t));
  t->k=TY_ARRAY;
  t->elem=elem;
  t->len=n;
  t->size=(int)sz;
  t->align=al;
  t->incomplete=0;
  tenv_ensure(E);
  E->items[E->count++] = t;
  return t;
}

Type* type_fn(TypeEnv* E, Type* ret, Type** params, int n){
  Type* t=(Type*)xmalloc(sizeof(Type));
  memset(t,0,sizeof(*t));
  t->k = TY_FN;
  t->fret = ret;
  t->fparam_n = n;
  if(n>0){
    t->fparams = (Type**)xmalloc(sizeof(Type*)*(size_t)n);
    for(int i=0;i<n;i++) t->fparams[i]=params[i];
  }
  // function types are not first-class values; size is pointer-sized when referenced via ptr
  t->size = 8;
  t->align = 8;
  t->incomplete=0;
  tenv_ensure(E);
  E->items[E->count++] = t;
  return t;
}

static int name_eq(Type* t, const char* name, int len){
  return t->k==TY_STRUCT && t->sname_len==len && strncmp(t->sname,name,(size_t)len)==0;
}

Type* tenv_find_struct(TypeEnv* E, const char* name, int len){
  for(int i=0;i<E->count;i++){
    if(name_eq(E->items[i], name, len)) return E->items[i];
  }
  return NULL;
}

Type* tenv_find_alias(TypeEnv* E, const char* name, int len){
  for(int i=0;i<E->alias_n;i++){
    if(E->alias_len[i]==len && strncmp(E->alias_name[i], name, (size_t)len)==0){
      return E->alias_ty[i];
    }
  }
  return NULL;
}

void tenv_add_alias(TypeEnv* E, const char* name, int len, Type* ty, SrcLoc loc){
  if(tenv_find_alias(E, name, len)) err_at(loc, "duplicate typedef name");
  if(E->alias_n==E->alias_cap){
    E->alias_cap*=2;
    E->alias_name=(const char**)realloc((void*)E->alias_name, sizeof(char*)*(size_t)E->alias_cap);
    E->alias_len =(int*)realloc(E->alias_len, sizeof(int)*(size_t)E->alias_cap);
    E->alias_ty  =(Type**)realloc(E->alias_ty, sizeof(Type*)*(size_t)E->alias_cap);
    if(!E->alias_name || !E->alias_len || !E->alias_ty) die("out of memory");
  }
  E->alias_name[E->alias_n]=name;
  E->alias_len[E->alias_n]=len;
  E->alias_ty[E->alias_n]=ty;
  E->alias_n++;
}

static void compute_struct_layout(Type* st){
  int off=0;
  int maxa=1;
  for(int i=0;i<st->field_n;i++){
    Field* f=&st->fields[i];
    int a = type_align(f->ty);
    int s = type_size(f->ty);
    if(a>maxa) maxa=a;
    off = align_up(off, a);
    f->off = off;
    off += s;
  }
  off = align_up(off, maxa);
  st->align = maxa;
  st->size = off;
}

Type* tenv_add_struct(TypeEnv* E, const char* name, int len, Field* fields, int field_n, SrcLoc loc){
  Type* existing = tenv_find_struct(E, name, len);
  if(existing){
    if(!existing->incomplete) err_at(loc, "duplicate struct name");
    existing->fields = (Field*)xmalloc(sizeof(Field)* (size_t)field_n);
    existing->field_n = field_n;
    for(int i=0;i<field_n;i++) existing->fields[i]=fields[i];
    existing->incomplete = 0;
    existing->decl_loc = loc;

    // validate unique fields
    for(int i=0;i<field_n;i++){
      for(int j=i+1;j<field_n;j++){
        if(existing->fields[i].name_len==existing->fields[j].name_len &&
           strncmp(existing->fields[i].name, existing->fields[j].name, (size_t)existing->fields[i].name_len)==0){
          err_at(loc, "duplicate struct field");
        }
      }
    }

    compute_struct_layout(existing);
    return existing;
  }

  Type* st=(Type*)xmalloc(sizeof(Type));
  memset(st,0,sizeof(*st));
  st->k = TY_STRUCT;
  st->sname = name;
  st->sname_len = len;
  st->field_n = field_n;
  st->incomplete = 0;
  st->decl_loc = loc;
  st->fields = (Field*)xmalloc(sizeof(Field)* (size_t)field_n);
  for(int i=0;i<field_n;i++) st->fields[i]=fields[i];

  // validate unique fields
  for(int i=0;i<field_n;i++){
    for(int j=i+1;j<field_n;j++){
      if(st->fields[i].name_len==st->fields[j].name_len &&
         strncmp(st->fields[i].name, st->fields[j].name, (size_t)st->fields[i].name_len)==0){
        err_at(loc, "duplicate struct field");
      }
    }
  }

  compute_struct_layout(st);

  tenv_ensure(E);
  E->items[E->count++] = st;
  return st;
}

Type* tenv_add_incomplete_struct(TypeEnv* E, const char* name, int len, SrcLoc loc){
  Type* existing = tenv_find_struct(E, name, len);
  if(existing) return existing;

  Type* st=(Type*)xmalloc(sizeof(Type));
  memset(st,0,sizeof(*st));
  st->k = TY_STRUCT;
  st->sname = name;
  st->sname_len = len;
  st->field_n = 0;
  st->fields = NULL;
  st->size = 0;
  st->align = 1;
  st->incomplete = 1;
  st->decl_loc = loc;

  tenv_ensure(E);
  E->items[E->count++] = st;
  return st;
}

int type_eq(Type* a, Type* b){
  if(!a || !b) return 0;
  if(a==b) return 1;
  if(a->k!=b->k) return 0;
  if(a->k==TY_PTR) return type_eq(a->elem,b->elem);
  if(a->k==TY_ARRAY) return a->len==b->len && type_eq(a->elem,b->elem);
  if(a->k==TY_STRUCT) return a->sname_len==b->sname_len && strncmp(a->sname,b->sname,(size_t)a->sname_len)==0;
  if(a->k==TY_FN){
    if(!type_eq(a->fret, b->fret)) return 0;
    if(a->fparam_n != b->fparam_n) return 0;
    for(int i=0;i<a->fparam_n;i++){
      if(!type_eq(a->fparams[i], b->fparams[i])) return 0;
    }
    return 1;
  }
  return 1;
}

Field* type_find_field(Type* st, const char* name, int len){
  if(st->k!=TY_STRUCT) return NULL;
  for(int i=0;i<st->field_n;i++){
    Field* f=&st->fields[i];
    if(f->name_len==len && strncmp(f->name,name,(size_t)len)==0) return f;
  }
  return NULL;
}

int type_size(Type* t){
  return t->size;
}
int type_align(Type* t){
  return t->align ? t->align : 1;
}

int type_is_signed_int(Type* t){
  if(!t) return 0;
  return t->k==TY_I8 || t->k==TY_I16 || t->k==TY_I32 || t->k==TY_I64;
}

int type_is_unsigned_int(Type* t){
  if(!t) return 0;
  return t->k==TY_U8 || t->k==TY_U16 || t->k==TY_U32 || t->k==TY_U64;
}

int type_is_int(Type* t){
  return type_is_signed_int(t) || type_is_unsigned_int(t);
}

int type_is_numeric(Type* t){
  return type_is_int(t) || (t && t->k==TY_F64);
}

int type_int_bits(Type* t){
  if(!type_is_int(t)) return 0;
  return type_size(t) * 8;
}

const char* type_builtin_name(Type* t){
  if(!t) return "i64";
  switch(t->k){
    case TY_I8: return "i8";
    case TY_I16: return "i16";
    case TY_I32: return "i32";
    case TY_I64: return "i64";
    case TY_U8: return "u8";
    case TY_U16: return "u16";
    case TY_U32: return "u32";
    case TY_U64: return "u64";
    case TY_F64: return "f64";
    case TY_VOID: return "void";
    default: return NULL;
  }
}
