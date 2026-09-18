#include <stddef.h>
#include <limits.h>

static int tn_mul_overflow_i64_pos(long long a, long long b, long long* out){
  if(a <= 0 || b <= 0){
    if(out) *out = 0;
    return 0;
  }
  if(a > (LLONG_MAX / b)) return 1;
  if(out) *out = a * b;
  return 0;
}

static int tn_ai_validate_matmul_shape(long long m, long long n, long long k){
  long long mn = 0;
  long long mk = 0;
  long long kn = 0;
  if(m <= 0 || n <= 0 || k <= 0) return 0;
  if(tn_mul_overflow_i64_pos(m, n, &mn)) return 0;
  if(tn_mul_overflow_i64_pos(m, k, &mk)) return 0;
  if(tn_mul_overflow_i64_pos(k, n, &kn)) return 0;
  (void)mn;
  (void)mk;
  (void)kn;
  return 1;
}

int tezz_ai_vec_add_f32(float* out, const float* a, const float* b, long long n){
  if(!out || !a || !b || n <= 0) return -1;
  for(long long i = 0; i < n; ++i) out[i] = a[i] + b[i];
  return 0;
}

int tezz_ai_vec_mul_f32(float* out, const float* a, const float* b, long long n){
  if(!out || !a || !b || n <= 0) return -1;
  for(long long i = 0; i < n; ++i) out[i] = a[i] * b[i];
  return 0;
}

int tezz_ai_vec_axpy_f32(float* out, const float* a, const float* b, float alpha, long long n){
  if(!out || !a || !b || n <= 0) return -1;
  for(long long i = 0; i < n; ++i) out[i] = a[i] + alpha * b[i];
  return 0;
}

int tezz_ai_dot_f32(const float* a, const float* b, long long n, float* out){
  if(!a || !b || !out || n <= 0) return -1;
  float acc = 0.0f;
  for(long long i = 0; i < n; ++i) acc += a[i] * b[i];
  *out = acc;
  return 0;
}

int tezz_ai_matmul_f32(float* out, const float* a, const float* b, long long m, long long n, long long k){
  if(!out || !a || !b || !tn_ai_validate_matmul_shape(m, n, k)) return -1;
  for(long long row = 0; row < m; ++row){
    for(long long col = 0; col < n; ++col){
      float acc = 0.0f;
      for(long long p = 0; p < k; ++p){
        acc += a[row * k + p] * b[p * n + col];
      }
      out[row * n + col] = acc;
    }
  }
  return 0;
}
