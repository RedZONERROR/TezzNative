// include/util.h
#pragma once
#include <stdarg.h>
#include "loc.h"

typedef struct {
  char* data;
  int len;
  int cap;
} Str;

void die(const char* msg);
void dief(const char* fmt, ...);

// string builder
void sb_init(Str* s);
void sb_puts(Str* s, const char* lit);
void sb_printf(Str* s, const char* fmt, ...);

// file helpers
char* read_file_cstr(const char* path);
void write_file_raw(const char* path, const char* data);

// diagnostics
void err_at(SrcLoc loc, const char* msg);
void err_at2(const char* path, const char* src, int line, int col, const char* msg);
void warn_at(SrcLoc loc, const char* msg);
void warn_at2(const char* path, const char* src, int line, int col, const char* msg);
