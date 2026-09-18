// tzgpu_kernels.cu -- TezzNative GPU Kernel Library v2.0 (Production)
// Linux:   nvcc -O3 -arch=sm_86 -shared -Xcompiler -fPIC tzgpu_kernels.cu -o libtzgpu.so -lcublas -lcurand
// Windows: nvcc -O3 -arch=sm_86 -shared -Xcompiler "/LD /MD" tzgpu_kernels.cu -o tzgpu.dll -lcublas -lcurand
// RTX 3060 = sm_86 (Ampere) | f32 primary for 4x perf, f64 for matmul compatibility

#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <curand_kernel.h>
#include <cuda_fp16.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#ifdef _WIN32
#  define TZ_EXPORT __declspec(dllexport)
#else
#  define TZ_EXPORT
#endif

// ─── Thread geometry ───────────────────────────────────────────────────────────
#define BLOCK_1D   256
#define TILE_DIM    16

// ─── Error check ──────────────────────────────────────────────────────────────
#define CUDA_CHECK(call) do { \
  cudaError_t _e = (call); \
  if(_e != cudaSuccess) fprintf(stderr,"CUDA %s:%d: %s\n",__FILE__,__LINE__,cudaGetErrorString(_e)); \
} while(0)

// ─── Global handles ───────────────────────────────────────────────────────────
static cublasHandle_t _blas = NULL;
static int _blas_ready = 0;
static cublasHandle_t tz_blas(){
  if(!_blas_ready){ cublasCreate(&_blas); _blas_ready=1; }
  return _blas;
}

// =============================================================================
//  ELEMENTWISE KERNELS (f32 primary)
// =============================================================================

__global__ void k_add_f32(const float* a, const float* b, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]+b[i];
}
__global__ void k_sub_f32(const float* a, const float* b, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]-b[i];
}
__global__ void k_mul_f32(const float* a, const float* b, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]*b[i];
}
__global__ void k_scale_f32(const float* a, float* c, int n, float s){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]*s;
}
__global__ void k_fill_f32(float* a, int n, float val){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) a[i]=val;
}
__global__ void k_copy_f32(const float* src, float* dst, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) dst[i]=src[i];
}
__global__ void k_axpy_f32(float* y, const float* x, float alpha, int n){
  // y = alpha*x + y
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) y[i]+=alpha*x[i];
}
__global__ void k_square_f32(const float* a, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]*a[i];
}
__global__ void k_sqrt_f32(const float* a, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=sqrtf(a[i]+1e-8f);
}
__global__ void k_clamp_f32(float* a, int n, float lo, float hi){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n){ if(a[i]<lo) a[i]=lo; if(a[i]>hi) a[i]=hi; }
}

// =============================================================================
//  ACTIVATION KERNELS (f32)
// =============================================================================

__global__ void k_relu_f32(const float* a, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]>0.f?a[i]:0.f;
}
__global__ void k_relu_bwd_f32(const float* grad_out, const float* x, float* grad_in, int n){
  // grad_in = grad_out * (x > 0)
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) grad_in[i]=x[i]>0.f?grad_out[i]:0.f;
}
__global__ void k_leaky_relu_f32(const float* a, float* c, int n, float alpha){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]>0.f?a[i]:alpha*a[i];
}
__global__ void k_sigmoid_f32(const float* a, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=1.f/(1.f+expf(-a[i]));
}
__global__ void k_sigmoid_bwd_f32(const float* grad_out, const float* sig_out, float* grad_in, int n){
  // grad_in = grad_out * sig*(1-sig)
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n){ float s=sig_out[i]; grad_in[i]=grad_out[i]*s*(1.f-s); }
}
__global__ void k_tanh_f32(const float* a, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=tanhf(a[i]);
}
__global__ void k_tanh_bwd_f32(const float* grad_out, const float* tanh_out, float* grad_in, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n){ float t=tanh_out[i]; grad_in[i]=grad_out[i]*(1.f-t*t); }
}
// GELU: x * sigmoid(1.702 * x)   [fast approximation]
__global__ void k_gelu_f32(const float* a, float* c, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]*(1.f/(1.f+expf(-1.702f*a[i])));
}
__global__ void k_gelu_bwd_f32(const float* grad_out, const float* x, float* grad_in, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n){
    float sig = 1.f/(1.f+expf(-1.702f*x[i]));
    grad_in[i] = grad_out[i] * (sig + x[i]*1.702f*sig*(1.f-sig));
  }
}

// =============================================================================
//  SOFTMAX (numerically stable, row-wise)
// =============================================================================

__global__ void k_softmax_f32(float* x, int rows, int cols){
  int row = blockIdx.x*blockDim.x+threadIdx.x;
  if(row>=rows) return;
  float* r = x + row*cols;
  float mx = r[0];
  for(int j=1;j<cols;j++) if(r[j]>mx) mx=r[j];
  float s = 0.f;
  for(int j=0;j<cols;j++){ r[j]=expf(r[j]-mx); s+=r[j]; }
  for(int j=0;j<cols;j++) r[j]/=s;
}

// =============================================================================
//  BIAS ADD (add bias vector b[out_dim] to every row of x[batch×out_dim])
// =============================================================================

__global__ void k_add_bias_f32(float* x, const float* b, int batch, int dim){
  int idx = blockIdx.x*blockDim.x+threadIdx.x;
  if(idx >= batch*dim) return;
  x[idx] += b[idx % dim];
}

// =============================================================================
//  LAYER NORM (row-wise, in-place, f32)
// =============================================================================

__global__ void k_layernorm_f32(float* x, const float* gamma, const float* beta,
                                  int rows, int cols, float eps){
  int row = blockIdx.x*blockDim.x+threadIdx.x;
  if(row>=rows) return;
  float* r = x + row*cols;
  float mu=0.f;
  for(int j=0;j<cols;j++) mu+=r[j];
  mu/=cols;
  float va=0.f;
  for(int j=0;j<cols;j++){ float d=r[j]-mu; va+=d*d; }
  va/=cols;
  float inv_std=rsqrtf(va+eps);
  for(int j=0;j<cols;j++){
    float norm=(r[j]-mu)*inv_std;
    r[j] = gamma ? gamma[j]*norm+beta[j] : norm;
  }
}

// LayerNorm backward (simplified — no gamma/beta grad for now, just dx)
__global__ void k_layernorm_bwd_f32(float* dx, const float* dy, const float* x,
                                      const float* gamma, int rows, int cols, float eps){
  int row = blockIdx.x*blockDim.x+threadIdx.x;
  if(row>=rows) return;
  const float* xr = x + row*cols;
  const float* dyr = dy + row*cols;
  float* dxr = dx + row*cols;
  float mu=0.f, va=0.f;
  for(int j=0;j<cols;j++) mu+=xr[j];
  mu/=cols;
  for(int j=0;j<cols;j++){ float d=xr[j]-mu; va+=d*d; }
  va/=cols;
  float inv_std=rsqrtf(va+eps);
  float N=(float)cols;
  float s1=0.f, s2=0.f;
  for(int j=0;j<cols;j++){
    float g=gamma?gamma[j]:1.f;
    s1+=dyr[j]*g;
    s2+=dyr[j]*g*(xr[j]-mu);
  }
  for(int j=0;j<cols;j++){
    float g=gamma?gamma[j]:1.f;
    float xhat=(xr[j]-mu)*inv_std;
    dxr[j]=inv_std/N*(N*dyr[j]*g - s1 - xhat*s2);
  }
}

// =============================================================================
//  BATCH NORM (channel-wise, inference mode: uses running mean/var)
// =============================================================================

__global__ void k_batchnorm_fwd_f32(float* x, const float* mean, const float* var,
                                      const float* gamma, const float* beta,
                                      int N, int C, int HW, float eps){
  // x layout: [N, C, HW]
  int idx = blockIdx.x*blockDim.x+threadIdx.x;
  if(idx >= N*C*HW) return;
  int c = (idx / HW) % C;
  float inv_std = rsqrtf(var[c]+eps);
  x[idx] = gamma[c]*((x[idx]-mean[c])*inv_std)+beta[c];
}

// =============================================================================
//  DROPOUT (training only, uses cuRAND per-element)
// =============================================================================

__global__ void k_dropout_f32(float* x, float* mask, int n, float rate, unsigned long long seed){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i>=n) return;
  curandState st;
  curand_init(seed, i, 0, &st);
  float r = curand_uniform(&st);
  float keep = r > rate ? 1.f/(1.f-rate) : 0.f;
  mask[i] = keep;
  x[i] *= keep;
}

// =============================================================================
//  EMBEDDING LOOKUP
// =============================================================================

__global__ void k_embedding_f32(const int* indices, const float* table,
                                  float* out, int n, int embed_dim){
  // out[i*embed_dim .. +embed_dim] = table[indices[i]*embed_dim ..]
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i>=n) return;
  int idx = indices[i];
  const float* src = table + idx*embed_dim;
  float* dst = out + i*embed_dim;
  for(int j=0;j<embed_dim;j++) dst[j]=src[j];
}

// Embedding backward: scatter-add grad into weight table
__global__ void k_embedding_bwd_f32(const int* indices, const float* grad_out,
                                      float* grad_table, int n, int embed_dim){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i>=n) return;
  int idx = indices[i];
  const float* gsrc = grad_out + i*embed_dim;
  float* gdst = grad_table + idx*embed_dim;
  for(int j=0;j<embed_dim;j++) atomicAdd(&gdst[j], gsrc[j]);
}

// =============================================================================
//  ROTARY POSITION EMBEDDING (RoPE) — for transformer attention
// =============================================================================

__global__ void k_rope_f32(float* x, int seq, int heads, int dim_head){
  // x: [seq, heads, dim_head]
  int tid = blockIdx.x*blockDim.x+threadIdx.x;
  int total = seq*heads*(dim_head/2);
  if(tid>=total) return;
  int half = dim_head/2;
  int h_idx = tid % half;
  int head  = (tid / half) % heads;
  int pos   = tid / (half*heads);
  float theta = (float)pos / powf(10000.f, 2.f*(float)h_idx/(float)dim_head);
  float cosT = cosf(theta), sinT = sinf(theta);
  int base = (pos*heads+head)*dim_head + h_idx;
  float x0 = x[base], x1 = x[base+half];
  x[base]      = x0*cosT - x1*sinT;
  x[base+half] = x0*sinT + x1*cosT;
}

// =============================================================================
//  CAUSAL ATTENTION MASK (set upper-triangle to -1e9)
// =============================================================================

__global__ void k_causal_mask_f32(float* scores, int batch_heads, int seq){
  int idx = blockIdx.x*blockDim.x+threadIdx.x;
  if(idx >= batch_heads*seq*seq) return;
  int col = idx % seq;
  int row = (idx / seq) % seq;
  if(col > row) scores[idx] = -1e9f;
}

// =============================================================================
//  CROSS-ENTROPY LOSS (stable: logsumexp trick)
// =============================================================================

// Returns per-sample loss; sum externally
__global__ void k_cross_entropy_f32(const float* logits, const int* labels,
                                      float* loss, int batch, int classes){
  int b = blockIdx.x*blockDim.x+threadIdx.x;
  if(b>=batch) return;
  const float* l = logits + b*classes;
  float mx=l[0];
  for(int c=1;c<classes;c++) if(l[c]>mx) mx=l[c];
  float s=0.f;
  for(int c=0;c<classes;c++) s+=expf(l[c]-mx);
  loss[b] = -(l[labels[b]]-mx) + logf(s);
}

// Cross-entropy gradient: softmax(logits) - one_hot(label)
__global__ void k_cross_entropy_bwd_f32(const float* logits, const int* labels,
                                          float* grad, int batch, int classes){
  int idx = blockIdx.x*blockDim.x+threadIdx.x;
  if(idx >= batch*classes) return;
  int b = idx / classes;
  int c = idx % classes;
  const float* l = logits + b*classes;
  float mx=l[0];
  for(int cc=1;cc<classes;cc++) if(l[cc]>mx) mx=l[cc];
  float s=0.f;
  for(int cc=0;cc<classes;cc++) s+=expf(l[cc]-mx);
  float sm = expf(l[c]-mx)/s;
  grad[idx] = (sm - (c==labels[b]?1.f:0.f)) / (float)batch;
}

// =============================================================================
//  MSE LOSS
// =============================================================================

__global__ void k_mse_loss_f32(const float* pred, const float* target,
                                 float* loss, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n){ float d=pred[i]-target[i]; loss[i]=d*d; }
}
__global__ void k_mse_bwd_f32(const float* pred, const float* target,
                                float* grad, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) grad[i] = 2.f*(pred[i]-target[i])/(float)n;
}

// =============================================================================
//  ADAM OPTIMIZER (fused, in-place)
// =============================================================================

__global__ void k_adam_f32(float* p, const float* g, float* m, float* v,
                             float lr, float b1, float b2, float eps,
                             float b1t, float b2t, int n){
  // b1t = b1^t, b2t = b2^t (precomputed bias correction)
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i>=n) return;
  float gi = g[i];
  m[i] = b1*m[i] + (1.f-b1)*gi;
  v[i] = b2*v[i] + (1.f-b2)*gi*gi;
  float mhat = m[i]/(1.f-b1t);
  float vhat = v[i]/(1.f-b2t);
  p[i] -= lr * mhat / (sqrtf(vhat)+eps);
}

// =============================================================================
//  SGD WITH MOMENTUM
// =============================================================================

__global__ void k_sgd_f32(float* p, const float* g, float* vel,
                            float lr, float momentum, float wd, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i>=n) return;
  float grad = g[i] + wd*p[i];
  vel[i] = momentum*vel[i] + grad;
  p[i] -= lr * vel[i];
}

// =============================================================================
//  GRADIENT CLIPPING (clip by global L2 norm)
// =============================================================================

__global__ void k_grad_clip_f32(float* g, int n, float max_norm, float actual_norm){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i>=n) return;
  if(actual_norm > max_norm) g[i] *= max_norm/actual_norm;
}

// Reduce sum (for norm computation) — one block only, small n
__global__ void k_reduce_sum_sq_f32(const float* x, float* out, int n){
  __shared__ float sdata[BLOCK_1D];
  int tid = threadIdx.x;
  int i = blockIdx.x*blockDim.x+tid;
  sdata[tid] = (i<n) ? x[i]*x[i] : 0.f;
  __syncthreads();
  for(int s=blockDim.x/2; s>0; s>>=1){
    if(tid<s) sdata[tid]+=sdata[tid+s];
    __syncthreads();
  }
  if(tid==0) atomicAdd(out, sdata[0]);
}

// =============================================================================
//  DTYPE CONVERSION
// =============================================================================

__global__ void k_f32_to_f64(const float* src, double* dst, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) dst[i]=(double)src[i];
}
__global__ void k_f64_to_f32(const double* src, float* dst, int n){
  int i = blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) dst[i]=(float)src[i];
}

// =============================================================================
//  TILED MATMUL (f64 fallback, compatible with v1)
// =============================================================================

__global__ void k_matmul_f64(const double* a, const double* b, double* c,
                               int M, int K, int N){
  __shared__ double As[TILE_DIM][TILE_DIM];
  __shared__ double Bs[TILE_DIM][TILE_DIM];
  int row=blockIdx.y*TILE_DIM+threadIdx.y;
  int col=blockIdx.x*TILE_DIM+threadIdx.x;
  double acc=0.0;
  for(int t=0; t<(K+TILE_DIM-1)/TILE_DIM; t++){
    As[threadIdx.y][threadIdx.x] = (row<M && t*TILE_DIM+threadIdx.x<K) ?
      a[row*K + t*TILE_DIM+threadIdx.x] : 0.0;
    Bs[threadIdx.y][threadIdx.x] = (col<N && t*TILE_DIM+threadIdx.y<K) ?
      b[(t*TILE_DIM+threadIdx.y)*N + col] : 0.0;
    __syncthreads();
    for(int k=0;k<TILE_DIM;k++) acc+=As[threadIdx.y][k]*Bs[k][threadIdx.x];
    __syncthreads();
  }
  if(row<M && col<N) c[row*N+col]=acc;
}

// f64 elementwise (kept for v1 compatibility)
__global__ void k_add_f64(const double* a, const double* b, double* c, int n){
  int i=blockIdx.x*blockDim.x+threadIdx.x; if(i<n) c[i]=a[i]+b[i];
}
__global__ void k_mul_f64(const double* a, const double* b, double* c, int n){
  int i=blockIdx.x*blockDim.x+threadIdx.x; if(i<n) c[i]=a[i]*b[i];
}
__global__ void k_relu_f64(const double* a, double* c, int n){
  int i=blockIdx.x*blockDim.x+threadIdx.x; if(i<n) c[i]=a[i]>0.0?a[i]:0.0;
}
__global__ void k_scale_f64(const double* a, double* c, int n, double s){
  int i=blockIdx.x*blockDim.x+threadIdx.x; if(i<n) c[i]=a[i]*s;
}
__global__ void k_gelu_f64(const double* a, double* c, int n){
  int i=blockIdx.x*blockDim.x+threadIdx.x;
  if(i<n) c[i]=a[i]*(1.0/(1.0+exp(-1.702*a[i])));
}
__global__ void k_sigmoid_f64(const double* a, double* c, int n){
  int i=blockIdx.x*blockDim.x+threadIdx.x; if(i<n) c[i]=1.0/(1.0+exp(-a[i]));
}
__global__ void k_softmax_f64(double* x, int rows, int cols){
  int row=blockIdx.x*blockDim.x+threadIdx.x; if(row>=rows) return;
  double* r=x+row*cols; double mx=r[0];
  for(int j=1;j<cols;j++) if(r[j]>mx) mx=r[j];
  double s=0.0;
  for(int j=0;j<cols;j++){ r[j]=exp(r[j]-mx); s+=r[j]; }
  for(int j=0;j<cols;j++) r[j]/=s;
}
__global__ void k_layer_norm_f64(double* x, int rows, int cols, double eps){
  int row=blockIdx.x*blockDim.x+threadIdx.x; if(row>=rows) return;
  double* rp=x+row*cols;
  double mu=0.0; for(int j=0;j<cols;j++) mu+=rp[j]; mu/=cols;
  double va=0.0; for(int j=0;j<cols;j++){ double d=rp[j]-mu; va+=d*d; } va/=cols;
  double inv_std=1.0/sqrt(va+eps);
  for(int j=0;j<cols;j++) rp[j]=(rp[j]-mu)*inv_std;
}

// =============================================================================
//  EXPORTED C API — ALL FUNCTIONS VISIBLE TO tzgpu.dll IMPORT TABLE
// =============================================================================
extern "C" {

// ── Memory ────────────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_cuda_malloc(int64_t nbytes){
  void* p=NULL; if(cudaMalloc(&p,(size_t)nbytes)!=cudaSuccess) return 0; return (int64_t)p;
}
TZ_EXPORT int64_t tz_cuda_free(int64_t ptr){
  if(ptr) cudaFree((void*)ptr); return 0;
}
TZ_EXPORT int64_t tz_cuda_memcpy(int64_t dst, int64_t src, int64_t n, int64_t kind){
  return (cudaMemcpy((void*)dst,(void*)src,(size_t)n,(cudaMemcpyKind)(int)kind)==cudaSuccess)?0:-1;
}
TZ_EXPORT int64_t tz_cuda_sync(){ cudaDeviceSynchronize(); return 0; }
TZ_EXPORT int64_t tz_cuda_device_count(){ int n=0; cudaGetDeviceCount(&n); return n; }
TZ_EXPORT int64_t tz_cuda_set_device(int64_t idx){ return cudaSetDevice((int)idx); }
TZ_EXPORT int64_t tz_cuda_mem_free(){ size_t f,t; cudaMemGetInfo(&f,&t); return (int64_t)f; }
TZ_EXPORT int64_t tz_cuda_mem_total(){ size_t f,t; cudaMemGetInfo(&f,&t); return (int64_t)t; }

// ── f32 Elementwise ───────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_add_f32(int64_t a, int64_t b, int64_t c, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_add_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)b,(float*)c,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_sub_f32(int64_t a, int64_t b, int64_t c, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_sub_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)b,(float*)c,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_mul_f32(int64_t a, int64_t b, int64_t c, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_mul_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)b,(float*)c,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_scale_f32(int64_t a, int64_t c, int64_t n, double s){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_scale_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)c,(int)n,(float)s); return 0;
}
TZ_EXPORT int64_t tz_gpu_fill_f32(int64_t a, int64_t n, double val){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_fill_f32<<<bl,BLOCK_1D>>>((float*)a,(int)n,(float)val); return 0;
}
TZ_EXPORT int64_t tz_gpu_copy_f32(int64_t src, int64_t dst, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_copy_f32<<<bl,BLOCK_1D>>>((float*)src,(float*)dst,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_axpy_f32(int64_t y, int64_t x, double alpha, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_axpy_f32<<<bl,BLOCK_1D>>>((float*)y,(float*)x,(float)alpha,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_clamp_f32(int64_t a, int64_t n, double lo, double hi){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_clamp_f32<<<bl,BLOCK_1D>>>((float*)a,(int)n,(float)lo,(float)hi); return 0;
}

// ── f32 Activations ───────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_relu_f32(int64_t a, int64_t c, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_relu_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)c,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_relu_bwd_f32(int64_t grad_out, int64_t x, int64_t grad_in, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_relu_bwd_f32<<<bl,BLOCK_1D>>>((float*)grad_out,(float*)x,(float*)grad_in,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_leaky_relu_f32(int64_t a, int64_t c, int64_t n, double alpha){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_leaky_relu_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)c,(int)n,(float)alpha); return 0;
}
TZ_EXPORT int64_t tz_gpu_sigmoid_f32(int64_t a, int64_t c, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_sigmoid_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)c,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_sigmoid_bwd_f32(int64_t grad_out, int64_t sig_out, int64_t grad_in, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_sigmoid_bwd_f32<<<bl,BLOCK_1D>>>((float*)grad_out,(float*)sig_out,(float*)grad_in,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_tanh_f32(int64_t a, int64_t c, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_tanh_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)c,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_tanh_bwd_f32(int64_t grad_out, int64_t tanh_out, int64_t grad_in, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_tanh_bwd_f32<<<bl,BLOCK_1D>>>((float*)grad_out,(float*)tanh_out,(float*)grad_in,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_gelu_f32(int64_t a, int64_t c, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_gelu_f32<<<bl,BLOCK_1D>>>((float*)a,(float*)c,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_gelu_bwd_f32(int64_t grad_out, int64_t x, int64_t grad_in, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_gelu_bwd_f32<<<bl,BLOCK_1D>>>((float*)grad_out,(float*)x,(float*)grad_in,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_softmax_f32(int64_t x, int64_t rows, int64_t cols){
  int bl=((int)rows+BLOCK_1D-1)/BLOCK_1D;
  k_softmax_f32<<<bl,BLOCK_1D>>>((float*)x,(int)rows,(int)cols); return 0;
}
TZ_EXPORT int64_t tz_gpu_add_bias_f32(int64_t x, int64_t b, int64_t batch, int64_t dim){
  int bl=((int)(batch*dim)+BLOCK_1D-1)/BLOCK_1D;
  k_add_bias_f32<<<bl,BLOCK_1D>>>((float*)x,(float*)b,(int)batch,(int)dim); return 0;
}

// ── Layer Norm ────────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_layernorm_f32(int64_t x, int64_t gamma, int64_t beta,
                                        int64_t rows, int64_t cols, double eps){
  int bl=((int)rows+BLOCK_1D-1)/BLOCK_1D;
  k_layernorm_f32<<<bl,BLOCK_1D>>>((float*)x,(float*)gamma,(float*)beta,
                                    (int)rows,(int)cols,(float)eps); return 0;
}
TZ_EXPORT int64_t tz_gpu_layernorm_bwd_f32(int64_t dx, int64_t dy, int64_t x,
                                             int64_t gamma, int64_t rows, int64_t cols, double eps){
  int bl=((int)rows+BLOCK_1D-1)/BLOCK_1D;
  k_layernorm_bwd_f32<<<bl,BLOCK_1D>>>((float*)dx,(float*)dy,(float*)x,
                                        (float*)gamma,(int)rows,(int)cols,(float)eps); return 0;
}

// ── Batch Norm (inference) ────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_batchnorm_f32(int64_t x, int64_t mean, int64_t var,
                                         int64_t gamma, int64_t beta,
                                         int64_t N, int64_t C, int64_t HW, double eps){
  int bl=((int)(N*C*HW)+BLOCK_1D-1)/BLOCK_1D;
  k_batchnorm_fwd_f32<<<bl,BLOCK_1D>>>((float*)x,(float*)mean,(float*)var,
                                        (float*)gamma,(float*)beta,
                                        (int)N,(int)C,(int)HW,(float)eps); return 0;
}

// ── Dropout ───────────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_dropout_f32(int64_t x, int64_t mask, int64_t n, double rate, int64_t seed){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_dropout_f32<<<bl,BLOCK_1D>>>((float*)x,(float*)mask,(int)n,(float)rate,
                                  (unsigned long long)seed); return 0;
}

// ── Embedding ─────────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_embedding_f32(int64_t indices, int64_t table, int64_t out,
                                         int64_t n, int64_t embed_dim){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_embedding_f32<<<bl,BLOCK_1D>>>((int*)indices,(float*)table,(float*)out,(int)n,(int)embed_dim);
  return 0;
}
TZ_EXPORT int64_t tz_gpu_embedding_bwd_f32(int64_t indices, int64_t grad_out,
                                             int64_t grad_table, int64_t n, int64_t embed_dim){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_embedding_bwd_f32<<<bl,BLOCK_1D>>>((int*)indices,(float*)grad_out,(float*)grad_table,(int)n,(int)embed_dim);
  return 0;
}

// ── Transformer ───────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_rope_f32(int64_t x, int64_t seq, int64_t heads, int64_t dim_head){
  int total=(int)(seq*heads*(dim_head/2));
  int bl=(total+BLOCK_1D-1)/BLOCK_1D;
  k_rope_f32<<<bl,BLOCK_1D>>>((float*)x,(int)seq,(int)heads,(int)dim_head); return 0;
}
TZ_EXPORT int64_t tz_gpu_causal_mask_f32(int64_t scores, int64_t batch_heads, int64_t seq){
  int bl=((int)(batch_heads*seq*seq)+BLOCK_1D-1)/BLOCK_1D;
  k_causal_mask_f32<<<bl,BLOCK_1D>>>((float*)scores,(int)batch_heads,(int)seq); return 0;
}

// ── Loss ──────────────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_cross_entropy_f32(int64_t logits, int64_t labels,
                                             int64_t loss_buf, int64_t batch, int64_t classes){
  int bl=((int)batch+BLOCK_1D-1)/BLOCK_1D;
  k_cross_entropy_f32<<<bl,BLOCK_1D>>>((float*)logits,(int*)labels,(float*)loss_buf,(int)batch,(int)classes);
  return 0;
}
TZ_EXPORT int64_t tz_gpu_cross_entropy_bwd_f32(int64_t logits, int64_t labels,
                                                 int64_t grad, int64_t batch, int64_t classes){
  int bl=((int)(batch*classes)+BLOCK_1D-1)/BLOCK_1D;
  k_cross_entropy_bwd_f32<<<bl,BLOCK_1D>>>((float*)logits,(int*)labels,(float*)grad,(int)batch,(int)classes);
  return 0;
}
TZ_EXPORT int64_t tz_gpu_mse_loss_f32(int64_t pred, int64_t target, int64_t loss_buf, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_mse_loss_f32<<<bl,BLOCK_1D>>>((float*)pred,(float*)target,(float*)loss_buf,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_mse_bwd_f32(int64_t pred, int64_t target, int64_t grad, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_mse_bwd_f32<<<bl,BLOCK_1D>>>((float*)pred,(float*)target,(float*)grad,(int)n); return 0;
}

// ── Optimizers ────────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_adam_f32(int64_t p, int64_t g, int64_t m, int64_t v,
                                    double lr, double b1, double b2, double eps,
                                    double b1t, double b2t, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_adam_f32<<<bl,BLOCK_1D>>>((float*)p,(float*)g,(float*)m,(float*)v,
                               (float)lr,(float)b1,(float)b2,(float)eps,
                               (float)b1t,(float)b2t,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_sgd_f32(int64_t p, int64_t g, int64_t vel,
                                   double lr, double momentum, double wd, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_sgd_f32<<<bl,BLOCK_1D>>>((float*)p,(float*)g,(float*)vel,(float)lr,(float)momentum,(float)wd,(int)n);
  return 0;
}

// ── Gradient utilities ────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_reduce_sum_sq_f32(int64_t x, int64_t out_buf, int64_t n){
  // out_buf must be pre-zeroed (device scalar)
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_reduce_sum_sq_f32<<<bl,BLOCK_1D>>>((float*)x,(float*)out_buf,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_grad_clip_f32(int64_t g, int64_t n, double max_norm, double actual_norm){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_grad_clip_f32<<<bl,BLOCK_1D>>>((float*)g,(int)n,(float)max_norm,(float)actual_norm); return 0;
}

// ── dtype conversion ──────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_f32_to_f64(int64_t src, int64_t dst, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_f32_to_f64<<<bl,BLOCK_1D>>>((float*)src,(double*)dst,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_f64_to_f32(int64_t src, int64_t dst, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_f64_to_f32<<<bl,BLOCK_1D>>>((double*)src,(float*)dst,(int)n); return 0;
}

// ── f32 GEMM via cuBLAS (primary path) ───────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_gemm_f32(int64_t a, int64_t b, int64_t c,
                                    int64_t M, int64_t K, int64_t N,
                                    double alpha, double beta){
  float fa=(float)alpha, fb=(float)beta;
  cublasSgemm(tz_blas(),CUBLAS_OP_N,CUBLAS_OP_N,
              (int)N,(int)M,(int)K,
              &fa,(float*)b,(int)N,(float*)a,(int)K,
              &fb,(float*)c,(int)N); return 0;
}

// ── f64 legacy ops (v1 compatibility) ────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_add(int64_t a, int64_t b, int64_t c, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_add_f64<<<bl,BLOCK_1D>>>((double*)a,(double*)b,(double*)c,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_mul(int64_t a, int64_t b, int64_t c, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_mul_f64<<<bl,BLOCK_1D>>>((double*)a,(double*)b,(double*)c,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_relu(int64_t a, int64_t c, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_relu_f64<<<bl,BLOCK_1D>>>((double*)a,(double*)c,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_scale(int64_t a, int64_t c, int64_t n, double s){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_scale_f64<<<bl,BLOCK_1D>>>((double*)a,(double*)c,(int)n,s); return 0;
}
TZ_EXPORT int64_t tz_gpu_gelu(int64_t a, int64_t c, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_gelu_f64<<<bl,BLOCK_1D>>>((double*)a,(double*)c,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_sigmoid(int64_t a, int64_t c, int64_t n){
  int bl=((int)n+BLOCK_1D-1)/BLOCK_1D;
  k_sigmoid_f64<<<bl,BLOCK_1D>>>((double*)a,(double*)c,(int)n); return 0;
}
TZ_EXPORT int64_t tz_gpu_gemm_f64(int64_t a, int64_t b, int64_t c,
                                    int64_t M, int64_t K, int64_t N,
                                    double alpha, double beta){
  cublasDgemm(tz_blas(),CUBLAS_OP_N,CUBLAS_OP_N,
              (int)N,(int)M,(int)K,
              &alpha,(double*)b,(int)N,(double*)a,(int)K,
              &beta,(double*)c,(int)N); return 0;
}
TZ_EXPORT int64_t tz_gpu_matmul_tiled(int64_t a, int64_t b, int64_t c,
                                        int64_t M, int64_t K, int64_t N){
  dim3 block(TILE_DIM,TILE_DIM);
  dim3 grid(((int)N+TILE_DIM-1)/TILE_DIM,((int)M+TILE_DIM-1)/TILE_DIM);
  k_matmul_f64<<<grid,block>>>((double*)a,(double*)b,(double*)c,(int)M,(int)K,(int)N); return 0;
}
TZ_EXPORT int64_t tz_gpu_softmax(int64_t x, int64_t rows, int64_t cols){
  int bl=((int)rows+BLOCK_1D-1)/BLOCK_1D;
  k_softmax_f64<<<bl,BLOCK_1D>>>((double*)x,(int)rows,(int)cols); return 0;
}
TZ_EXPORT int64_t tz_gpu_layer_norm(int64_t x, int64_t rows, int64_t cols, double eps){
  int bl=((int)rows+BLOCK_1D-1)/BLOCK_1D;
  k_layer_norm_f64<<<bl,BLOCK_1D>>>((double*)x,(int)rows,(int)cols,eps); return 0;
}

// ── Device info ───────────────────────────────────────────────────────────────
TZ_EXPORT int64_t tz_gpu_info(){
  int n=0; cudaGetDeviceCount(&n);
  for(int i=0;i<n;i++){
    cudaDeviceProp p; cudaGetDeviceProperties(&p,i);
    printf("[GPU %d] %s | SM %d.%d | %.1f GB VRAM | f32 TFlops=~%d\n",
      i, p.name, p.major, p.minor,
      (double)p.totalGlobalMem/(1024.0*1024.0*1024.0),
      p.multiProcessorCount*128*2*(p.clockRate/1000)/1000);
  }
  return n;
}

// =============================================================================
//  PHASE 5: BACKWARD PASS KERNELS (v2.1)
// =============================================================================

// ── Column-wise reduce (bias gradient: db = sum_rows(dY)) ─────────────────────
// out[j] += sum_i X[i, j]   for i in [0, rows), j in [0, cols)
__global__ void k_reduce_sum_cols_f32(const float* X, float* out, int rows, int cols){
  int j = blockIdx.x * blockDim.x + threadIdx.x;
  if(j >= cols) return;
  float s = 0.f;
  for(int i = 0; i < rows; i++) s += X[i * cols + j];
  atomicAdd(&out[j], s);
}

// ── Xavier / He init via cuRAND ───────────────────────────────────────────────
// Fills W[n] with N(0, std) where std = sqrt(2.0 / fan_in)  (He initialization)
__global__ void k_xavier_init_f32(float* W, int n, float std, unsigned long long seed){
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if(i >= n) return;
  curandStatePhilox4_32_10_t state;
  curand_init(seed, i, 0, &state);
  // Box-Muller via curand_normal
  W[i] = curand_normal(&state) * std;
}

// ── Exported backward pass API ────────────────────────────────────────────────

// C = A @ B^T   (needed for dW = dY^T @ X,  i.e. call with dY^T as A, X as B)
// cuBLAS Sgemm with CUBLAS_OP_T on B:
//   C[M x N] = A[M x K] @ B[N x K]^T
TZ_EXPORT int64_t tz_gpu_gemm_nt_f32(int64_t A, int64_t B, int64_t C,
                                       int64_t M, int64_t K, int64_t N,
                                       double alpha, double beta){
  float fa=(float)alpha, fb=(float)beta;
  // cuBLAS col-major: C^T = B @ A^T, so for row-major C=A@B^T:
  // call Sgemm(N, T, N=N, M=M, K=K, B, A, C)
  cublasSgemm(tz_blas(), CUBLAS_OP_T, CUBLAS_OP_N,
              (int)N, (int)M, (int)K,
              &fa, (float*)B, (int)K,
                   (float*)A, (int)K,
              &fb, (float*)C, (int)N);
  return 0;
}

// C = A^T @ B   (needed for dX = dY @ W, i.e. dX[batch x in] = dY[batch x out] @ W[out x in])
// C[M x N] = A[K x M]^T @ B[K x N]
TZ_EXPORT int64_t tz_gpu_gemm_tn_f32(int64_t A, int64_t B, int64_t C,
                                       int64_t M, int64_t K, int64_t N,
                                       double alpha, double beta){
  float fa=(float)alpha, fb=(float)beta;
  // For row-major C[M x N] = A^T[K x M] @ B[K x N]:
  // cuBLAS col-major: C^T[N x M] = B^T[N x K] @ A[K x M]
  // Sgemm(N, N, N=M, M=N, K=K, alpha, A[K x M] as col-major [M x K], B[K x N], beta, C)
  cublasSgemm(tz_blas(), CUBLAS_OP_N, CUBLAS_OP_T,
              (int)M, (int)N, (int)K,
              &fa, (float*)A, (int)M,
                   (float*)B, (int)N,
              &fb, (float*)C, (int)M);
  return 0;
}

// Column-wise sum: out[j] = sum_i X[i*cols + j]  (bias gradient accumulation)
// out must be pre-zeroed; this kernel atomicAdds into it.
TZ_EXPORT int64_t tz_gpu_reduce_sum_cols_f32(int64_t X, int64_t out, int64_t rows, int64_t cols){
  int bl = ((int)cols + BLOCK_1D - 1) / BLOCK_1D;
  k_reduce_sum_cols_f32<<<bl, BLOCK_1D>>>((float*)X, (float*)out, (int)rows, (int)cols);
  return 0;
}

// Xavier / He weight initialization
// std = sqrt(2.0 / fan_in) for ReLU networks (He init)
TZ_EXPORT int64_t tz_gpu_xavier_init_f32(int64_t W, int64_t n, int64_t fan_in, int64_t seed){
  float std = sqrtf(2.0f / (float)fan_in);
  int bl = ((int)n + BLOCK_1D - 1) / BLOCK_1D;
  k_xavier_init_f32<<<bl, BLOCK_1D>>>((float*)W, (int)n, std, (unsigned long long)seed);
  return 0;
}

} // extern "C"
