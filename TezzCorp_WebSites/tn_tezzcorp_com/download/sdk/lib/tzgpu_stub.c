// tzgpu_stub.c -- TezzNative GPU Stub DLL
// Satisfies all tzgpu.dll imports on machines without CUDA.
// tz_cuda_device_count() returns 0, so the CPU fallback activates.
// All other functions return 0 (no-op / not available).
//
// Build:
//   (Windows x64, MSVC): cl /O1 /LD /MD /nologo tzgpu_stub.c /Fe:tzgpu.dll
//   (Windows ARM64):     cl /O1 /arch:ARM64 /LD /MD /nologo tzgpu_stub.c /Fe:tzgpu.dll
//   (Linux):             gcc -shared -fPIC tzgpu_stub.c -o libtzgpu.so

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#  define TZ_EXPORT __declspec(dllexport)
#else
#  define TZ_EXPORT __attribute__((visibility("default")))
#endif

// ── CUDA memory stubs ─────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_cuda_malloc(int64_t n) { (void)n; return 0; }
TZ_EXPORT int64_t tz_cuda_free(int64_t p) { (void)p; return 0; }
TZ_EXPORT int64_t tz_cuda_memcpy(int64_t d, int64_t s, int64_t n) { (void)d;(void)s;(void)n; return 0; }
TZ_EXPORT int64_t tz_cuda_sync() { return 0; }
TZ_EXPORT int64_t tz_cuda_device_count() { return 0; }  /* KEY: 0 → triggers CPU path */
TZ_EXPORT int64_t tz_cuda_set_device(int64_t i) { (void)i; return 0; }
TZ_EXPORT int64_t tz_cuda_mem_free(int64_t p) { (void)p; return 0; }
TZ_EXPORT int64_t tz_cuda_mem_total(int64_t p) { (void)p; return 0; }

// ── v1 legacy ─────────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_add(int64_t a,int64_t b,int64_t c,int64_t n){(void)a;(void)b;(void)c;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_mul(int64_t a,int64_t b,int64_t c,int64_t n){(void)a;(void)b;(void)c;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_relu(int64_t a,int64_t c,int64_t n){(void)a;(void)c;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_scale(int64_t a,int64_t c,int64_t n,double s){(void)a;(void)c;(void)n;(void)s;return 0;}
TZ_EXPORT int64_t tz_gpu_gelu(int64_t a,int64_t c,int64_t n){(void)a;(void)c;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_sigmoid(int64_t a,int64_t c,int64_t n){(void)a;(void)c;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_gemm_f64(int64_t a,int64_t b,int64_t c,int64_t M,int64_t K,int64_t N){(void)a;(void)b;(void)c;(void)M;(void)K;(void)N;return 0;}
TZ_EXPORT int64_t tz_gpu_gemm_f32(int64_t a,int64_t b,int64_t c,int64_t M,int64_t K,int64_t N,double al,double be){(void)a;(void)b;(void)c;(void)M;(void)K;(void)N;(void)al;(void)be;return 0;}
TZ_EXPORT int64_t tz_gpu_matmul_tiled(int64_t a,int64_t b,int64_t c,int64_t M,int64_t K,int64_t N){(void)a;(void)b;(void)c;(void)M;(void)K;(void)N;return 0;}
TZ_EXPORT int64_t tz_gpu_softmax(int64_t x,int64_t r,int64_t c){(void)x;(void)r;(void)c;return 0;}
TZ_EXPORT int64_t tz_gpu_layer_norm(int64_t x,int64_t g,int64_t b,int64_t r,int64_t c,double e){(void)x;(void)g;(void)b;(void)r;(void)c;(void)e;return 0;}
TZ_EXPORT int64_t tz_gpu_info() { printf("[GPU] No CUDA GPU (stub)\n"); return 0; }

// ── v2 production ─────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_add_f32(int64_t a,int64_t b,int64_t c,int64_t n){(void)a;(void)b;(void)c;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_sub_f32(int64_t a,int64_t b,int64_t c,int64_t n){(void)a;(void)b;(void)c;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_mul_f32(int64_t a,int64_t b,int64_t c,int64_t n){(void)a;(void)b;(void)c;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_scale_f32(int64_t a,int64_t c,int64_t n,double s){(void)a;(void)c;(void)n;(void)s;return 0;}
TZ_EXPORT int64_t tz_gpu_fill_f32(int64_t a,int64_t n,double v){(void)a;(void)n;(void)v;return 0;}
TZ_EXPORT int64_t tz_gpu_copy_f32(int64_t s,int64_t d,int64_t n){(void)s;(void)d;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_axpy_f32(int64_t y,int64_t x,double a,int64_t n){(void)y;(void)x;(void)a;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_clamp_f32(int64_t a,int64_t c,int64_t n,double lo,double hi){(void)a;(void)c;(void)n;(void)lo;(void)hi;return 0;}
TZ_EXPORT int64_t tz_gpu_relu_f32(int64_t a,int64_t c,int64_t n){(void)a;(void)c;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_relu_bwd_f32(int64_t g,int64_t x,int64_t gi,int64_t n){(void)g;(void)x;(void)gi;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_leaky_relu_f32(int64_t a,int64_t c,int64_t n,double al){(void)a;(void)c;(void)n;(void)al;return 0;}
TZ_EXPORT int64_t tz_gpu_sigmoid_f32(int64_t a,int64_t c,int64_t n){(void)a;(void)c;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_sigmoid_bwd_f32(int64_t g,int64_t x,int64_t gi,int64_t n){(void)g;(void)x;(void)gi;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_tanh_f32(int64_t a,int64_t c,int64_t n){(void)a;(void)c;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_tanh_bwd_f32(int64_t g,int64_t x,int64_t gi,int64_t n){(void)g;(void)x;(void)gi;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_gelu_f32(int64_t a,int64_t c,int64_t n){(void)a;(void)c;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_gelu_bwd_f32(int64_t g,int64_t x,int64_t gi,int64_t n){(void)g;(void)x;(void)gi;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_softmax_f32(int64_t x,int64_t r,int64_t c){(void)x;(void)r;(void)c;return 0;}
TZ_EXPORT int64_t tz_gpu_add_bias_f32(int64_t x,int64_t b,int64_t bt,int64_t d){(void)x;(void)b;(void)bt;(void)d;return 0;}
TZ_EXPORT int64_t tz_gpu_layernorm_f32(int64_t x,int64_t g,int64_t b,int64_t r,int64_t c,double e){(void)x;(void)g;(void)b;(void)r;(void)c;(void)e;return 0;}
TZ_EXPORT int64_t tz_gpu_layernorm_bwd_f32(int64_t dx,int64_t dy,int64_t x,int64_t g,int64_t r,int64_t c,double e){(void)dx;(void)dy;(void)x;(void)g;(void)r;(void)c;(void)e;return 0;}
TZ_EXPORT int64_t tz_gpu_batchnorm_f32(int64_t x,int64_t g,int64_t b,int64_t n,int64_t c,double e){(void)x;(void)g;(void)b;(void)n;(void)c;(void)e;return 0;}
TZ_EXPORT int64_t tz_gpu_dropout_f32(int64_t x,int64_t m,int64_t n,double r,int64_t s){(void)x;(void)m;(void)n;(void)r;(void)s;return 0;}
TZ_EXPORT int64_t tz_gpu_embedding_f32(int64_t i,int64_t t,int64_t o,int64_t n,int64_t d){(void)i;(void)t;(void)o;(void)n;(void)d;return 0;}
TZ_EXPORT int64_t tz_gpu_embedding_bwd_f32(int64_t gi,int64_t go,int64_t idx,int64_t n,int64_t d){(void)gi;(void)go;(void)idx;(void)n;(void)d;return 0;}
TZ_EXPORT int64_t tz_gpu_rope_f32(int64_t x,int64_t s,int64_t h,int64_t d){(void)x;(void)s;(void)h;(void)d;return 0;}
TZ_EXPORT int64_t tz_gpu_causal_mask_f32(int64_t s,int64_t bh,int64_t sq){(void)s;(void)bh;(void)sq;return 0;}
TZ_EXPORT int64_t tz_gpu_cross_entropy_f32(int64_t l,int64_t lb,int64_t lo,int64_t b,int64_t c){(void)l;(void)lb;(void)lo;(void)b;(void)c;return 0;}
TZ_EXPORT int64_t tz_gpu_cross_entropy_bwd_f32(int64_t l,int64_t lb,int64_t g,int64_t b,int64_t c){(void)l;(void)lb;(void)g;(void)b;(void)c;return 0;}
TZ_EXPORT int64_t tz_gpu_mse_loss_f32(int64_t p,int64_t t,int64_t lo,int64_t n){(void)p;(void)t;(void)lo;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_mse_bwd_f32(int64_t p,int64_t t,int64_t g,int64_t n){(void)p;(void)t;(void)g;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_adam_f32(int64_t p,int64_t g,int64_t m,int64_t v,double lr,double b1,double b2,double e,double b1t,double b2t,int64_t n){(void)p;(void)g;(void)m;(void)v;(void)lr;(void)b1;(void)b2;(void)e;(void)b1t;(void)b2t;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_sgd_f32(int64_t p,int64_t g,int64_t v,double lr,double mo,double wd,int64_t n){(void)p;(void)g;(void)v;(void)lr;(void)mo;(void)wd;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_reduce_sum_sq_f32(int64_t x,int64_t o,int64_t n){(void)x;(void)o;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_grad_clip_f32(int64_t g,int64_t n,double mx){(void)g;(void)n;(void)mx;return 0;}
TZ_EXPORT int64_t tz_gpu_f32_to_f64(int64_t s,int64_t d,int64_t n){(void)s;(void)d;(void)n;return 0;}
TZ_EXPORT int64_t tz_gpu_f64_to_f32(int64_t s,int64_t d,int64_t n){(void)s;(void)d;(void)n;return 0;}

// ── v2.1 backward pass ────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_gemm_nt_f32(int64_t A,int64_t B,int64_t C,int64_t M,int64_t K,int64_t N,double al,double be){(void)A;(void)B;(void)C;(void)M;(void)K;(void)N;(void)al;(void)be;return 0;}
TZ_EXPORT int64_t tz_gpu_gemm_tn_f32(int64_t A,int64_t B,int64_t C,int64_t M,int64_t K,int64_t N,double al,double be){(void)A;(void)B;(void)C;(void)M;(void)K;(void)N;(void)al;(void)be;return 0;}
TZ_EXPORT int64_t tz_gpu_reduce_sum_cols_f32(int64_t X,int64_t o,int64_t r,int64_t c){(void)X;(void)o;(void)r;(void)c;return 0;}
TZ_EXPORT int64_t tz_gpu_xavier_init_f32(int64_t W,int64_t n,int64_t fi,int64_t s){(void)W;(void)n;(void)fi;(void)s;return 0;}
