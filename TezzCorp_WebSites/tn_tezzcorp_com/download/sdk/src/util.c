// src/util.c
#include "../include/util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void die(const char* msg){
  fprintf(stderr, "tezzc error: %s\n", msg);
  exit(1);
}

void dief(const char* fmt, ...){
  va_list ap;
  fprintf(stderr, "tezzc error: ");
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

static void sb_grow(Str* s, int need){
  if (s->cap >= need) return;
  int nc = s->cap ? s->cap : 64;
  while (nc < need) nc *= 2;
  char* p = (char*)realloc(s->data, (size_t)nc);
  if(!p) die("out of memory");
  s->data = p;
  s->cap = nc;
}

void sb_init(Str* s){
  s->data = NULL;
  s->len = 0;
  s->cap = 0;
  sb_grow(s, 1);
  s->data[0] = 0;
}

void sb_puts(Str* s, const char* lit){
  int n = (int)strlen(lit);
  sb_grow(s, s->len + n + 1);
  memcpy(s->data + s->len, lit, (size_t)n);
  s->len += n;
  s->data[s->len] = 0;
}

void sb_printf(Str* s, const char* fmt, ...){
  va_list ap;
  va_start(ap, fmt);
  char tmp[4096];
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n < 0) die("vsnprintf failed");
  if (n >= (int)sizeof(tmp)) die("sb_printf overflow (increase tmp)");
  sb_grow(s, s->len + n + 1);
  memcpy(s->data + s->len, tmp, (size_t)n);
  s->len += n;
  s->data[s->len] = 0;
}

char* read_file_cstr(const char* path){
  FILE* f = fopen(path, "rb");
  if(!f){
    fprintf(stderr, "tezzc error: cannot open file: %s\n", path);
    exit(1);
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* buf = (char*)malloc((size_t)sz + 1);
  if(!buf) die("out of memory");

  size_t got = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[got] = 0;
  return buf;
}

void write_file_raw(const char* path, const char* data){
  FILE* f = fopen(path, "wb");
  if(!f){
    fprintf(stderr, "tezzc error: cannot write file: %s\n", path);
    exit(1);
  }
  fwrite(data, 1, strlen(data), f);
  fclose(f);
}

static void print_snippet(const char* src, int line, int col){
  // show the line text + caret
  const char* p = src;
  int cur = 1;

  while (*p && cur < line){
    if (*p == '\n') cur++;
    p++;
  }

  const char* line_start = p;
  const char* line_end = p;
  while (*line_end && *line_end != '\n') line_end++;

  fprintf(stderr, "  %.*s\n", (int)(line_end - line_start), line_start);
  fprintf(stderr, "  ");
  for (int i=1;i<col;i++) fputc(' ', stderr);
  fprintf(stderr, "^\n");
}

static const char* help_for_error(const char* msg){
  if (!msg) return NULL;
  if (strstr(msg, "unknown name")){
    return "declare the name before use, import the module that defines it, or check the spelling.";
  }
  if (strstr(msg, "unknown module")){
    return "add the module import, check the module name, or verify that the module file is on the search path.";
  }
  if (strstr(msg, "wrong number of arguments")){
    return "check the function signature and pass the expected number of arguments.";
  }
  if (strstr(msg, "type mismatch") || strstr(msg, "cannot assign")){
    return "make the expression type match the target type, or use an explicit cast when the conversion is intentional.";
  }
  if (strstr(msg, "unsafe operation")){
    return "wrap pointer operations in an unsafe block after validating the pointer and ownership rules.";
  }
  if (strstr(msg, "expected ':'")){
    return "use ':' after block headers such as fn, if, else, while, for, and unsafe.";
  }
  return NULL;
}

void err_at2(const char* path, const char* src, int line, int col, const char* msg){
  fprintf(stderr, "%s:%d:%d: tezzc error: %s\n", path?path:"<mem>", line, col, msg);
  if (src) print_snippet(src, line, col);
  const char* help = help_for_error(msg);
  if (help) fprintf(stderr, "help: %s\n", help);
  exit(1);
}

void err_at(SrcLoc loc, const char* msg){
  err_at2(loc.path, loc.src, loc.line, loc.col, msg);
}

void warn_at2(const char* path, const char* src, int line, int col, const char* msg){
  fprintf(stderr, "%s:%d:%d: tezzc warning: %s\n", path?path:"<mem>", line, col, msg);
  if (src) print_snippet(src, line, col);
}

void warn_at(SrcLoc loc, const char* msg){
  warn_at2(loc.path, loc.src, loc.line, loc.col, msg);
}
