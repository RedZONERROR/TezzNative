// include/lexer.h
#pragma once
#include <stddef.h>
#include "loc.h"

typedef enum {
  TK_EOF = 0,

  // literals
  TK_NUMBER,
  TK_FLOAT,
  TK_STRING,
  TK_IDENT,

  // punctuation
  TK_LPAREN, TK_RPAREN,
  TK_LBRACE, TK_RBRACE,
  TK_LBRACKET, TK_RBRACKET,
  TK_SEMI, TK_COMMA,
  TK_DOT,
  TK_AT,
  TK_QUESTION, TK_COLON,
  TK_NEWLINE,
  TK_INDENT,
  TK_DEDENT,

  // operators
  TK_ASSIGN,
  TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,

  TK_PLUSPLUS, TK_MINUSMINUS,

  TK_EQ, TK_NEQ, TK_LT, TK_LTE, TK_GT, TK_GTE,

  TK_LAND, TK_LOR,
  TK_LNOT, TK_BNOT,

  TK_BAND, TK_BOR, TK_BXOR,
  TK_SHL, TK_SHR,

  // compound assign
  TK_PLUSEQ, TK_MINUSEQ, TK_STAREQ, TK_SLASHEQ, TK_PERCENTEQ,
  TK_ANDEQ, TK_OREQ, TK_XOREQ,
  TK_SHLEQ, TK_SHREQ,

  // multi-char
  TK_ARROW,   // ->

  // keywords
  TK_FN,
  TK_LET,
  TK_RET,
  TK_IF,
  TK_ELSE,
  TK_WHILE,
  TK_FOR,
  TK_BREAK,
  TK_CONTINUE,
  TK_SAY,
  TK_SAYSTR,
  TK_MALLOC,
  TK_FREE,
  TK_INPUT_LINE,
  TK_INPUT_I64,

  // A + C
  TK_STRUCT,
  TK_IMPORT,
  TK_ENUM,
  TK_TYPEDEF,
  TK_SWITCH,
  TK_CASE,
  TK_DEFAULT,
  TK_AS,
  TK_FALLTHROUGH,
  TK_SIZEOF,
  TK_ALIGNOF,
  TK_CONST,
  TK_UNSAFE,
  TK_KERNEL,
  TK_STATIC,
  TK_EXTERN,

} TokenKind;

typedef struct {
  TokenKind kind;
  const char* start;
  int len;
  long long num;  // TK_NUMBER
  double f;       // TK_FLOAT
  char* str;      // TK_STRING (heap)
  int line;
  int col;
} Token;

typedef struct {
  const char* src;
  const char* path;
  size_t pos;
  int line;
  int col;
  Token cur;
  int at_line_start;
  int indent_stack[64];
  int indent_top;
  int pending_dedents;
} Lexer;

void lex_init(Lexer* L, const char* src, const char* path);
void lex_next(Lexer* L);

static inline SrcLoc tok_loc(Lexer* L){
  SrcLoc loc;
  loc.path = L->path ? L->path : "<mem>";
  loc.src  = L->src;
  loc.line = (L->cur.line ? L->cur.line : L->line);
  loc.col  = (L->cur.col  ? L->cur.col  : L->col);
  return loc;
}
