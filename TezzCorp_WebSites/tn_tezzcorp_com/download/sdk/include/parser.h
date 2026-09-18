#pragma once
#include "ast.h"

typedef struct {
  Lexer lex;
  TypeEnv* tenv;
  const char* src;
  const char* path;
} Parser;

extern TokenKind op_tok;

Module* parse_module(TypeEnv* tenv, const char* src, const char* path);
