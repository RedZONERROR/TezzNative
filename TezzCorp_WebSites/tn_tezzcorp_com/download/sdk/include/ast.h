#pragma once
#include "types.h"
#include "loc.h"

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct FnDecl FnDecl;
typedef struct StructDecl StructDecl;
typedef struct TypedefDecl TypedefDecl;
typedef struct EnumDecl EnumDecl;
typedef struct Module Module;
typedef struct Attr Attr;

typedef enum {
  EX_NUM, EX_STR, EX_NAME,
  EX_FLOAT,
  EX_BIN, EX_UN,
  EX_CALL,
  EX_INDEX,
  EX_DOT,        // a.b  (field access or module.fn)
  EX_ADDR,       // &lvalue
  EX_DEREF,      // *expr
  EX_ARRAY_LIT,  // [a,b,c]
  EX_STRUCT_LIT, // Type{ field:expr, ... }
  EX_CAST,       // expr as Type
  EX_SIZEOF,
  EX_ALIGNOF
} ExprKind;

typedef enum {
  OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
  OP_EQ, OP_NEQ, OP_LT, OP_LTE, OP_GT, OP_GTE,
  OP_LAND, OP_LOR,
  OP_BAND, OP_BOR, OP_BXOR,
  OP_SHL, OP_SHR
} BinOp;

typedef enum { UN_POS, UN_NEG, UN_LNOT, UN_BNOT } UnOp;

struct Expr {
  ExprKind k;
  SrcLoc loc;
  Type* ty; // set by typechecker
  union {
    long long num;
    double f;
    const char* str;   // pointer into lexer-owned heap string copied into pool in vm
    struct { const char* name; int len; } name;
    struct { BinOp op; Expr* a; Expr* b; } bin;
    struct { UnOp op; Expr* e; } un;
    struct { Expr* callee; Expr** args; int argc; } call;
    struct { Expr* base; Expr* idx; } index;
    struct { Expr* base; const char* mem; int memlen; } dot;
    struct { Expr** items; int n; } arr;
    struct { const char* tname; int tlen; const char** f; int* flen; Expr** v; int n; } stlit;
    struct { Expr* e; Type* to; } cast;
    struct { Expr* e; Type* ty; int is_type; } siz;
  };
};

typedef enum {
  ST_BLOCK,
  ST_LET,
  ST_ASSIGN,
  ST_EXPR,
  ST_IF,
  ST_WHILE,
  ST_FOR,
  ST_BREAK,
  ST_CONTINUE,
  ST_RET,
  ST_FALLTHROUGH,
  ST_SWITCH,
  ST_UNSAFE
} StmtKind;

typedef struct {
  Expr* val;   // NULL for default
  Stmt* body;  // block
} SwitchCase;

typedef struct {
  const char* name;
  int len;
  long long val;
} EnumConst;

struct Attr {
  const char* name;
  int len;
  const char* arg;
  int arg_len;
};

typedef struct {
  const char* name;
  int len;
  long long val;
  int has_val;
  SrcLoc loc;
} EnumItem;

struct TypedefDecl {
  const char* name; int len;
  Type* ty;
  SrcLoc loc;
  Attr* attrs;
  int attr_n;
};

struct EnumDecl {
  const char* name; int len;
  EnumItem* items;
  int item_n;
  int is_brace; // 1 for { ... }, 0 for : block
  SrcLoc loc;
  Attr* attrs;
  int attr_n;
};

struct Stmt {
  StmtKind k;
  SrcLoc loc;
  union {
    struct { Stmt** items; int n; } block;
    struct { const char* name; int len; Type* ty; Expr* init; int is_const; } let;
    struct { Expr* lhs; TokenKind op; Expr* rhs; } asg; // op includes =, += etc, or ++/--
    struct { Expr* e; } expr;
    struct { Expr* cond; Stmt* thenb; Stmt* elseb; } iff;
    struct { Expr* cond; Stmt* body; } wh;
    struct { Stmt* init; Expr* cond; Stmt* step; Stmt* body; } fr;
    struct { Expr* e; } ret;
    struct { Expr* cond; SwitchCase* cases; int case_n; } sw;
    struct { Stmt* body; } uns;
  };
};

struct FnDecl {
  const char* name; int len;
  const char** pnames; int* plens; Type** ptypes; int arity;
  Type* ret;
  Stmt* body;
  SrcLoc loc;
  int is_extern;
  int is_unsafe;
  int is_kernel;
  Type* fty;
  Attr* attrs;
  int attr_n;
};

struct StructDecl {
  const char* name; int len;
  Field* fields;
  int field_n;
  SrcLoc loc;
  Attr* attrs;
  int attr_n;
};

struct Module {
  const char* path;
  const char* src;

  // module name derived from filename (no ext)
  const char* mname; int mlen;

  // imports are strings (paths)
  const char** imports;
  int import_n;

  StructDecl** structs;
  int struct_n;

  TypedefDecl** typedefs;
  int typedef_n;

  EnumDecl** enums;
  int enum_n;

  FnDecl** fns;
  int fn_n;

  // globals (module-scope)
  struct GlobalVar {
    const char* name; int len;
    Type* ty;
    Expr* init; // may be NULL (zero-init)
    int is_extern;
    int is_static;
    Attr* attrs;
    int attr_n;
  } *globals;
  int global_n;

  // enum constants (flattened)
  EnumConst* econsts;
  int econst_n;
};
