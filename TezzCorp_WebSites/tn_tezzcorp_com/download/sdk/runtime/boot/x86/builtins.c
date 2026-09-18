// runtime/boot/x86/builtins.c
// Minimal compiler-rt style helpers for 32-bit freestanding builds.

#include <stdint.h>

static uint64_t tn_udivmod64(uint64_t n, uint64_t d, uint64_t* rem_out){
  if(d == 0) {
    if(rem_out) *rem_out = 0;
    return 0;
  }
  uint64_t q = 0;
  uint64_t r = 0;
  for(int bit = 63; bit >= 0; bit--){
    r = (r << 1) | ((n >> bit) & 1ULL);
    if(r >= d){
      r -= d;
      q |= (1ULL << bit);
    }
  }
  if(rem_out) *rem_out = r;
  return q;
}

unsigned long long __udivdi3(unsigned long long n, unsigned long long d){
  return tn_udivmod64(n, d, 0);
}

unsigned long long __umoddi3(unsigned long long n, unsigned long long d){
  uint64_t r = 0;
  (void)tn_udivmod64(n, d, &r);
  return r;
}

long long __divdi3(long long a, long long b){
  uint64_t ua = (uint64_t)a;
  uint64_t ub = (uint64_t)b;
  int neg = 0;
  if((long long)ua < 0){
    ua = (~ua) + 1ULL;
    neg ^= 1;
  }
  if((long long)ub < 0){
    ub = (~ub) + 1ULL;
    neg ^= 1;
  }
  uint64_t q = tn_udivmod64(ua, ub, 0);
  if(neg) q = (~q) + 1ULL;
  return (long long)q;
}

long long __moddi3(long long a, long long b){
  uint64_t ua = (uint64_t)a;
  uint64_t ub = (uint64_t)b;
  int neg = 0;
  if((long long)ua < 0){
    ua = (~ua) + 1ULL;
    neg = 1;
  }
  if((long long)ub < 0){
    ub = (~ub) + 1ULL;
  }
  uint64_t r = 0;
  (void)tn_udivmod64(ua, ub, &r);
  if(neg) r = (~r) + 1ULL;
  return (long long)r;
}
