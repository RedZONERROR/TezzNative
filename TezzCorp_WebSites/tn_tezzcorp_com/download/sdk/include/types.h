#pragma once
#include "lexer.h"
#include "loc.h"

typedef enum {
  TY_I8,
  TY_I16,
  TY_I32,
  TY_I64,
  TY_U8,
  TY_U16,
  TY_U32,
  TY_U64,
  TY_F64,
  TY_PTR,
  TY_ARRAY,
  TY_STRUCT,
  TY_FN,
  TY_VOID
} TyKind;

typedef struct Type Type;
typedef struct Field Field;

struct Field { const char* name; int name_len; Type* ty; int off; };

struct Type {
  TyKind k;
  Type* elem;
  long long len;
  const char* sname; // struct name (points into src text; safe as long src lives)
  int sname_len;
  Field* fields;
  int field_n;
  Type** fparams;
  int fparam_n;
  Type* fret;
  int size;
  int align;
  int incomplete; // for forward-declared structs
  SrcLoc decl_loc;
};

typedef struct {
  Type* ty_i8;
  Type* ty_i16;
  Type* ty_i32;
  Type* ty_i64;
  Type* ty_u8;
  Type* ty_u16;
  Type* ty_u32;
  Type* ty_u64;
  Type* ty_f64;
  Type* ty_void;
} BuiltinTypes;

typedef struct {
  Type** items;
  int count;
  int cap;
  BuiltinTypes b;
  const char** alias_name;
  int* alias_len;
  Type** alias_ty;
  int alias_n;
  int alias_cap;
} TypeEnv;

void tenv_init(TypeEnv* E);
void tenv_free(TypeEnv* E);

Type* type_ptr(TypeEnv* E, Type* elem);
Type* type_array(TypeEnv* E, Type* elem, long long n);
Type* type_fn(TypeEnv* E, Type* ret, Type** params, int n);

Type* tenv_find_struct(TypeEnv* E, const char* name, int len);
Type* tenv_add_struct(TypeEnv* E, const char* name, int len, Field* fields, int field_n, SrcLoc loc);
Type* tenv_add_incomplete_struct(TypeEnv* E, const char* name, int len, SrcLoc loc);
Type* tenv_find_alias(TypeEnv* E, const char* name, int len);
void  tenv_add_alias(TypeEnv* E, const char* name, int len, Type* ty, SrcLoc loc);

int   type_eq(Type* a, Type* b);
Field* type_find_field(Type* st, const char* name, int len);

int   type_size(Type* t);
int   type_align(Type* t);
int   type_is_int(Type* t);
int   type_is_signed_int(Type* t);
int   type_is_unsigned_int(Type* t);
int   type_is_numeric(Type* t);
int   type_int_bits(Type* t);
const char* type_builtin_name(Type* t);
