// include/loc.h
#pragma once

typedef struct {
  const char* path;   // file path or "<mem>"
  const char* src;    // pointer to full source text
  int line;           // 1-based
  int col;            // 1-based
} SrcLoc;
