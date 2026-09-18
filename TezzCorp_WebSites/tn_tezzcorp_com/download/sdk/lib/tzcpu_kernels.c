// tzcpu_kernels.c -- TezzNative CPU Kernel Library v1.0
// Portable C99 implementation — runs on any CPU: x86_64, ARM64 (Snapdragon), RISC-V
//
// Windows: cl /O2 /LD /MD tzcpu_kernels.c /Fe:tzcpu.dll /link /DEF:tzcpu.def
//          or: gcc -O3 -shared -fPIC tzcpu_kernels.c -o tzcpu.dll -lm
// Linux:   gcc -O3 -shared -fPIC -fopenmp tzcpu_kernels.c -o libtzcpu.so -lm
// ARM64 (Snapdragon WoA):
//          cl /O2 /arch:arm64 /LD /MD tzcpu_kernels.c /Fe:tzcpu.dll
//
// OpenMP is optional — if not available the code still works, just single-threaded.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <pthread.h>
#  include <unistd.h>
#endif

#ifdef _OPENMP
#  include <omp.h>
#endif

#ifdef _WIN32
#  define TZ_CPU_EXPORT __declspec(dllexport)
#else
#  define TZ_CPU_EXPORT __attribute__((visibility("default")))
#endif

// ─── f32 pointer helpers ──────────────────────────────────────────────────────
static inline float* fp(int64_t p){ return (float*)(uintptr_t)p; }
static inline int*   ip(int64_t p){ return (int*  )(uintptr_t)p; }

// ─── Thread count ─────────────────────────────────────────────────────────────
static int tz_cpu_threads(){
#ifdef _OPENMP
  return omp_get_max_threads();
#else
  return 1;
#endif
}

// =============================================================================
//  EXPORTED C API — mirrors tzgpu.dll naming with tz_cpu_ prefix
// =============================================================================
#ifdef __cplusplus
extern "C" {
#endif

// ── Device probing ────────────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_info(){
  int threads = tz_cpu_threads();
  printf("[CPU] TezzNative CPU backend v1.0\n");
  printf("[CPU] OpenMP threads: %d\n", threads);
#if defined(__aarch64__) || defined(_M_ARM64)
  printf("[CPU] Architecture: ARM64 (Snapdragon / Apple Silicon compatible)\n");
#elif defined(__x86_64__) || defined(_M_X64)
  printf("[CPU] Architecture: x86-64\n");
#else
  printf("[CPU] Architecture: unknown\n");
#endif
  return threads;
}

// Returns 1 always (CPU is always available)
TZ_CPU_EXPORT int64_t tz_cpu_device_count(){ return 1; }

// ── Memory (aligned malloc for SIMD) ─────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_malloc(int64_t nbytes){
#ifdef _WIN32
  void* p = _aligned_malloc((size_t)nbytes, 64);
#else
  void* p = NULL;
  if(posix_memalign(&p, 64, (size_t)nbytes) != 0) p = NULL;
#endif
  if(!p) return 0;
  return (int64_t)(uintptr_t)p;
}

TZ_CPU_EXPORT int64_t tz_cpu_free(int64_t ptr){
  if(ptr){
#ifdef _WIN32
    _aligned_free((void*)(uintptr_t)ptr);
#else
    free((void*)(uintptr_t)ptr);
#endif
  }
  return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_zeros(int64_t n){
  int64_t p = tz_cpu_malloc(n * 4);
  if(p) memset((void*)(uintptr_t)p, 0, (size_t)(n * 4));
  return p;
}

TZ_CPU_EXPORT int64_t tz_cpu_memcpy(int64_t dst, int64_t src, int64_t nbytes){
  memcpy((void*)(uintptr_t)dst, (void*)(uintptr_t)src, (size_t)nbytes);
  return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_sync(){ return 0; }  // no-op on CPU

// ── f32 Elementwise ───────────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_fill_f32(int64_t a, int64_t n, double val){
  float v = (float)val; float* p = fp(a);
  for(int64_t i = 0; i < n; i++) p[i] = v;
  return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_copy_f32(int64_t src, int64_t dst, int64_t n){
  memcpy(fp(dst), fp(src), (size_t)(n * 4)); return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_add_f32(int64_t a, int64_t b, int64_t c, int64_t n){
  float *pa=fp(a),*pb=fp(b),*pc=fp(c);
  for(int64_t i=0;i<n;i++) pc[i]=pa[i]+pb[i]; return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_sub_f32(int64_t a, int64_t b, int64_t c, int64_t n){
  float *pa=fp(a),*pb=fp(b),*pc=fp(c);
  for(int64_t i=0;i<n;i++) pc[i]=pa[i]-pb[i]; return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_mul_f32(int64_t a, int64_t b, int64_t c, int64_t n){
  float *pa=fp(a),*pb=fp(b),*pc=fp(c);
  for(int64_t i=0;i<n;i++) pc[i]=pa[i]*pb[i]; return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_scale_f32(int64_t a, int64_t c, int64_t n, double s){
  float sv=(float)s; float *pa=fp(a),*pc=fp(c);
  for(int64_t i=0;i<n;i++) pc[i]=pa[i]*sv; return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_axpy_f32(int64_t y, int64_t x, double alpha, int64_t n){
  float a=(float)alpha; float *py=fp(y),*px=fp(x);
  for(int64_t i=0;i<n;i++) py[i]+=a*px[i]; return 0;
}

// ── Activations ───────────────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_relu_f32(int64_t a, int64_t c, int64_t n){
  float *pa=fp(a),*pc=fp(c);
  for(int64_t i=0;i<n;i++) pc[i]=pa[i]>0.f?pa[i]:0.f; return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_relu_bwd_f32(int64_t grad_out, int64_t x, int64_t grad_in, int64_t n){
  float *gout=fp(grad_out),*px=fp(x),*gin=fp(grad_in);
  for(int64_t i=0;i<n;i++) gin[i]=px[i]>0.f?gout[i]:0.f; return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_sigmoid_f32(int64_t a, int64_t c, int64_t n){
  float *pa=fp(a),*pc=fp(c);
  for(int64_t i=0;i<n;i++) pc[i]=1.f/(1.f+expf(-pa[i])); return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_tanh_f32(int64_t a, int64_t c, int64_t n){
  float *pa=fp(a),*pc=fp(c);
  for(int64_t i=0;i<n;i++) pc[i]=tanhf(pa[i]); return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_gelu_f32(int64_t a, int64_t c, int64_t n){
  // GELU approx: 0.5x(1+tanh(sqrt(2/pi)(x+0.044715x^3)))
  float *pa=fp(a),*pc=fp(c);
  const float k=0.7978845608f; // sqrt(2/pi)
  for(int64_t i=0;i<n;i++){
    float x=pa[i];
    pc[i]=0.5f*x*(1.f+tanhf(k*(x+0.044715f*x*x*x)));
  }
  return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_gelu_bwd_f32(int64_t grad_out, int64_t x, int64_t grad_in, int64_t n){
  float *gout=fp(grad_out),*px=fp(x),*gin=fp(grad_in);
  const float k=0.7978845608f;
  for(int64_t i=0;i<n;i++){
    float xi=px[i];
    float t=tanhf(k*(xi+0.044715f*xi*xi*xi));
    float dt=1.f-t*t;
    float gelu_d=0.5f*(1.f+t)+0.5f*xi*dt*k*(1.f+3.f*0.044715f*xi*xi);
    gin[i]=gout[i]*gelu_d;
  }
  return 0;
}

// ── Softmax (in-place, row-wise) ──────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_softmax_f32(int64_t x, int64_t rows, int64_t cols){
  float *px=fp(x);
  for(int64_t r=0;r<rows;r++){
    float *row=px+r*cols;
    float mx=row[0];
    for(int64_t c=1;c<cols;c++) if(row[c]>mx) mx=row[c];
    float s=0.f;
    for(int64_t c=0;c<cols;c++){ row[c]=expf(row[c]-mx); s+=row[c]; }
    for(int64_t c=0;c<cols;c++) row[c]/=s;
  }
  return 0;
}

// ── Add bias (broadcast) ──────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_add_bias_f32(int64_t x, int64_t b, int64_t batch, int64_t dim){
  float *px=fp(x),*pb=fp(b);
  for(int64_t r=0;r<batch;r++)
    for(int64_t j=0;j<dim;j++) px[r*dim+j]+=pb[j];
  return 0;
}

// ── Layer normalization ───────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_layernorm_f32(int64_t x, int64_t gamma, int64_t beta, int64_t rows, int64_t cols, double eps){
  float *px=fp(x),*pg=fp(gamma),*pb=fp(beta);
  float e=(float)eps;
  for(int64_t r=0;r<rows;r++){
    float *row=px+r*cols;
    float mu=0.f;
    for(int64_t c=0;c<cols;c++) mu+=row[c]; mu/=(float)cols;
    float va=0.f;
    for(int64_t c=0;c<cols;c++){ float d=row[c]-mu; va+=d*d; } va/=(float)cols;
    float inv=1.f/sqrtf(va+e);
    for(int64_t c=0;c<cols;c++)
      row[c]=(row[c]-mu)*inv*(pg?pg[c]:1.f)+(pb?pb[c]:0.f);
  }
  return 0;
}

// ── GEMM: C[M x N] = A[M x K] @ B[K x N] + beta*C  (row-major) ──────────────
// OpenMP parallelized for Snapdragon big.LITTLE cores
TZ_CPU_EXPORT int64_t tz_cpu_gemm_f32(int64_t A, int64_t B, int64_t C,
                                        int64_t M, int64_t K, int64_t N,
                                        double alpha, double beta){
  float *pA=fp(A),*pB=fp(B),*pC=fp(C);
  float fa=(float)alpha, fb=(float)beta;
  // Scale C by beta first
  if(fb==0.f) memset(pC,0,(size_t)(M*N*4));
  else if(fb!=1.f){ int64_t mn=M*N; for(int64_t i=0;i<mn;i++) pC[i]*=fb; }
  // GEMM: accumulate A @ B into C
  int m;
#ifdef _OPENMP
  #pragma omp parallel for schedule(dynamic,16)
#endif
  for(m=0;m<(int)M;m++){
    for(int64_t n=0;n<N;n++){
      float acc=0.f;
      const float *rowA=pA+m*K;
      for(int64_t k=0;k<K;k++) acc+=rowA[k]*pB[k*N+n];
      pC[m*N+n]+=fa*acc;
    }
  }
  return 0;
}

// C = A @ B^T  (for dW computation)
TZ_CPU_EXPORT int64_t tz_cpu_gemm_nt_f32(int64_t A, int64_t B, int64_t C,
                                           int64_t M, int64_t K, int64_t N,
                                           double alpha, double beta){
  float *pA=fp(A),*pB=fp(B),*pC=fp(C);
  float fa=(float)alpha, fb=(float)beta;
  if(fb==0.f) memset(pC,0,(size_t)(M*N*4));
  else if(fb!=1.f){ int64_t mn=M*N; for(int64_t i=0;i<mn;i++) pC[i]*=fb; }
  int m;
#ifdef _OPENMP
  #pragma omp parallel for schedule(dynamic,8)
#endif
  for(m=0;m<(int)M;m++){
    for(int64_t n=0;n<N;n++){
      float acc=0.f;
      const float *rowA=pA+m*K;
      const float *rowB=pB+n*K;  // B^T: col n of B^T = row n of B
      for(int64_t k=0;k<K;k++) acc+=rowA[k]*rowB[k];
      pC[m*N+n]+=fa*acc;
    }
  }
  return 0;
}

// C = A^T @ B  (for dX computation)
TZ_CPU_EXPORT int64_t tz_cpu_gemm_tn_f32(int64_t A, int64_t B, int64_t C,
                                           int64_t M, int64_t K, int64_t N,
                                           double alpha, double beta){
  float *pA=fp(A),*pB=fp(B),*pC=fp(C);
  float fa=(float)alpha, fb=(float)beta;
  if(fb==0.f) memset(pC,0,(size_t)(M*N*4));
  else if(fb!=1.f){ int64_t mn=M*N; for(int64_t i=0;i<mn;i++) pC[i]*=fb; }
  int m;
#ifdef _OPENMP
  #pragma omp parallel for schedule(dynamic,8)
#endif
  for(m=0;m<(int)M;m++){
    for(int64_t n=0;n<N;n++){
      float acc=0.f;
      for(int64_t k=0;k<K;k++) acc+=pA[k*M+m]*pB[k*N+n];  // A^T: row m of A^T = col m of A
      pC[m*N+n]+=fa*acc;
    }
  }
  return 0;
}

// ── Column-wise reduce (bias gradient) ───────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_reduce_sum_cols_f32(int64_t X, int64_t out, int64_t rows, int64_t cols){
  float *px=fp(X),*po=fp(out);
  for(int64_t r=0;r<rows;r++)
    for(int64_t c=0;c<cols;c++) po[c]+=px[r*cols+c];
  return 0;
}

// ── Cross-entropy loss (with softmax, in-place) ───────────────────────────────
// logits: [batch x classes] f32 (modified in-place to become softmax probs)
// labels: [batch] int32
// loss_buf: [batch] f32 output
TZ_CPU_EXPORT int64_t tz_cpu_cross_entropy_f32(int64_t logits, int64_t labels, int64_t loss_buf, int64_t batch, int64_t classes){
  float *pl=fp(logits); int *plbl=ip(labels); float *ploss=fp(loss_buf);
  for(int64_t b=0;b<batch;b++){
    float *row=pl+b*classes;
    // Softmax
    float mx=row[0];
    for(int64_t c=1;c<classes;c++) if(row[c]>mx) mx=row[c];
    float s=0.f;
    for(int64_t c=0;c<classes;c++){ row[c]=expf(row[c]-mx); s+=row[c]; }
    for(int64_t c=0;c<classes;c++) row[c]/=s;
    // Cross-entropy: -log(p[label])
    int lbl=plbl[b]; if(lbl<0||lbl>=(int)classes) lbl=0;
    float p=row[lbl]; if(p<1e-7f) p=1e-7f;
    ploss[b]=-logf(p);
  }
  return 0;
}

// Cross-entropy backward: grad = (softmax_probs - one_hot) / batch
// logits must have been run through cross_entropy_f32 first (now contains softmax probs)
TZ_CPU_EXPORT int64_t tz_cpu_cross_entropy_bwd_f32(int64_t logits, int64_t labels, int64_t grad, int64_t batch, int64_t classes){
  float *pl=fp(logits); int *plbl=ip(labels); float *pg=fp(grad);
  float scale=1.f/(float)batch;
  for(int64_t b=0;b<batch;b++){
    int lbl=plbl[b]; if(lbl<0||lbl>=(int)classes) lbl=0;
    for(int64_t c=0;c<classes;c++){
      float p=pl[b*classes+c];
      pg[b*classes+c]=(c==(int64_t)lbl ? p-1.f : p)*scale;
    }
  }
  return 0;
}

// ── MSE loss ──────────────────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_mse_loss_f32(int64_t pred, int64_t target, int64_t loss_buf, int64_t n){
  float *pp=fp(pred),*pt=fp(target),*pl=fp(loss_buf);
  for(int64_t i=0;i<n;i++){ float d=pp[i]-pt[i]; pl[i]=d*d; }
  return 0;
}

// ── Dropout ───────────────────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_dropout_f32(int64_t x, int64_t mask, int64_t n, double rate, int64_t seed){
  float *px=fp(x),*pm=fp(mask);
  float keep=(float)(1.0-rate);
  float scale=(keep>0.f)?1.f/keep:0.f;
  // Simple LCG PRNG (deterministic given seed)
  uint64_t state=(uint64_t)seed;
  for(int64_t i=0;i<n;i++){
    state=state*6364136223846793005ULL+1442695040888963407ULL;
    float u=(float)((state>>33)&0x7FFFFF)/(float)0x7FFFFF;
    pm[i]=(u<keep)?scale:0.f;
    px[i]*=pm[i];
  }
  return 0;
}

// ── Adam optimizer ────────────────────────────────────────────────────────────
// p[i] -= lr * m_hat[i] / (sqrt(v_hat[i]) + eps)
TZ_CPU_EXPORT int64_t tz_cpu_adam_f32(int64_t p, int64_t g, int64_t m, int64_t v,
                                        double lr, double b1, double b2, double eps,
                                        double b1t, double b2t, int64_t n){
  float *pp=fp(p),*pg=fp(g),*pm=fp(m),*pv=fp(v);
  float flr=(float)lr,fb1=(float)b1,fb2=(float)b2,feps=(float)eps;
  float bc1=1.f-(float)b1t, bc2=1.f-(float)b2t;
  for(int64_t i=0;i<n;i++){
    float gi=pg[i];
    pm[i]=fb1*pm[i]+(1.f-fb1)*gi;
    pv[i]=fb2*pv[i]+(1.f-fb2)*gi*gi;
    float mh=pm[i]/bc1;
    float vh=pv[i]/bc2;
    pp[i]-=flr*mh/(sqrtf(vh)+feps);
  }
  return 0;
}

// ── SGD with momentum ─────────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_sgd_f32(int64_t p, int64_t g, int64_t vel, double lr, double momentum, double wd, int64_t n){
  float *pp=fp(p),*pg=fp(g),*pv=fp(vel);
  float flr=(float)lr,fm=(float)momentum,fwd=(float)wd;
  for(int64_t i=0;i<n;i++){
    float gi=pg[i]+fwd*pp[i];
    pv[i]=fm*pv[i]+gi;
    pp[i]-=flr*pv[i];
  }
  return 0;
}

// ── Xavier / He init ──────────────────────────────────────────────────────────
// Box-Muller transform for N(0, std)
TZ_CPU_EXPORT int64_t tz_cpu_xavier_init_f32(int64_t W, int64_t n, int64_t fan_in, int64_t seed){
  float *pw=fp(W);
  float std=sqrtf(2.f/(float)fan_in);
  uint64_t state=(uint64_t)(seed^0xDEADBEEF);
  const float TWO_PI=6.28318530718f;
  for(int64_t i=0;i<n;i+=2){
    state=state*6364136223846793005ULL+1442695040888963407ULL;
    float u1=(float)((state>>33)&0x7FFFFF)/(float)0x7FFFFF+1e-7f;
    state=state*6364136223846793005ULL+1442695040888963407ULL;
    float u2=(float)((state>>33)&0x7FFFFF)/(float)0x7FFFFF;
    float mag=std*sqrtf(-2.f*logf(u1));
    pw[i]=mag*cosf(TWO_PI*u2);
    if(i+1<n) pw[i+1]=mag*sinf(TWO_PI*u2);
  }
  return 0;
}

// ── Grad clipping ─────────────────────────────────────────────────────────────
TZ_CPU_EXPORT int64_t tz_cpu_grad_clip_f32(int64_t g, int64_t n, double max_norm){
  float *pg=fp(g);
  float norm=0.f;
  for(int64_t i=0;i<n;i++) norm+=pg[i]*pg[i];
  norm=sqrtf(norm);
  if(norm>(float)max_norm){
    float scale=(float)max_norm/norm;
    for(int64_t i=0;i<n;i++) pg[i]*=scale;
  }
  return 0;
}

#ifdef __cplusplus
}
#endif

// ── Loss reporting (reads float buffer, prints real loss value) ───────────────
// Computes mean of loss_buf[0..n), prints "[Epoch %d] loss = %.4f", 
// returns (int)(mean_loss * 10000) so Tezz can compare it as an integer.
TZ_CPU_EXPORT int64_t tz_cpu_report_loss(int64_t buf, int64_t n, int64_t epoch){
  float *p = fp(buf);
  float sum = 0.f;
  for(int64_t i = 0; i < n; i++) sum += p[i];
  float mean = sum / (float)n;
  printf("[Epoch %2lld] loss = %.6f\n", (long long)epoch, mean);
  fflush(stdout);
  return (int64_t)(mean * 10000.0f);  // return scaled int for comparison in Tezz
}

// ── Cycling label fill (i32, for diverse synthetic data) ─────────────────────
// Fills buf[0..n) with int32 labels: 0,1,2,...,classes-1,0,1,...
// Ensures every class appears, giving the network a real learning signal.
TZ_CPU_EXPORT int64_t tz_cpu_fill_labels_cycle_i32(int64_t buf, int64_t n, int64_t classes){
  int *p = ip(buf);
  for(int64_t i = 0; i < n; i++) p[i] = (int)(i % classes);
  return 0;
}

// ── Diverse synthetic input generator ────────────────────────────────────────
// Creates N samples where sample i (with label i%classes) has:
//   pixels in band [k*band_w .. (k+1)*band_w) = 0.9
//   all other pixels = 0.1
// This gives each class a DISTINCT input pattern -> network can learn.
TZ_CPU_EXPORT int64_t tz_cpu_fill_inputs_by_class(int64_t buf, int64_t n, int64_t in_dim, int64_t classes){
  float *p = fp(buf);
  int band_w = (int)(in_dim / classes);  // pixels per class band
  for(int64_t i = 0; i < n; i++){
    int cls = (int)(i % classes);
    float *row = p + i * in_dim;
    // Background = 0.1
    for(int64_t j = 0; j < in_dim; j++) row[j] = 0.1f;
    // Class band = 0.9
    int start = cls * band_w;
    int end   = start + band_w;
    if(end > (int)in_dim) end = (int)in_dim;
    for(int j = start; j < end; j++) row[j] = 0.9f;
  }
  return 0;
}

// =============================================================================
//  NEW KERNELS v2.0 — Transformer / LLM / ARM64-ABI-safe
// =============================================================================

// ── 1. get_loss_f32 ───────────────────────────────────────────────────────────
// Returns the mean of buf[n] as int64 via bit-cast.
// Fixes Gap #3: Tezz code can now read the loss as a float programmatically.
//   float mean = *(float*)&result;  (C side)
//   In Tezz:  loss_i: int = tz_cpu_get_loss_f32(buf, n)
//             → bit-cast back in C analysis or via a reinterpret helper
TZ_CPU_EXPORT int64_t tz_cpu_get_loss_f32(int64_t buf, int64_t n){
  float *p = fp(buf);
  double sum = 0.0;
  for(int64_t i = 0; i < n; i++) sum += (double)p[i];
  float mean = (n > 0) ? (float)(sum / (double)n) : 0.0f;
  int64_t out = 0;
  memcpy(&out, &mean, 4);   // 32-bit float bits in low 32 of int64
  return out;
}

// ── 2. argmax_f32 ─────────────────────────────────────────────────────────────
// Returns index of the maximum value in x[n]. Used for greedy decoding.
TZ_CPU_EXPORT int64_t tz_cpu_argmax_f32(int64_t x, int64_t n){
  float *p = fp(x);
  int64_t best = 0;
  float   bval = p[0];
  for(int64_t i = 1; i < n; i++){
    if(p[i] > bval){ bval = p[i]; best = i; }
  }
  return best;
}

// ── 3. transpose_f32 ──────────────────────────────────────────────────────────
// out[N×M] = A[M×N]^T
TZ_CPU_EXPORT int64_t tz_cpu_transpose_f32(int64_t A, int64_t out,
                                            int64_t M, int64_t N){
  float *a = fp(A), *o = fp(out);
  for(int64_t i = 0; i < M; i++)
    for(int64_t j = 0; j < N; j++)
      o[j*M + i] = a[i*N + j];
  return 0;
}

// ── 4. bmm_f32 ────────────────────────────────────────────────────────────────
// Batched matmul: C[B,M,N] = A[B,M,K] @ B_in[B,K,N]
TZ_CPU_EXPORT int64_t tz_cpu_bmm_f32(int64_t A, int64_t B_in, int64_t C,
                                      int64_t batch, int64_t M_,
                                      int64_t K_, int64_t N_){
  float *a = fp(A), *b = fp(B_in), *c = fp(C);
  int64_t m = M_, k = K_, n = N_;
  int bi;
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for(bi = 0; bi < (int)batch; bi++){
    float *Ab = a + bi*m*k;
    float *Bb = b + bi*k*n;
    float *Cb = c + bi*m*n;
    for(int64_t i = 0; i < m; i++){
      for(int64_t j = 0; j < n; j++){
        float s = 0.0f;
        for(int64_t kk = 0; kk < k; kk++) s += Ab[i*k+kk]*Bb[kk*n+j];
        Cb[i*n+j] = s;
      }
    }
  }
  return 0;
}

// ── 5. embedding_f32 ──────────────────────────────────────────────────────────
// Token lookup: out[T,D] = table[indices[T], :] where indices are int32
TZ_CPU_EXPORT int64_t tz_cpu_embedding_f32(int64_t indices, int64_t table,
                                            int64_t out,
                                            int64_t T, int64_t D){
  int   *idx = ip(indices);
  float *tab = fp(table);
  float *o   = fp(out);
  for(int64_t t = 0; t < T; t++){
    int id = idx[t];
    float *src = tab + (int64_t)id * D;
    float *dst = o   + t * D;
    memcpy(dst, src, (size_t)D * sizeof(float));
  }
  return 0;
}

// Backward: accumulate gradient into embedding table rows
TZ_CPU_EXPORT int64_t tz_cpu_embedding_bwd_f32(int64_t grad, int64_t indices,
                                                int64_t dtable,
                                                int64_t T, int64_t D,
                                                int64_t V){
  (void)V;  // vocab size — dtable is already allocated [V,D]
  float *g   = fp(grad);
  int   *idx = ip(indices);
  float *dt  = fp(dtable);
  for(int64_t t = 0; t < T; t++){
    int id = idx[t];
    float *gsrc = g  + t * D;
    float *gdst = dt + (int64_t)id * D;
    for(int64_t d = 0; d < D; d++) gdst[d] += gsrc[d];
  }
  return 0;
}

// ── 6. rmsnorm_f32 ────────────────────────────────────────────────────────────
// RMSNorm (LLaMA-style): out[B,D] = (x[B,D] / rms(x)) * w[D]
// eps passed as int64 bit-pattern of float (ARM64-safe)
TZ_CPU_EXPORT int64_t tz_cpu_rmsnorm_f32(int64_t x, int64_t w, int64_t out,
                                          int64_t B, int64_t D,
                                          int64_t eps_bits){
  float eps; memcpy(&eps, &eps_bits, 4);
  if(eps == 0.0f) eps = 1e-6f;
  float *xp = fp(x), *wp = fp(w), *op = fp(out);
  for(int64_t b = 0; b < B; b++){
    float *xrow = xp + b*D;
    float *orow = op + b*D;
    double ss = 0.0;
    for(int64_t d = 0; d < D; d++) ss += (double)xrow[d]*(double)xrow[d];
    float scale = 1.0f / sqrtf((float)(ss/(double)D) + eps);
    for(int64_t d = 0; d < D; d++) orow[d] = xrow[d] * scale * wp[d];
  }
  return 0;
}

// RMSNorm backward: dx, dw
TZ_CPU_EXPORT int64_t tz_cpu_rmsnorm_bwd_f32(int64_t dout, int64_t x,
                                              int64_t w,
                                              int64_t dx, int64_t dw,
                                              int64_t B, int64_t D,
                                              int64_t eps_bits){
  float eps; memcpy(&eps, &eps_bits, 4);
  if(eps == 0.0f) eps = 1e-6f;
  float *do_ = fp(dout), *xp = fp(x), *wp = fp(w);
  float *dxp = fp(dx),   *dwp= fp(dw);
  for(int64_t b = 0; b < B; b++){
    float *xrow  = xp  + b*D;
    float *dorow = do_ + b*D;
    float *dxrow = dxp + b*D;
    // rms = sqrt(mean(x^2) + eps)
    double ss = 0.0;
    for(int64_t d = 0; d < D; d++) ss += (double)xrow[d]*(double)xrow[d];
    float rms_inv = 1.0f / sqrtf((float)(ss/(double)D) + eps);
    // dx = (1/rms) * (dout*w - x * sum(dout*w*x) / (D*rms^2))
    double dot = 0.0;
    for(int64_t d = 0; d < D; d++) dot += (double)dorow[d]*wp[d]*xrow[d];
    float c = (float)(dot / (double)D) * rms_inv * rms_inv;
    for(int64_t d = 0; d < D; d++){
      dxrow[d] += (dorow[d]*wp[d] - xrow[d]*c) * rms_inv;
      dwp[d]   += dorow[d] * xrow[d] * rms_inv;  // accumulate dw
    }
  }
  return 0;
}

// ── 7. rope_f32 ───────────────────────────────────────────────────────────────
// Rotary Position Embedding: rotate pairs (x[2i], x[2i+1]) by theta^i * pos
// x: [T, D], out: [T, D], pos_offset: starting position index
TZ_CPU_EXPORT int64_t tz_cpu_rope_f32(int64_t x, int64_t out,
                                       int64_t T, int64_t D,
                                       int64_t pos_offset){
  float *xp = fp(x), *op = fp(out);
  int64_t half = D / 2;
  for(int64_t t = 0; t < T; t++){
    float *xrow = xp + t*D;
    float *orow = op + t*D;
    float pos = (float)(t + pos_offset);
    for(int64_t i = 0; i < half; i++){
      // theta_i = 1 / (10000^(2i/D))
      float theta = pos * powf(10000.0f, -(float)(2*i)/(float)D);
      float cos_t = cosf(theta), sin_t = sinf(theta);
      float x0 = xrow[i], x1 = xrow[i + half];
      orow[i]        = x0*cos_t - x1*sin_t;
      orow[i + half] = x0*sin_t + x1*cos_t;
    }
  }
  return 0;
}

// ── 8. attention_f32 ──────────────────────────────────────────────────────────
// Scaled dot-product attention with optional causal mask.
// Q,K,V: [B, T, D]  →  out: [B, T, D]
// is_causal=1: mask future positions (autoregressive)
// Uses a temporary score buffer allocated on heap.
TZ_CPU_EXPORT int64_t tz_cpu_attention_f32(int64_t Q, int64_t K, int64_t V,
                                            int64_t out,
                                            int64_t B, int64_t T, int64_t D,
                                            int64_t is_causal){
  float *q = fp(Q), *k = fp(K), *v = fp(V), *o = fp(out);
  float scale = 1.0f / sqrtf((float)D);
  // Allocate score matrix [T, T] per batch
  float *scores = (float*)malloc((size_t)(T*T)*sizeof(float));
  if(!scores) return -1;

  for(int64_t b = 0; b < B; b++){
    float *qb = q + b*T*D;
    float *kb = k + b*T*D;
    float *vb = v + b*T*D;
    float *ob = o + b*T*D;

    // 1. scores[i,j] = Q[i,:] · K[j,:] * scale
    for(int64_t i = 0; i < T; i++){
      for(int64_t j = 0; j < T; j++){
        float s = 0.0f;
        for(int64_t d = 0; d < D; d++) s += qb[i*D+d]*kb[j*D+d];
        scores[i*T+j] = s * scale;
        // Causal mask: future positions → -inf
        if(is_causal && j > i) scores[i*T+j] = -1e9f;
      }
    }

    // 2. Softmax over each row
    for(int64_t i = 0; i < T; i++){
      float *row = scores + i*T;
      float mx = row[0];
      for(int64_t j = 1; j < T; j++) if(row[j] > mx) mx = row[j];
      float sum = 0.0f;
      for(int64_t j = 0; j < T; j++){ row[j] = expf(row[j]-mx); sum += row[j]; }
      float inv = (sum > 0.0f) ? 1.0f/sum : 0.0f;
      for(int64_t j = 0; j < T; j++) row[j] *= inv;
    }

    // 3. out[i,:] = sum_j scores[i,j] * V[j,:]
    for(int64_t i = 0; i < T; i++){
      float *orow = ob + i*D;
      for(int64_t d = 0; d < D; d++) orow[d] = 0.0f;
      for(int64_t j = 0; j < T; j++){
        float a = scores[i*T+j];
        for(int64_t d = 0; d < D; d++) orow[d] += a * vb[j*D+d];
      }
    }
  }

  free(scores);
  return 0;
}

// ── attention_bwd_f32 ─────────────────────────────────────────────────────────
// Backward pass for scaled dot-product attention.
// dout: [B,T,D] → dQ, dK, dV: [B,T,D]  (accumulate)
// Requires the softmax scores from forward — recompute them here.
TZ_CPU_EXPORT int64_t tz_cpu_attention_bwd_f32(int64_t Q, int64_t K,
                                                int64_t V, int64_t dout,
                                                int64_t dQ, int64_t dK,
                                                int64_t dV,
                                                int64_t B, int64_t T,
                                                int64_t D,
                                                int64_t is_causal){
  float *q=fp(Q), *k=fp(K), *v=fp(V), *do_=fp(dout);
  float *dq=fp(dQ), *dk=fp(dK), *dv=fp(dV);
  float scale = 1.0f / sqrtf((float)D);
  float *scores = (float*)malloc((size_t)(T*T)*sizeof(float));
  float *dscores = (float*)malloc((size_t)(T*T)*sizeof(float));
  if(!scores || !dscores){ free(scores); free(dscores); return -1; }

  for(int64_t b = 0; b < B; b++){
    float *qb=q+b*T*D, *kb=k+b*T*D, *vb=v+b*T*D, *dob=do_+b*T*D;
    float *dqb=dq+b*T*D, *dkb=dk+b*T*D, *dvb=dv+b*T*D;

    // Recompute softmax scores
    for(int64_t i = 0; i < T; i++){
      for(int64_t j = 0; j < T; j++){
        float s = 0.0f;
        for(int64_t d = 0; d < D; d++) s += qb[i*D+d]*kb[j*D+d];
        scores[i*T+j] = s*scale;
        if(is_causal && j > i) scores[i*T+j] = -1e9f;
      }
      float *row=scores+i*T, mx=row[0];
      for(int64_t j=1;j<T;j++) if(row[j]>mx) mx=row[j];
      float sm=0.0f;
      for(int64_t j=0;j<T;j++){row[j]=expf(row[j]-mx);sm+=row[j];}
      float inv=(sm>0.f)?1.f/sm:0.f;
      for(int64_t j=0;j<T;j++) row[j]*=inv;
    }

    // dV += scores^T @ dout  (dV[j,:] += sum_i scores[i,j]*dout[i,:])
    for(int64_t j = 0; j < T; j++)
      for(int64_t d = 0; d < D; d++){
        float acc=0.0f;
        for(int64_t i=0;i<T;i++) acc += scores[i*T+j]*dob[i*D+d];
        dvb[j*D+d] += acc;
      }

    // dscores[i,j] = dout[i,:] · V[j,:]
    for(int64_t i=0;i<T;i++)
      for(int64_t j=0;j<T;j++){
        float s=0.0f;
        for(int64_t d=0;d<D;d++) s+=dob[i*D+d]*vb[j*D+d];
        dscores[i*T+j]=(is_causal&&j>i)?0.0f:s;
      }

    // Softmax backward: ds_raw[i,j] = p[i,j]*(dscores[i,j] - sum_k p[i,k]*dscores[i,k])
    for(int64_t i=0;i<T;i++){
      float dot=0.0f;
      for(int64_t j=0;j<T;j++) dot+=scores[i*T+j]*dscores[i*T+j];
      for(int64_t j=0;j<T;j++)
        dscores[i*T+j]=scores[i*T+j]*(dscores[i*T+j]-dot)*scale;
    }

    // dQ += dscores @ K
    for(int64_t i=0;i<T;i++)
      for(int64_t d=0;d<D;d++){
        float acc=0.0f;
        for(int64_t j=0;j<T;j++) acc+=dscores[i*T+j]*kb[j*D+d];
        dqb[i*D+d]+=acc;
      }
    // dK += dscores^T @ Q
    for(int64_t j=0;j<T;j++)
      for(int64_t d=0;d<D;d++){
        float acc=0.0f;
        for(int64_t i=0;i<T;i++) acc+=dscores[i*T+j]*qb[i*D+d];
        dkb[j*D+d]+=acc;
      }
  }

  free(scores); free(dscores);
  return 0;
}

// ── 9. conv2d_f32 ─────────────────────────────────────────────────────────────
// Direct 2D convolution: out[B, OutC, outH, outW] = conv(x[B, InC, H, W], W[OutC, InC, kH, kW]) + b[OutC]
// outH = (H + 2*pad - kH)/stride + 1, outW = (W + 2*pad - kW)/stride + 1
TZ_CPU_EXPORT int64_t tz_cpu_conv2d_f32(int64_t x, int64_t W, int64_t b, int64_t out,
                                         int64_t B, int64_t InC, int64_t H, int64_t W_in,
                                         int64_t OutC, int64_t kH, int64_t kW,
                                         int64_t pad, int64_t stride){
  float *xp = fp(x), *wp = fp(W), *bp = b ? fp(b) : NULL, *op = fp(out);
  int64_t outH = (H + 2 * pad - kH) / stride + 1;
  int64_t outW = (W_in + 2 * pad - kW) / stride + 1;
  if(outH <= 0 || outW <= 0) return -1;
  int bi;
#ifdef _OPENMP
  #pragma omp parallel for schedule(dynamic, 4)
#endif
  for(bi = 0; bi < (int)B; bi++){
    for(int64_t oc = 0; oc < OutC; oc++){
      float bias = bp ? bp[oc] : 0.0f;
      float *out_plane = op + (bi * OutC + oc) * (outH * outW);
      for(int64_t oh = 0; oh < outH; oh++){
        int64_t ih_base = oh * stride - pad;
        for(int64_t ow = 0; ow < outW; ow++){
          int64_t iw_base = ow * stride - pad;
          float acc = bias;
          for(int64_t ic = 0; ic < InC; ic++){
            const float *in_plane = xp + (bi * InC + ic) * (H * W_in);
            const float *wt_plane = wp + (oc * InC + ic) * (kH * kW);
            for(int64_t kh = 0; kh < kH; kh++){
              int64_t ih = ih_base + kh;
              if((uint64_t)ih < (uint64_t)H){
                for(int64_t kw = 0; kw < kW; kw++){
                  int64_t iw = iw_base + kw;
                  if((uint64_t)iw < (uint64_t)W_in){
                    acc += in_plane[ih * W_in + iw] * wt_plane[kh * kW + kw];
                  }
                }
              }
            }
          }
          out_plane[oh * outW + ow] = acc;
        }
      }
    }
  }
  return 0;
}

// ── 10. conv2d_bwd_f32 ────────────────────────────────────────────────────────
// Backward pass: computes dx[B, InC, H, W], dW[OutC, InC, kH, kW], db[OutC] from dout[B, OutC, outH, outW]
TZ_CPU_EXPORT int64_t tz_cpu_conv2d_bwd_f32(int64_t dout, int64_t x, int64_t W,
                                             int64_t dx, int64_t dW, int64_t db,
                                             int64_t B, int64_t InC, int64_t H, int64_t W_in,
                                             int64_t OutC, int64_t kH, int64_t kW,
                                             int64_t pad, int64_t stride){
  float *dop = fp(dout), *xp = fp(x), *wp = fp(W);
  float *dxp = dx ? fp(dx) : NULL;
  float *dwp = dW ? fp(dW) : NULL;
  float *dbp = db ? fp(db) : NULL;
  int64_t outH = (H + 2 * pad - kH) / stride + 1;
  int64_t outW = (W_in + 2 * pad - kW) / stride + 1;
  if(outH <= 0 || outW <= 0) return -1;

  if(dxp) memset(dxp, 0, (size_t)(B * InC * H * W_in * sizeof(float)));

  for(int64_t bi = 0; bi < B; bi++){
    for(int64_t oc = 0; oc < OutC; oc++){
      const float *do_plane = dop + (bi * OutC + oc) * (outH * outW);
      for(int64_t oh = 0; oh < outH; oh++){
        int64_t ih_base = oh * stride - pad;
        for(int64_t ow = 0; ow < outW; ow++){
          float g = do_plane[oh * outW + ow];
          int64_t iw_base = ow * stride - pad;
          if(dbp) dbp[oc] += g;
          for(int64_t ic = 0; ic < InC; ic++){
            const float *in_plane = xp + (bi * InC + ic) * (H * W_in);
            float *dw_plane = dwp ? (dwp + (oc * InC + ic) * (kH * kW)) : NULL;
            const float *w_plane = wp + (oc * InC + ic) * (kH * kW);
            float *dx_plane = dxp ? (dxp + (bi * InC + ic) * (H * W_in)) : NULL;
            for(int64_t kh = 0; kh < kH; kh++){
              int64_t ih = ih_base + kh;
              if((uint64_t)ih < (uint64_t)H){
                for(int64_t kw = 0; kw < kW; kw++){
                  int64_t iw = iw_base + kw;
                  if((uint64_t)iw < (uint64_t)W_in){
                    if(dw_plane) dw_plane[kh * kW + kw] += g * in_plane[ih * W_in + iw];
                    if(dx_plane) dx_plane[ih * W_in + iw] += g * w_plane[kh * kW + kw];
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return 0;
}

// ── 11. maxpool2d_f32 ─────────────────────────────────────────────────────────
// Max pooling: out[B, C, outH, outW], mask stores winning index for backprop
TZ_CPU_EXPORT int64_t tz_cpu_maxpool2d_f32(int64_t x, int64_t out, int64_t mask,
                                            int64_t B, int64_t C, int64_t H, int64_t W,
                                            int64_t kH, int64_t kW, int64_t stride){
  float *xp = fp(x), *op = fp(out);
  int32_t *mp = mask ? ip(mask) : NULL;
  int64_t outH = (H - kH) / stride + 1;
  int64_t outW = (W - kW) / stride + 1;
  if(outH <= 0 || outW <= 0) return -1;

  for(int64_t bc = 0; bc < B * C; bc++){
    const float *in_plane = xp + bc * (H * W);
    float *out_plane = op + bc * (outH * outW);
    int32_t *mask_plane = mp ? (mp + bc * (outH * outW)) : NULL;
    for(int64_t oh = 0; oh < outH; oh++){
      for(int64_t ow = 0; ow < outW; ow++){
        float mx = -1e30f;
        int32_t best_idx = 0;
        for(int64_t kh = 0; kh < kH; kh++){
          int64_t ih = oh * stride + kh;
          for(int64_t kw = 0; kw < kW; kw++){
            int64_t iw = ow * stride + kw;
            int64_t idx = ih * W + iw;
            if(in_plane[idx] > mx){ mx = in_plane[idx]; best_idx = (int32_t)idx; }
          }
        }
        out_plane[oh * outW + ow] = mx;
        if(mask_plane) mask_plane[oh * outW + ow] = best_idx;
      }
    }
  }
  return 0;
}

// Backward pass for MaxPool2D: unpools dout into dx using argmax indices stored in mask
TZ_CPU_EXPORT int64_t tz_cpu_maxpool2d_bwd_f32(int64_t dout, int64_t mask, int64_t dx,
                                                int64_t B, int64_t C, int64_t H, int64_t W,
                                                int64_t outH, int64_t outW){
  float *dop = fp(dout), *dxp = fp(dx);
  const int32_t *mp = ip(mask);
  if(!dxp || !dop || !mp) return -1;
  memset(dxp, 0, (size_t)(B * C * H * W * sizeof(float)));

  for(int64_t bc = 0; bc < B * C; bc++){
    const float *do_plane = dop + bc * (outH * outW);
    const int32_t *m_plane = mp + bc * (outH * outW);
    float *dx_plane = dxp + bc * (H * W);
    for(int64_t i = 0; i < outH * outW; i++){
      int32_t best_idx = m_plane[i];
      if((uint64_t)best_idx < (uint64_t)(H * W)){
        dx_plane[best_idx] += do_plane[i];
      }
    }
  }
  return 0;
}

// ── 12. topk_f32 ──────────────────────────────────────────────────────────────
// Finds top-K values and indices from x[n]
TZ_CPU_EXPORT int64_t tz_cpu_topk_f32(int64_t x, int64_t out_vals, int64_t out_indices,
                                       int64_t n, int64_t k){
  float *xp = fp(x), *ov = fp(out_vals);
  int32_t *oi = ip(out_indices);
  if(k > n) k = n;

  // Track chosen indices
  char *chosen = (char*)calloc((size_t)n, 1);
  if(!chosen) return -1;

  for(int64_t ki = 0; ki < k; ki++){
    float mx = -1e30f;
    int64_t best_j = -1;
    for(int64_t j = 0; j < n; j++){
      if(!chosen[j] && xp[j] > mx){ mx = xp[j]; best_j = j; }
    }
    if(best_j >= 0){
      chosen[best_j] = 1;
      if(ov) ov[ki] = mx;
      if(oi) oi[ki] = (int32_t)best_j;
    }
  }
  free(chosen);
  return 0;
}

// ── 13. sample_f32 ────────────────────────────────────────────────────────────
// Temperature + Top-P / Top-K nucleus sampling from logits[n]
// seed is stateful; returns selected token ID (0..n-1)
static uint64_t s_rng_state = 88172645463325252ULL;
TZ_CPU_EXPORT int64_t tz_cpu_sample_f32(int64_t logits, int64_t n,
                                         double temperature, double top_p, int64_t top_k,
                                         int64_t seed){
  float *lp = fp(logits);
  if(n <= 0) return 0;
  if(seed > 0) s_rng_state = (uint64_t)seed;

  float temp = (float)temperature;
  if(temp <= 1e-4f){
    // Greedy argmax
    int64_t best = 0;
    for(int64_t i = 1; i < n; i++) if(lp[i] > lp[best]) best = i;
    return best;
  }

  // Softmax with temperature
  float *probs = (float*)malloc((size_t)n * sizeof(float));
  if(!probs) return 0;
  float mx = lp[0] / temp;
  for(int64_t i = 1; i < n; i++) if(lp[i] / temp > mx) mx = lp[i] / temp;
  float sum = 0.0f;
  for(int64_t i = 0; i < n; i++){ probs[i] = expf(lp[i] / temp - mx); sum += probs[i]; }
  float inv_sum = (sum > 0.0f) ? 1.0f / sum : 0.0f;
  for(int64_t i = 0; i < n; i++) probs[i] *= inv_sum;

  // Xorshift64 random float in [0, 1)
  s_rng_state ^= (s_rng_state << 13);
  s_rng_state ^= (s_rng_state >> 7);
  s_rng_state ^= (s_rng_state << 17);
  float r = (float)(s_rng_state & 0xFFFFFFFFFFF) / (float)0x100000000000;

  // Cumulative distribution sampling
  float cum = 0.0f;
  int64_t choice = n - 1;
  for(int64_t i = 0; i < n; i++){
    cum += probs[i];
    if(r <= cum){ choice = i; break; }
  }
  free(probs);
  return choice;
}

// ── 14. kv_cache_update_f32 ───────────────────────────────────────────────────
// Rolling KV-cache insert: cache[B, H, max_seq, D] at position `pos`
// k_new/v_new: [B, H, 1, D]
TZ_CPU_EXPORT int64_t tz_cpu_kv_cache_update_f32(int64_t cache, int64_t new_val,
                                                  int64_t B, int64_t H, int64_t max_seq,
                                                  int64_t D, int64_t pos){
  float *c = fp(cache), *v = fp(new_val);
  if(pos >= max_seq) return -1;
  for(int64_t b = 0; b < B; b++){
    for(int64_t h = 0; h < H; h++){
      float *dst = c + ((b * H + h) * max_seq + pos) * D;
      const float *src = v + (b * H + h) * D;
      memcpy(dst, src, (size_t)D * sizeof(float));
    }
  }
  return 0;
}

// ── 15. masked_softmax_f32 ────────────────────────────────────────────────────
// Softmax with mask: mask=1 enables, mask=0 sets logit to -1e9f
TZ_CPU_EXPORT int64_t tz_cpu_masked_softmax_f32(int64_t x, int64_t mask, int64_t out,
                                                 int64_t rows, int64_t cols){
  float *xp = fp(x), *op = fp(out);
  const int32_t *mp = mask ? ip(mask) : NULL;
  for(int64_t r = 0; r < rows; r++){
    const float *xrow = xp + r * cols;
    float *orow = op + r * cols;
    const int32_t *mrow = mp ? (mp + r * cols) : NULL;
    float mx = -1e30f;
    for(int64_t c = 0; c < cols; c++){
      float v = (mrow && !mrow[c]) ? -1e9f : xrow[c];
      orow[c] = v;
      if(v > mx) mx = v;
    }
    float s = 0.0f;
    for(int64_t c = 0; c < cols; c++){ orow[c] = expf(orow[c] - mx); s += orow[c]; }
    float inv = (s > 0.0f) ? 1.0f / s : 0.0f;
    for(int64_t c = 0; c < cols; c++) orow[c] *= inv;
  }
  return 0;
}

// ── 16. concat_f32 & split_f32 ────────────────────────────────────────────────
// Concatenate 2 tensors along dimension 1: A[M, N1], B[M, N2] -> Out[M, N1 + N2]
TZ_CPU_EXPORT int64_t tz_cpu_concat_f32(int64_t A, int64_t B, int64_t out,
                                         int64_t M, int64_t N1, int64_t N2){
  float *pa = fp(A), *pb = fp(B), *po = fp(out);
  int64_t outN = N1 + N2;
  for(int64_t m = 0; m < M; m++){
    memcpy(po + m * outN,      pa + m * N1, (size_t)N1 * sizeof(float));
    memcpy(po + m * outN + N1, pb + m * N2, (size_t)N2 * sizeof(float));
  }
  return 0;
}

// Split tensor along dimension 1: In[M, N1 + N2] -> A[M, N1], B[M, N2]
TZ_CPU_EXPORT int64_t tz_cpu_split_f32(int64_t in, int64_t A, int64_t B,
                                        int64_t M, int64_t N1, int64_t N2){
  float *pi = fp(in), *pa = fp(A), *pb = fp(B);
  int64_t inN = N1 + N2;
  for(int64_t m = 0; m < M; m++){
    if(pa) memcpy(pa + m * N1, pi + m * inN,      (size_t)N1 * sizeof(float));
    if(pb) memcpy(pb + m * N2, pi + m * inN + N1, (size_t)N2 * sizeof(float));
  }
  return 0;
}

// ── 17. FP16 half-precision conversion ────────────────────────────────────────
// Single-precision float32 <-> IEEE 754 half-precision float16 (uint16_t)
static uint16_t f32_to_f16_val(float val){
  uint32_t x; memcpy(&x, &val, 4);
  uint32_t sign = (x >> 31) & 1;
  int32_t  exp  = ((x >> 23) & 0xFF) - 127 + 15;
  uint32_t frac = (x >> 13) & 0x3FF;
  if(exp <= 0) return (uint16_t)(sign << 15);
  if(exp >= 31) return (uint16_t)((sign << 15) | 0x7C00);
  return (uint16_t)((sign << 15) | (exp << 10) | frac);
}

static float f16_to_f32_val(uint16_t val){
  uint32_t sign = (val >> 15) & 1;
  int32_t  exp  = ((val >> 10) & 0x1F) - 15 + 127;
  uint32_t frac = (val & 0x3FF) << 13;
  if(exp <= 112) exp = 0;
  uint32_t x = (sign << 31) | ((uint32_t)exp << 23) | frac;
  float f; memcpy(&f, &x, 4);
  return f;
}

TZ_CPU_EXPORT int64_t tz_cpu_f32_to_f16(int64_t src, int64_t dst, int64_t n){
  float *sp = fp(src); uint16_t *dp = (uint16_t*)(uintptr_t)(uint64_t)dst;
  for(int64_t i = 0; i < n; i++) dp[i] = f32_to_f16_val(sp[i]);
  return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_f16_to_f32(int64_t src, int64_t dst, int64_t n){
  uint16_t *sp = (uint16_t*)(uintptr_t)(uint64_t)src; float *dp = fp(dst);
  for(int64_t i = 0; i < n; i++) dp[i] = f16_to_f32_val(sp[i]);
  return 0;
}

// ── 18. int8_gemm (quantized matrix multiply with scale) ──────────────────────
// C[M, N] (f32) = (A[M, K] (int8) @ B[K, N] (int8)) * (scaleA * scaleB)
TZ_CPU_EXPORT int64_t tz_cpu_int8_gemm(int64_t A, int64_t B, int64_t C,
                                        int64_t M, int64_t K, int64_t N,
                                        double scaleA, double scaleB){
  const int8_t *pA = (const int8_t*)(uintptr_t)(uint64_t)A;
  const int8_t *pB = (const int8_t*)(uintptr_t)(uint64_t)B;
  float *pC = fp(C);
  float s = (float)(scaleA * scaleB);
  int m;
#ifdef _OPENMP
  #pragma omp parallel for schedule(dynamic, 8)
#endif
  for(m = 0; m < (int)M; m++){
    for(int64_t n = 0; n < N; n++){
      int32_t acc = 0;
      for(int64_t k = 0; k < K; k++) acc += (int32_t)pA[m * K + k] * (int32_t)pB[k * N + n];
      pC[m * N + n] = (float)acc * s;
    }
  }
  return 0;
}

// ── 19. Diagnostic & Validation Tools ─────────────────────────────────────────
// Print tensor summary and first N elements
TZ_CPU_EXPORT int64_t tz_cpu_print_tensor_f32(int64_t ptr, int64_t n, int64_t max_print){
  float *p = fp(ptr);
  if(!p || n <= 0){ printf("[tensor] null or empty (n=%lld)\n", (long long)n); return 0; }
  float mn = p[0], mx = p[0];
  double sum = 0.0;
  for(int64_t i = 0; i < n; i++){
    if(p[i] < mn) mn = p[i];
    if(p[i] > mx) mx = p[i];
    sum += (double)p[i];
  }
  printf("[tensor size=%lld min=%.5f max=%.5f mean=%.5f]\n  [", (long long)n, mn, mx, (float)(sum / (double)n));
  int64_t k = (max_print > 0 && max_print < n) ? max_print : (n > 8 ? 8 : n);
  for(int64_t i = 0; i < k; i++) printf("%.4f%s", p[i], (i < k - 1) ? ", " : "");
  if(k < n) printf(", ... +%lld more", (long long)(n - k));
  printf("]\n");
  return 0;
}

static void print_indent(int spaces){
  for(int i = 0; i < spaces; i++) putchar(' ');
}

// PyTorch-style rich multi-dimensional tensor printing
TZ_CPU_EXPORT int64_t tz_cpu_print_tensor_formatted(
    int64_t ptr, int64_t d0, int64_t d1, int64_t d2, int64_t d3,
    int64_t ndim, int64_t device, int64_t requires_grad)
{
  float *p = fp(ptr);
  if(!p){ printf("tensor(null)\n"); return 0; }

  if(ndim <= 1){
    printf("tensor([");
    int64_t n = d0;
    if(n <= 8){
      for(int64_t i = 0; i < n; i++) printf("%.4f%s", p[i], (i < n - 1) ? ", " : "");
    } else {
      for(int64_t i = 0; i < 3; i++) printf("%.4f, ", p[i]);
      printf("..., ");
      for(int64_t i = n - 3; i < n; i++) printf("%.4f%s", p[i], (i < n - 1) ? ", " : "");
    }
    printf("]");
  } else if(ndim == 2){
    printf("tensor([");
    int64_t rows = d0;
    int64_t cols = d1;
    int truncate_rows = (rows > 8);
    int64_t r_limit = truncate_rows ? 3 : rows;

    for(int64_t r = 0; r < r_limit; r++){
      if(r > 0) print_indent(8);
      printf("[");
      if(cols <= 8){
        for(int64_t c = 0; c < cols; c++) printf("%7.4f%s", p[r * cols + c], (c < cols - 1) ? ", " : "");
      } else {
        for(int64_t c = 0; c < 3; c++) printf("%7.4f, ", p[r * cols + c]);
        printf("  ..., ");
        for(int64_t c = cols - 3; c < cols; c++) printf("%7.4f%s", p[r * cols + c], (c < cols - 1) ? ", " : "");
      }
      printf("]%s\n", (r < rows - 1) ? "," : "");
    }
    if(truncate_rows){
      print_indent(8); printf("...,\n");
      for(int64_t r = rows - 3; r < rows; r++){
        print_indent(8);
        printf("[");
        if(cols <= 8){
          for(int64_t c = 0; c < cols; c++) printf("%7.4f%s", p[r * cols + c], (c < cols - 1) ? ", " : "");
        } else {
          for(int64_t c = 0; c < 3; c++) printf("%7.4f, ", p[r * cols + c]);
          printf("  ..., ");
          for(int64_t c = cols - 3; c < cols; c++) printf("%7.4f%s", p[r * cols + c], (c < cols - 1) ? ", " : "");
        }
        printf("]%s\n", (r < rows - 1) ? "," : "");
      }
    }
    print_indent(7); printf("]");
  } else if(ndim == 3){
    printf("tensor([\n");
    int64_t planes = (d0 > 4) ? 2 : d0;
    int64_t rows = (d1 > 4) ? 4 : d1;
    int64_t cols = (d2 > 6) ? 6 : d2;
    for(int64_t pl = 0; pl < planes; pl++){
      print_indent(8); printf("[\n");
      for(int64_t r = 0; r < rows; r++){
        print_indent(10); printf("[");
        for(int64_t c = 0; c < cols; c++){
          int64_t idx = pl * (d1 * d2) + r * d2 + c;
          printf("%7.4f%s", p[idx], (c < cols - 1) ? ", " : "");
        }
        if(cols < d2) printf(", ...");
        printf("]%s\n", (r < rows - 1) ? "," : "");
      }
      if(rows < d1){ print_indent(10); printf("...\n"); }
      print_indent(8); printf("]%s\n", (pl < planes - 1) ? ",\n" : "");
    }
    if(planes < d0){ print_indent(8); printf("...\n"); }
    print_indent(7); printf("]");
  } else {
    // 4D Tensor: [d0, d1, d2, d3]
    printf("tensor([\n");
    int64_t b_max = (d0 > 2) ? 2 : d0;
    int64_t c_max = (d1 > 2) ? 2 : d1;
    int64_t h_max = (d2 > 4) ? 4 : d2;
    int64_t w_max = (d3 > 4) ? 4 : d3;
    for(int64_t b = 0; b < b_max; b++){
      print_indent(8); printf("[\n");
      for(int64_t c = 0; c < c_max; c++){
        print_indent(10); printf("[\n");
        for(int64_t h = 0; h < h_max; h++){
          print_indent(12); printf("[");
          for(int64_t w = 0; w < w_max; w++){
            int64_t idx = b * (d1 * d2 * d3) + c * (d2 * d3) + h * d3 + w;
            printf("%7.4f%s", p[idx], (w < w_max - 1) ? ", " : "");
          }
          if(w_max < d3) printf(", ...");
          printf("]%s\n", (h < h_max - 1) ? "," : "");
        }
        if(h_max < d2){ print_indent(12); printf("...\n"); }
        print_indent(10); printf("]%s\n", (c < c_max - 1) ? ",\n" : "");
      }
      if(c_max < d1){ print_indent(10); printf("...\n"); }
      print_indent(8); printf("]%s\n", (b < b_max - 1) ? ",\n" : "");
    }
    if(b_max < d0){ print_indent(8); printf("...\n"); }
    print_indent(7); printf("]");
  }

  // Footer attributes
  if(ndim == 1){
    printf(", shape=(%lld)", (long long)d0);
  } else if(ndim == 2){
    printf(", shape=(%lld, %lld)", (long long)d0, (long long)d1);
  } else if(ndim == 3){
    printf(", shape=(%lld, %lld, %lld)", (long long)d0, (long long)d1, (long long)d2);
  } else {
    printf(", shape=(%lld, %lld, %lld, %lld)", (long long)d0, (long long)d1, (long long)d2, (long long)d3);
  }
  printf(", dtype=f32");
  if(device == 1) printf(", device='cuda:0'");
  if(requires_grad == 1) printf(", requires_grad=True");
  printf(")\n");
  fflush(stdout);
  return 0;
}

// Check if all elements in tensor are finite (no NaN, no Inf)
TZ_CPU_EXPORT int64_t tz_cpu_check_finite_f32(int64_t ptr, int64_t n){
  float *p = fp(ptr);
  if(!p) return 0;
  for(int64_t i = 0; i < n; i++){
    uint32_t bits; memcpy(&bits, &p[i], 4);
    uint32_t exp = (bits >> 23) & 0xFF;
    if(exp == 0xFF) return 0; // NaN or Inf
  }
  return 1;
}

// Multi-Dimensional 4D Tensor Strided Slicing (Forward)
// Extracts sub-tensor src[s0:e0, s1:e1, s2:e2, s3:e3] into dst contiguous buffer
TZ_CPU_EXPORT int64_t tz_cpu_slice4d_f32(
    int64_t src_ptr, int64_t dst_ptr,
    int64_t s_d0, int64_t s_d1, int64_t s_d2, int64_t s_d3,
    int64_t s0, int64_t e0,
    int64_t s1, int64_t e1,
    int64_t s2, int64_t e2,
    int64_t s3, int64_t e3)
{
  float *src = fp(src_ptr);
  float *dst = fp(dst_ptr);
  if(!src || !dst) return -1;

  int64_t out_d0 = (e0 > s0) ? (e0 - s0) : 1;
  int64_t out_d1 = (e1 > s1) ? (e1 - s1) : 1;
  int64_t out_d2 = (e2 > s2) ? (e2 - s2) : 1;
  int64_t out_d3 = (e3 > s3) ? (e3 - s3) : 1;

  int64_t src_stride0 = s_d1 * s_d2 * s_d3;
  int64_t src_stride1 = s_d2 * s_d3;
  int64_t src_stride2 = s_d3;

  int64_t dst_stride0 = out_d1 * out_d2 * out_d3;
  int64_t dst_stride1 = out_d2 * out_d3;
  int64_t dst_stride2 = out_d3;

  int i0, i1, i2;
  #pragma omp parallel for private(i1, i2) schedule(static)
  for(i0 = 0; i0 < (int)out_d0; i0++){
    for(i1 = 0; i1 < (int)out_d1; i1++){
      for(i2 = 0; i2 < (int)out_d2; i2++){
        int64_t src_base = (s0 + (int64_t)i0) * src_stride0 + (s1 + (int64_t)i1) * src_stride1 + (s2 + (int64_t)i2) * src_stride2 + s3;
        int64_t dst_base = (int64_t)i0 * dst_stride0 + (int64_t)i1 * dst_stride1 + (int64_t)i2 * dst_stride2;
        memcpy(&dst[dst_base], &src[src_base], (size_t)out_d3 * sizeof(float));
      }
    }
  }
  return 0;
}

// Multi-Dimensional 4D Tensor Strided Slicing (Backward Gradient Scatter)
// Accumulates grad_out back into grad_in at [s0:e0, s1:e1, s2:e2, s3:e3]
TZ_CPU_EXPORT int64_t tz_cpu_slice4d_bwd_f32(
    int64_t grad_out_ptr, int64_t grad_in_ptr,
    int64_t s_d0, int64_t s_d1, int64_t s_d2, int64_t s_d3,
    int64_t s0, int64_t e0,
    int64_t s1, int64_t e1,
    int64_t s2, int64_t e2,
    int64_t s3, int64_t e3)
{
  float *grad_out = fp(grad_out_ptr);
  float *grad_in  = fp(grad_in_ptr);
  if(!grad_out || !grad_in) return -1;

  int64_t out_d0 = (e0 > s0) ? (e0 - s0) : 1;
  int64_t out_d1 = (e1 > s1) ? (e1 - s1) : 1;
  int64_t out_d2 = (e2 > s2) ? (e2 - s2) : 1;
  int64_t out_d3 = (e3 > s3) ? (e3 - s3) : 1;

  int64_t src_stride0 = s_d1 * s_d2 * s_d3;
  int64_t src_stride1 = s_d2 * s_d3;
  int64_t src_stride2 = s_d3;

  int64_t dst_stride0 = out_d1 * out_d2 * out_d3;
  int64_t dst_stride1 = out_d2 * out_d3;
  int64_t dst_stride2 = out_d3;

  int i0, i1, i2;
  #pragma omp parallel for private(i1, i2) schedule(static)
  for(i0 = 0; i0 < (int)out_d0; i0++){
    for(i1 = 0; i1 < (int)out_d1; i1++){
      for(i2 = 0; i2 < (int)out_d2; i2++){
        int64_t in_base  = (s0 + (int64_t)i0) * src_stride0 + (s1 + (int64_t)i1) * src_stride1 + (s2 + (int64_t)i2) * src_stride2 + s3;
        int64_t out_base = (int64_t)i0 * dst_stride0 + (int64_t)i1 * dst_stride1 + (int64_t)i2 * dst_stride2;
        for(int64_t i3 = 0; i3 < out_d3; i3++){
          grad_in[in_base + i3] += grad_out[out_base + i3];
        }
      }
    }
  }
  return 0;
}


// ── GGUF Ingestion & Quantization Kernels ────────────────────────────────────

#define GGUF_MAGIC 0x46554747 // 'GGUF'
#define MAX_GGUF_TENSORS 4096
#define MAX_GGUF_NAME 128
#define MAX_GGUF_DIMS 8

typedef enum {
  GGUF_TYPE_F32  = 0,
  GGUF_TYPE_F16  = 1,
  GGUF_TYPE_Q4_0 = 2,
  GGUF_TYPE_Q4_1 = 3,
  GGUF_TYPE_Q5_0 = 6,
  GGUF_TYPE_Q5_1 = 7,
  GGUF_TYPE_Q8_0 = 8,
  GGUF_TYPE_Q8_1 = 9,
  GGUF_TYPE_I8   = 24,
  GGUF_TYPE_I16  = 25,
  GGUF_TYPE_I32  = 26,
  GGUF_TYPE_I64  = 27,
  GGUF_TYPE_F64  = 28,
  GGUF_TYPE_BF16 = 30
} GgufTensorType;

#pragma pack(push, 1)
typedef struct {
  uint16_t d;       // fp16 scale
  uint8_t  qs[16];  // 32 nibbles
} block_q4_0;

typedef struct {
  uint16_t d;       // fp16 scale
  int8_t   qs[32];  // 32 int8 weights
} block_q8_0;
#pragma pack(pop)

typedef struct {
  char name[MAX_GGUF_NAME];
  uint32_t type;
  int64_t shape[MAX_GGUF_DIMS];
  int ndim;
  int64_t numel;
  uint64_t offset;
  uint64_t size_bytes;
} GgufTensorMeta;

typedef struct {
  FILE* fp;
  uint32_t version;
  uint64_t tensor_count;
  uint64_t kv_count;
  uint64_t data_base_offset;
  uint64_t file_size;
  GgufTensorMeta* tensors;
  int tensor_n;
} GgufFile;

// IEEE half to float conversion
static inline float gguf_f16_to_f32(uint16_t h) {
  uint32_t sign = (h & 0x8000u) << 16;
  uint32_t exp  = (h & 0x7C00u) >> 10;
  uint32_t mant = (h & 0x03FFu);
  if(exp == 0) {
    if(mant == 0) { uint32_t res = sign; float f; memcpy(&f, &res, 4); return f; }
    while(!(mant & 0x0400u)) { mant <<= 1; exp--; }
    exp++; mant &= ~0x0400u;
  } else if(exp == 31) {
    uint32_t res = sign | 0x7F800000u | (mant << 13); float f; memcpy(&f, &res, 4); return f;
  }
  exp = exp + (127 - 15);
  uint32_t res = sign | (exp << 23) | (mant << 13);
  float f; memcpy(&f, &res, 4); return f;
}

// Float to IEEE half conversion
static inline uint16_t gguf_f32_to_f16(float val) {
  uint32_t x;
  memcpy(&x, &val, 4);
  uint32_t sign = (x >> 31) & 1;
  int32_t exp = ((x >> 23) & 0xFF) - 127 + 15;
  uint32_t mant = (x & 0x7FFFFF) >> 13;
  if(exp <= 0) return (uint16_t)(sign << 15);
  if(exp >= 31) return (uint16_t)((sign << 15) | (31 << 10));
  return (uint16_t)((sign << 15) | (exp << 10) | mant);
}

// Dequantize Q4_0 block array
TZ_CPU_EXPORT int tz_gguf_dequantize_q4_0(const void* src, float* dst, int64_t k) {
  if(!src || !dst || k <= 0) return -1;
  const block_q4_0* blocks = (const block_q4_0*)src;
  int64_t nb = k / 32;

  for(int64_t b = 0; b < nb; b++) {
    float d = gguf_f16_to_f32(blocks[b].d);
    float* out = dst + b * 32;
    for(int i = 0; i < 16; i++) {
      uint8_t q = blocks[b].qs[i];
      int8_t v0 = (int8_t)(q & 0x0F) - 8;
      int8_t v1 = (int8_t)(q >> 4)   - 8;
      out[i]      = (float)v0 * d;
      out[i + 16] = (float)v1 * d;
    }
  }
  return 0;
}

// Dequantize Q8_0 block array
TZ_CPU_EXPORT int tz_gguf_dequantize_q8_0(const void* src, float* dst, int64_t k) {
  if(!src || !dst || k <= 0) return -1;
  const block_q8_0* blocks = (const block_q8_0*)src;
  int64_t nb = k / 32;

  for(int64_t b = 0; b < nb; b++) {
    float d = gguf_f16_to_f32(blocks[b].d);
    float* out = dst + b * 32;
    for(int i = 0; i < 32; i++) {
      out[i] = (float)blocks[b].qs[i] * d;
    }
  }
  return 0;
}

// Quantize float array to Q4_0
TZ_CPU_EXPORT int tz_gguf_quantize_q4_0(const float* src, void* dst, int64_t k) {
  if(!src || !dst || k <= 0) return -1;
  block_q4_0* blocks = (block_q4_0*)dst;
  int64_t nb = k / 32;

  for(int64_t b = 0; b < nb; b++) {
    const float* in = src + b * 32;
    float max_val = 0.0f;
    for(int i = 0; i < 32; i++) {
      float abs_v = (float)fabs(in[i]);
      if(abs_v > max_val) max_val = abs_v;
    }
    float d = max_val / 7.0f;
    if(d == 0.0f) d = 1e-6f;
    float id = 1.0f / d;
    blocks[b].d = gguf_f32_to_f16(d);

    for(int i = 0; i < 16; i++) {
      int8_t v0 = (int8_t)roundf(in[i] * id);
      int8_t v1 = (int8_t)roundf(in[i + 16] * id);
      if(v0 < -8) v0 = -8; if(v0 > 7) v0 = 7;
      if(v1 < -8) v1 = -8; if(v1 > 7) v1 = 7;
      uint8_t q0 = (uint8_t)(v0 + 8);
      uint8_t q1 = (uint8_t)(v1 + 8);
      blocks[b].qs[i] = (uint8_t)(q0 | (q1 << 4));
    }
  }
  return 0;
}

// Quantize float array to Q8_0
TZ_CPU_EXPORT int tz_gguf_quantize_q8_0(const float* src, void* dst, int64_t k) {
  if(!src || !dst || k <= 0) return -1;
  block_q8_0* blocks = (block_q8_0*)dst;
  int64_t nb = k / 32;

  for(int64_t b = 0; b < nb; b++) {
    const float* in = src + b * 32;
    float max_val = 0.0f;
    for(int i = 0; i < 32; i++) {
      float abs_v = (float)fabs(in[i]);
      if(abs_v > max_val) max_val = abs_v;
    }
    float d = max_val / 127.0f;
    if(d == 0.0f) d = 1e-6f;
    float id = 1.0f / d;
    blocks[b].d = gguf_f32_to_f16(d);

    for(int i = 0; i < 32; i++) {
      int v = (int)roundf(in[i] * id);
      if(v < -128) v = -128; if(v > 127) v = 127;
      blocks[b].qs[i] = (int8_t)v;
    }
  }
  return 0;
}

// Matrix-Vector product: y = W * x directly with Q4_0 weights
TZ_CPU_EXPORT int tz_gguf_gemv_q4_0(const void* w, const float* x, float* y, int64_t rows, int64_t cols) {
  if(!w || !x || !y) return -1;
  const block_q4_0* blocks = (const block_q4_0*)w;
  int64_t blocks_per_row = cols / 32;

  int64_t r;
  #pragma omp parallel for schedule(static)
  for(r = 0; r < rows; r++) {
    float sum = 0.0f;
    const block_q4_0* row_blocks = blocks + r * blocks_per_row;
    for(int64_t b = 0; b < blocks_per_row; b++) {
      float d = gguf_f16_to_f32(row_blocks[b].d);
      const float* x_ptr = x + b * 32;
      float row_sum = 0.0f;
      for(int i = 0; i < 16; i++) {
        uint8_t q = row_blocks[b].qs[i];
        int8_t v0 = (int8_t)(q & 0x0F) - 8;
        int8_t v1 = (int8_t)(q >> 4)   - 8;
        row_sum += (float)v0 * x_ptr[i] + (float)v1 * x_ptr[i + 16];
      }
      sum += row_sum * d;
    }
    y[r] = sum;
  }
  return 0;
}

// Skip a GGUF metadata value by type
static void skip_kv_value(FILE* fp, uint32_t val_type) {
  switch(val_type) {
    case 0: case 1: case 7: fseek(fp, 1, SEEK_CUR); break; // uint8, int8, bool
    case 2: case 3: fseek(fp, 2, SEEK_CUR); break;         // uint16, int16
    case 4: case 5: case 6: fseek(fp, 4, SEEK_CUR); break; // uint32, int32, float32
    case 10: case 11: case 12: fseek(fp, 8, SEEK_CUR); break; // uint64, int64, float64
    case 8: { // string
      uint64_t slen = 0;
      if(fread(&slen, 8, 1, fp) == 1) fseek(fp, (long)slen, SEEK_CUR);
      break;
    }
    case 9: { // array
      uint32_t elem_type = 0;
      uint64_t elem_count = 0;
      if(fread(&elem_type, 4, 1, fp) == 1 && fread(&elem_count, 8, 1, fp) == 1) {
        for(uint64_t i = 0; i < elem_count; i++) skip_kv_value(fp, elem_type);
      }
      break;
    }
    default: break;
  }
}

// Open and parse GGUF file
TZ_CPU_EXPORT int64_t tz_gguf_open(const char* path) {
  if(!path) return 0;
  FILE* fp = fopen(path, "rb");
  if(!fp) return 0;

  uint32_t magic = 0;
  if(fread(&magic, 4, 1, fp) != 1 || magic != GGUF_MAGIC) {
    fclose(fp);
    return 0;
  }

  GgufFile* gf = (GgufFile*)calloc(1, sizeof(GgufFile));
  if(!gf) { fclose(fp); return 0; }
  gf->fp = fp;

  fread(&gf->version, 4, 1, fp);
  fread(&gf->tensor_count, 8, 1, fp);
  fread(&gf->kv_count, 8, 1, fp);

  // Skip metadata KV pairs
  for(uint64_t i = 0; i < gf->kv_count; i++) {
    uint64_t klen = 0;
    if(fread(&klen, 8, 1, fp) != 1) break;
    fseek(fp, (long)klen, SEEK_CUR);
    uint32_t vtype = 0;
    if(fread(&vtype, 4, 1, fp) != 1) break;
    skip_kv_value(fp, vtype);
  }

  // Parse tensor metadata
  gf->tensors = (GgufTensorMeta*)calloc((size_t)gf->tensor_count, sizeof(GgufTensorMeta));
  gf->tensor_n = (int)gf->tensor_count;

  for(uint64_t i = 0; i < gf->tensor_count; i++) {
    uint64_t name_len = 0;
    if(fread(&name_len, 8, 1, fp) != 1) break;
    if(name_len >= MAX_GGUF_NAME) name_len = MAX_GGUF_NAME - 1;
    fread(gf->tensors[i].name, 1, (size_t)name_len, fp);
    gf->tensors[i].name[name_len] = '\0';

    uint32_t ndims = 0;
    fread(&ndims, 4, 1, fp);
    gf->tensors[i].ndim = (int)ndims;
    int64_t numel = 1;
    for(uint32_t d = 0; d < ndims; d++) {
      uint64_t dim_size = 0;
      fread(&dim_size, 8, 1, fp);
      if(d < MAX_GGUF_DIMS) gf->tensors[i].shape[d] = (int64_t)dim_size;
      numel *= (int64_t)dim_size;
    }
    gf->tensors[i].numel = numel;

    uint32_t type = 0;
    fread(&type, 4, 1, fp);
    gf->tensors[i].type = type;

    uint64_t off = 0;
    fread(&off, 8, 1, fp);
    gf->tensors[i].offset = off;
  }

  // Align to 32-byte boundary for data section
  long cur_pos = ftell(fp);
  gf->data_base_offset = (uint64_t)((cur_pos + 31) & ~31);

  return (int64_t)(intptr_t)gf;
}

TZ_CPU_EXPORT int64_t tz_gguf_num_tensors(int64_t handle) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  return gf ? (int64_t)gf->tensor_count : 0;
}

TZ_CPU_EXPORT const char* tz_gguf_tensor_name(int64_t handle, int64_t idx) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf || idx < 0 || idx >= (int64_t)gf->tensor_count) return "";
  return gf->tensors[idx].name;
}

TZ_CPU_EXPORT int64_t tz_gguf_tensor_type(int64_t handle, int64_t idx) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf || idx < 0 || idx >= (int64_t)gf->tensor_count) return -1;
  return (int64_t)gf->tensors[idx].type;
}

TZ_CPU_EXPORT int64_t tz_gguf_tensor_shape(int64_t handle, int64_t idx, int64_t dim_idx) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf || idx < 0 || idx >= (int64_t)gf->tensor_count || dim_idx < 0 || dim_idx >= gf->tensors[idx].ndim) return 1;
  return gf->tensors[idx].shape[dim_idx];
}

TZ_CPU_EXPORT int64_t tz_gguf_tensor_numel(int64_t handle, int64_t idx) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf || idx < 0 || idx >= (int64_t)gf->tensor_count) return 0;
  return gf->tensors[idx].numel;
}

TZ_CPU_EXPORT int tz_gguf_read_tensor_f32(int64_t handle, int64_t idx, float* dst) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf || idx < 0 || idx >= (int64_t)gf->tensor_count || !dst) return -1;

  GgufTensorMeta* tm = &gf->tensors[idx];
  uint64_t abs_offset = gf->data_base_offset + tm->offset;
  fseek(gf->fp, (long)abs_offset, SEEK_SET);

  if(tm->type == GGUF_TYPE_F32) {
    fread(dst, 4, (size_t)tm->numel, gf->fp);
    return 0;
  } else if(tm->type == GGUF_TYPE_F16) {
    uint16_t* buf = (uint16_t*)malloc((size_t)tm->numel * 2);
    fread(buf, 2, (size_t)tm->numel, gf->fp);
    for(int64_t i = 0; i < tm->numel; i++) dst[i] = gguf_f16_to_f32(buf[i]);
    free(buf);
    return 0;
  } else if(tm->type == GGUF_TYPE_Q4_0) {
    int64_t bytes = (tm->numel / 32) * (int64_t)sizeof(block_q4_0);
    void* buf = malloc((size_t)bytes);
    fread(buf, 1, (size_t)bytes, gf->fp);
    tz_gguf_dequantize_q4_0(buf, dst, tm->numel);
    free(buf);
    return 0;
  } else if(tm->type == GGUF_TYPE_Q8_0) {
    int64_t bytes = (tm->numel / 32) * (int64_t)sizeof(block_q8_0);
    void* buf = malloc((size_t)bytes);
    fread(buf, 1, (size_t)bytes, gf->fp);
    tz_gguf_dequantize_q8_0(buf, dst, tm->numel);
    free(buf);
    return 0;
  }
  return -2;
}

TZ_CPU_EXPORT int tz_gguf_close(int64_t handle) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf) return -1;
  if(gf->fp) fclose(gf->fp);
  if(gf->tensors) free(gf->tensors);
  free(gf);
  return 0;
}

// ── Tensor Element Accessors & Synthetic Dataset Helpers ──────────────────────

TZ_CPU_EXPORT int tz_cpu_set_f32(int64_t ptr, int64_t idx, float val) {
  float* p = fp(ptr);
  if (p) p[idx] = val;
  return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_get_f32(int64_t ptr, int64_t idx) {
  float* p = fp(ptr);
  if (!p) return 0;
  float val = p[idx];
  uint32_t bits;
  memcpy(&bits, &val, 4);
  return (int64_t)bits;
}

TZ_CPU_EXPORT int tz_cpu_fill_xor_data(int64_t x_ptr, int64_t y_ptr) {
  float* X = fp(x_ptr);
  int* Y = ip(y_ptr);
  if (X) {
    X[0] = 0.0f; X[1] = 0.0f;
    X[2] = 0.0f; X[3] = 1.0f;
    X[4] = 1.0f; X[5] = 0.0f;
    X[6] = 1.0f; X[7] = 1.0f;
  }
  if (Y) {
    Y[0] = 0;
    Y[1] = 1;
    Y[2] = 1;
    Y[3] = 0;
  }
  return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_stdin(void) {
  return (int64_t)(intptr_t)stdin;
}

TZ_CPU_EXPORT int64_t tz_cpu_stdout(void) {
  return (int64_t)(intptr_t)stdout;
}

TZ_CPU_EXPORT int64_t tz_cpu_stderr(void) {
  return (int64_t)(intptr_t)stderr;
}

typedef struct {
  void* (*fn)(void*);
  void* arg;
  int64_t result;
  int done;
#ifdef _WIN32
  HANDLE thread;
#else
  pthread_t thread;
#endif
} TzAsyncTask;

#ifdef _WIN32
static DWORD WINAPI tz_async_worker(LPVOID param) {
  TzAsyncTask* task = (TzAsyncTask*)param;
  if (task && task->fn) {
    task->result = (int64_t)(intptr_t)task->fn(task->arg);
    task->done = 1;
  }
  return 0;
}
#else
static void* tz_async_worker(void* param) {
  TzAsyncTask* task = (TzAsyncTask*)param;
  if (task && task->fn) {
    task->result = (int64_t)(intptr_t)task->fn(task->arg);
    task->done = 1;
  }
  return NULL;
}
#endif

TZ_CPU_EXPORT int64_t tz_task_spawn(int64_t fn_ptr, int64_t arg) {
  if (!fn_ptr) return 0;
  TzAsyncTask* task = (TzAsyncTask*)calloc(1, sizeof(TzAsyncTask));
  if (!task) return 0;
  task->fn = (void* (*)(void*))(intptr_t)fn_ptr;
  task->arg = (void*)(intptr_t)arg;
#ifdef _WIN32
  task->thread = CreateThread(NULL, 0, tz_async_worker, task, 0, NULL);
#else
  pthread_create(&task->thread, NULL, tz_async_worker, task);
#endif
  return (int64_t)(intptr_t)task;
}

TZ_CPU_EXPORT int64_t tz_task_await(int64_t task_handle) {
  if (!task_handle) return 0;
  TzAsyncTask* task = (TzAsyncTask*)(intptr_t)task_handle;
#ifdef _WIN32
  if (task->thread) {
    WaitForSingleObject(task->thread, INFINITE);
    CloseHandle(task->thread);
    task->thread = NULL;
  }
#else
  if (task->thread) {
    pthread_join(task->thread, NULL);
    task->thread = 0;
  }
#endif
  int64_t res = task->result;
  free(task);
  return res;
}

TZ_CPU_EXPORT int64_t tz_task_yield(void) {
#ifdef _WIN32
  Sleep(0);
#else
  usleep(0);
#endif
  return 0;
}

/* ==========================================================================
 * Native Win32 Double-Buffered Framebuffer & Host Windowing Subsystem
 * ========================================================================== */
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")

static HWND g_fb_hwnd = NULL;
static HDC g_fb_hdc = NULL;
static HDC g_fb_memdc = NULL;
static HBITMAP g_fb_hbmp = NULL;
static uint32_t* g_fb_pixels = NULL;
static int g_fb_w = 0;
static int g_fb_h = 0;
static int g_fb_closed = 0;
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static int g_mouse_prev_x = 0;
static int g_mouse_prev_y = 0;
static int g_mouse_btns = 0;
static int g_last_key = 0;
static int g_key_queue[64];
static int g_key_head = 0;
static int g_key_tail = 0;

static LRESULT CALLBACK TzFbWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_MOUSEMOVE:
      g_mouse_prev_x = g_mouse_x;
      g_mouse_prev_y = g_mouse_y;
      g_mouse_x = (int)(short)LOWORD(lp);
      g_mouse_y = (int)(short)HIWORD(lp);
      return 0;
    case WM_LBUTTONDOWN:
      g_mouse_btns |= 1;
      SetCapture(hwnd);
      return 0;
    case WM_LBUTTONUP:
      g_mouse_btns &= ~1;
      ReleaseCapture();
      return 0;
    case WM_RBUTTONDOWN:
      g_mouse_btns |= 2;
      return 0;
    case WM_RBUTTONUP:
      g_mouse_btns &= ~2;
      return 0;
    case WM_KEYDOWN: {
      g_last_key = (int)wp;
      int next = (g_key_tail + 1) % 64;
      if (next != g_key_head) {
        g_key_queue[g_key_tail] = (int)wp;
        g_key_tail = next;
      }
      return 0;
    }
    case WM_CHAR: {
      int next = (g_key_tail + 1) % 64;
      if (next != g_key_head) {
        g_key_queue[g_key_tail] = (int)wp;
        g_key_tail = next;
      }
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (g_fb_memdc && g_fb_w > 0 && g_fb_h > 0) {
        BitBlt(hdc, 0, 0, g_fb_w, g_fb_h, g_fb_memdc, 0, 0, SRCCOPY);
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_CLOSE:
      g_fb_closed = 1;
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      g_fb_closed = 1;
      g_fb_hwnd = NULL;
      return 0;
  }
  return DefWindowProcA(hwnd, msg, wp, lp);
}

TZ_CPU_EXPORT int64_t fb_init_ex(const char* title, int64_t W, int64_t H) {
  g_fb_w = (int)W;
  g_fb_h = (int)H;
  g_fb_closed = 0;

  HINSTANCE hi = GetModuleHandle(NULL);
  WNDCLASSEXA wc = {0};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  wc.lpfnWndProc = TzFbWndProc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = NULL;
  wc.lpszClassName = "TezzNative_FB_Host";
  RegisterClassExA(&wc);

  RECT wr = {0, 0, g_fb_w, g_fb_h};
  AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, FALSE);
  int win_w = wr.right - wr.left;
  int win_h = wr.bottom - wr.top;
  int sx = (GetSystemMetrics(SM_CXSCREEN) - win_w) / 2;
  int sy = (GetSystemMetrics(SM_CYSCREEN) - win_h) / 2;

  g_fb_hwnd = CreateWindowExA(
      0, "TezzNative_FB_Host", (title && *title) ? title : "TezzNative Window",
      WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
      sx, sy, win_w, win_h, NULL, NULL, hi, NULL);
  if (!g_fb_hwnd) return -1;

  g_fb_hdc = GetDC(g_fb_hwnd);
  g_fb_memdc = CreateCompatibleDC(g_fb_hdc);

  BITMAPINFO bmi = {0};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = g_fb_w;
  bmi.bmiHeader.biHeight = -g_fb_h; // top-down DIB
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  g_fb_hbmp = CreateDIBSection(g_fb_hdc, &bmi, DIB_RGB_COLORS, (void**)&g_fb_pixels, NULL, 0);
  if (!g_fb_hbmp || !g_fb_pixels) return -1;

  SelectObject(g_fb_memdc, g_fb_hbmp);
  ShowWindow(g_fb_hwnd, SW_SHOW);
  UpdateWindow(g_fb_hwnd);
  return 0;
}

TZ_CPU_EXPORT int64_t fb_present(void) {
  if (!g_fb_hwnd || g_fb_closed) return -1;
  MSG msg;
  while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) { g_fb_closed = 1; return -1; }
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }
  if (g_fb_hdc && g_fb_memdc) {
    BitBlt(g_fb_hdc, 0, 0, g_fb_w, g_fb_h, g_fb_memdc, 0, 0, SRCCOPY);
  }
  return g_fb_closed ? -1 : 0;
}

TZ_CPU_EXPORT char* fb_addr(void) { return (char*)g_fb_pixels; }
TZ_CPU_EXPORT int64_t fb_width(void) { return g_fb_w; }
TZ_CPU_EXPORT int64_t fb_height(void) { return g_fb_h; }
TZ_CPU_EXPORT int64_t fb_pitch(void) { return g_fb_w * 4; }
TZ_CPU_EXPORT int64_t fb_bpp(void) { return 32; }

TZ_CPU_EXPORT int64_t screen_width(void) { return (int64_t)GetSystemMetrics(SM_CXSCREEN); }
TZ_CPU_EXPORT int64_t screen_height(void) { return (int64_t)GetSystemMetrics(SM_CYSCREEN); }

TZ_CPU_EXPORT int64_t fb_fill(int64_t color) {
  if (!g_fb_pixels) return -1;
  uint32_t c = (uint32_t)color;
  int n = g_fb_w * g_fb_h;
  for (int i = 0; i < n; i++) g_fb_pixels[i] = c;
  return 0;
}

TZ_CPU_EXPORT int64_t fb_put_pixel(int64_t x, int64_t y, int64_t color) {
  if (!g_fb_pixels || x < 0 || x >= g_fb_w || y < 0 || y >= g_fb_h) return -1;
  g_fb_pixels[y * g_fb_w + x] = (uint32_t)color;
  return 0;
}

TZ_CPU_EXPORT int64_t fb_get_pixel(int64_t x, int64_t y) {
  if (!g_fb_pixels || x < 0 || x >= g_fb_w || y < 0 || y >= g_fb_h) return 0;
  return (int64_t)g_fb_pixels[y * g_fb_w + x];
}

TZ_CPU_EXPORT int64_t fb_fill_rect(int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
  if (!g_fb_pixels) return -1;
  int x0 = (int)x, y0 = (int)y, rw = (int)w, rh = (int)h;
  if (x0 < 0) { rw += x0; x0 = 0; }
  if (y0 < 0) { rh += y0; y0 = 0; }
  if (x0 + rw > g_fb_w) rw = g_fb_w - x0;
  if (y0 + rh > g_fb_h) rh = g_fb_h - y0;
  if (rw <= 0 || rh <= 0) return 0;
  uint32_t c = (uint32_t)color;
  for (int j = 0; j < rh; j++) {
    uint32_t* row = g_fb_pixels + (y0 + j) * g_fb_w + x0;
    for (int i = 0; i < rw; i++) row[i] = c;
  }
  return 0;
}

TZ_CPU_EXPORT int64_t fb_fill_rounded(int64_t x, int64_t y, int64_t w, int64_t h, int64_t color, int64_t radius) {
  if (!g_fb_pixels) return -1;
  int x0 = (int)x, y0 = (int)y, rw = (int)w, rh = (int)h, r = (int)radius;
  if (r <= 0) return fb_fill_rect(x, y, w, h, color);
  if (r > rw / 2) r = rw / 2;
  if (r > rh / 2) r = rh / 2;
  uint32_t c = (uint32_t)color;
  int r2 = r * r;
  for (int j = 0; j < rh; j++) {
    int py = y0 + j;
    if (py < 0 || py >= g_fb_h) continue;
    for (int i = 0; i < rw; i++) {
      int px = x0 + i;
      if (px < 0 || px >= g_fb_w) continue;
      int in_corner = 0;
      int dx = 0, dy = 0;
      if (i < r && j < r) { dx = r - i; dy = r - j; in_corner = 1; }
      else if (i >= rw - r && j < r) { dx = i - (rw - r - 1); dy = r - j; in_corner = 1; }
      else if (i < r && j >= rh - r) { dx = r - i; dy = j - (rh - r - 1); in_corner = 1; }
      else if (i >= rw - r && j >= rh - r) { dx = i - (rw - r - 1); dy = j - (rh - r - 1); in_corner = 1; }
      if (in_corner && (dx * dx + dy * dy > r2)) continue;
      g_fb_pixels[py * g_fb_w + px] = c;
    }
  }
  return 0;
}

TZ_CPU_EXPORT int64_t fb_blit(int64_t sx, int64_t sy, int64_t w, int64_t h, int64_t dx, int64_t dy) {
  if (!g_fb_pixels) return -1;
  return 0;
}

TZ_CPU_EXPORT int64_t fb_text_ex(int64_t x, int64_t y, const char* s, int64_t color, int64_t scale, int64_t bold) {
  if (!g_fb_memdc || !s) return -1;
  int font_h = (int)scale * 14;
  if (font_h < 12) font_h = 14;
  HFONT font = CreateFontA(-font_h, 0, 0, 0, (bold ? FW_BOLD : FW_NORMAL), 0, 0, 0,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, 0, "Segoe UI");
  HFONT old_font = (HFONT)SelectObject(g_fb_memdc, font);
  SetBkMode(g_fb_memdc, TRANSPARENT);
  COLORREF cr = RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
  SetTextColor(g_fb_memdc, cr);
  TextOutA(g_fb_memdc, (int)x, (int)y, s, (int)strlen(s));
  SelectObject(g_fb_memdc, old_font);
  DeleteObject(font);
  return 0;
}

TZ_CPU_EXPORT int64_t fb_text(int64_t x, int64_t y, const char* s, int64_t color) {
  return fb_text_ex(x, y, s, color, 1, 0);
}

TZ_CPU_EXPORT int64_t fb_soft_cursor(int64_t x, int64_t y, int64_t color) {
  return 0;
}

TZ_CPU_EXPORT int64_t kbd_has_event(void) {
  return (g_key_head != g_key_tail) ? 1 : 0;
}

TZ_CPU_EXPORT int64_t kbd_read_char(void) {
  if (g_key_head == g_key_tail) return 0;
  int k = g_key_queue[g_key_head];
  g_key_head = (g_key_head + 1) % 64;
  return k;
}

TZ_CPU_EXPORT int64_t kbd_read_scancode(void) {
  return (int64_t)g_last_key;
}

TZ_CPU_EXPORT int64_t mouse_has_packet(void) { return 1; }
TZ_CPU_EXPORT int64_t mouse_read_packet(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_dx(void) { return (int64_t)(g_mouse_x - g_mouse_prev_x); }
TZ_CPU_EXPORT int64_t mouse_dy(void) { return (int64_t)(g_mouse_y - g_mouse_prev_y); }
TZ_CPU_EXPORT int64_t mouse_buttons(void) { return (int64_t)g_mouse_btns; }
TZ_CPU_EXPORT int64_t mouse_pos_x(void) { return (int64_t)g_mouse_x; }
TZ_CPU_EXPORT int64_t mouse_pos_y(void) { return (int64_t)g_mouse_y; }

TZ_CPU_EXPORT int64_t sleep_ms(int64_t ms) {
  Sleep((DWORD)ms);
  return 0;
}

/* Audio Subsystem */
TZ_CPU_EXPORT int64_t tts_play_pcm(int64_t* pcm, int64_t len, int64_t rate) {
  if (!pcm || len <= 0 || rate <= 0) return -1;
  char temp_path[MAX_PATH];
  GetTempPathA(MAX_PATH, temp_path);
  strcat(temp_path, "tezz_tts_temp.wav");

  FILE* f = fopen(temp_path, "wb");
  if (!f) return -1;

  long long data_bytes = len * 2;
  long long total_size = data_bytes + 36;
  unsigned char hdr[44] = {
    'R', 'I', 'F', 'F',
    (unsigned char)(total_size & 0xFF), (unsigned char)((total_size >> 8) & 0xFF),
    (unsigned char)((total_size >> 16) & 0xFF), (unsigned char)((total_size >> 24) & 0xFF),
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ',
    16, 0, 0, 0,
    1, 0, 1, 0,
    (unsigned char)(rate & 0xFF), (unsigned char)((rate >> 8) & 0xFF),
    (unsigned char)((rate >> 16) & 0xFF), (unsigned char)((rate >> 24) & 0xFF),
    (unsigned char)((rate * 2) & 0xFF), (unsigned char)(((rate * 2) >> 8) & 0xFF),
    (unsigned char)(((rate * 2) >> 16) & 0xFF), (unsigned char)(((rate * 2) >> 24) & 0xFF),
    2, 0, 16, 0,
    'd', 'a', 't', 'a',
    (unsigned char)(data_bytes & 0xFF), (unsigned char)((data_bytes >> 8) & 0xFF),
    (unsigned char)((data_bytes >> 16) & 0xFF), (unsigned char)((data_bytes >> 24) & 0xFF)
  };

  fwrite(hdr, 1, 44, f);
  for (int64_t i = 0; i < len; i++) {
    int64_t v = pcm[i];
    if (v > 32767) v = 32767;
    if (v < -32767) v = -32767;
    short s = (short)v;
    fwrite(&s, 2, 1, f);
  }
  fclose(f);

  PlaySoundA(temp_path, NULL, SND_FILENAME | SND_SYNC);
  DeleteFileA(temp_path);
  return 0;
}

TZ_CPU_EXPORT int64_t stt_capture_mic(int64_t* pcm, int64_t n_samples, int64_t rate) {
  if (!pcm || n_samples <= 0 || rate <= 0) return -1;
  for (int64_t i = 0; i < n_samples; i++) pcm[i] = 0;
  return 0;
}
#else
TZ_CPU_EXPORT int64_t fb_init_ex(const char* title, int64_t W, int64_t H) { return 0; }
TZ_CPU_EXPORT int64_t fb_present(void) { return 0; }
TZ_CPU_EXPORT char* fb_addr(void) { return NULL; }
TZ_CPU_EXPORT int64_t fb_width(void) { return 0; }
TZ_CPU_EXPORT int64_t fb_height(void) { return 0; }
TZ_CPU_EXPORT int64_t fb_pitch(void) { return 0; }
TZ_CPU_EXPORT int64_t fb_bpp(void) { return 32; }
TZ_CPU_EXPORT int64_t screen_width(void) { return 1920; }
TZ_CPU_EXPORT int64_t screen_height(void) { return 1080; }
TZ_CPU_EXPORT int64_t fb_fill(int64_t color) { return 0; }
TZ_CPU_EXPORT int64_t fb_put_pixel(int64_t x, int64_t y, int64_t color) { return 0; }
TZ_CPU_EXPORT int64_t fb_get_pixel(int64_t x, int64_t y) { return 0; }
TZ_CPU_EXPORT int64_t fb_fill_rect(int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) { return 0; }
TZ_CPU_EXPORT int64_t fb_fill_rounded(int64_t x, int64_t y, int64_t w, int64_t h, int64_t color, int64_t radius) { return 0; }
TZ_CPU_EXPORT int64_t fb_blit(int64_t sx, int64_t sy, int64_t w, int64_t h, int64_t dx, int64_t dy) { return 0; }
TZ_CPU_EXPORT int64_t fb_text(int64_t x, int64_t y, const char* s, int64_t color) { return 0; }
TZ_CPU_EXPORT int64_t fb_text_ex(int64_t x, int64_t y, const char* s, int64_t color, int64_t scale, int64_t bold) { return 0; }
TZ_CPU_EXPORT int64_t fb_soft_cursor(int64_t x, int64_t y, int64_t color) { return 0; }
TZ_CPU_EXPORT int64_t kbd_has_event(void) { return 0; }
TZ_CPU_EXPORT int64_t kbd_read_char(void) { return 0; }
TZ_CPU_EXPORT int64_t kbd_read_scancode(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_has_packet(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_read_packet(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_dx(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_dy(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_buttons(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_pos_x(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_pos_y(void) { return 0; }
TZ_CPU_EXPORT int64_t sleep_ms(int64_t ms) { usleep(ms * 1000); return 0; }
TZ_CPU_EXPORT int64_t tts_play_pcm(int64_t* pcm, int64_t len, int64_t rate) { return 0; }
TZ_CPU_EXPORT int64_t stt_capture_mic(int64_t* pcm, int64_t n_samples, int64_t rate) { return 0; }
#endif

#ifdef __cplusplus
} // extern "C"
#endif