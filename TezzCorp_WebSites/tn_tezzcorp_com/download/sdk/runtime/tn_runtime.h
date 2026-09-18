#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void tn_print_str(const char* s);
void tn_print_i64(long long v);

void* tn_malloc(unsigned long long n);
void  tn_free(void* p);

char* tn_input_line(void);     // heap string; caller must free
long long tn_input_i64(void);  // reads signed 64-bit

#ifdef __cplusplus
}
#endif
