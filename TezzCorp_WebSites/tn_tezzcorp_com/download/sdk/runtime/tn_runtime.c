#include "tn_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TN_GUARD_ALLOC
#include "tn_guard_alloc.h"
#define malloc  tn_guard_malloc
#define free    tn_guard_free
#define realloc tn_guard_realloc
#endif

void tn_print_str(const char* s) {
  if (!s) { puts("(null)"); return; }
  puts(s);
}

void tn_print_i64(long long v) {
  printf("%lld\n", v);
}

void* tn_malloc(unsigned long long n) {
  if (n == 0) n = 1;
  void* p = malloc((size_t)n);
  if (!p) {
    fputs("TezzNative: malloc failed\n", stderr);
    exit(1);
  }
  return p;
}

void tn_free(void* p) {
  free(p);
}

static void strip_newline(char* s) {
  size_t n = strlen(s);
  while (n && (s[n-1] == '\n' || s[n-1] == '\r')) {
    s[n-1] = 0;
    n--;
  }
}

char* tn_input_line(void) {
  // Portable dynamic fgets loop for MinGW
  size_t cap = 256;
  size_t len = 0;
  char* buf = (char*)malloc(cap);
  if (!buf) { fputs("TezzNative: OOM\n", stderr); exit(1); }

  for (;;) {
    if (!fgets(buf + len, (int)(cap - len), stdin)) {
      // EOF -> return empty string (heap)
      buf[0] = 0;
      return buf;
    }
    len = strlen(buf);
    if (len && (buf[len-1] == '\n' || buf[len-1] == '\r')) break;

    // need more
    cap *= 2;
    char* nb = (char*)realloc(buf, cap);
    if (!nb) { free(buf); fputs("TezzNative: OOM\n", stderr); exit(1); }
    buf = nb;
  }

  strip_newline(buf);
  return buf;
}

long long tn_input_i64(void) {
  // Simple robust parse via line
  char* s = tn_input_line();
  char* end = NULL;
  long long v = strtoll(s, &end, 10);
  free(s);
  return v;
}
