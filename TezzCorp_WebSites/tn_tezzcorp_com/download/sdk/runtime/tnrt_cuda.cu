// runtime/tnrt_cuda.cu
// CUDA runtime hooks for TezzNative GPU backend.

#include <cuda_runtime.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" long long tn_simd_v4f_add(double* out, const double* a, const double* b){
  out[0] = a[0] + b[0];
  out[1] = a[1] + b[1];
  out[2] = a[2] + b[2];
  out[3] = a[3] + b[3];
  return 0;
}

extern "C" long long tn_simd_v4f_sub(double* out, const double* a, const double* b){
  out[0] = a[0] - b[0];
  out[1] = a[1] - b[1];
  out[2] = a[2] - b[2];
  out[3] = a[3] - b[3];
  return 0;
}

extern "C" long long tn_simd_v4f_mul(double* out, const double* a, const double* b){
  out[0] = a[0] * b[0];
  out[1] = a[1] * b[1];
  out[2] = a[2] * b[2];
  out[3] = a[3] * b[3];
  return 0;
}

extern "C" long long tn_simd_v4f_dot(const double* a, const double* b){
  double r = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
  long long bits = 0;
  memcpy(&bits, &r, sizeof(bits));
  return bits;
}

extern "C" long long tn_simd_v4i_add(long long* out, const long long* a, const long long* b){
  out[0] = a[0] + b[0];
  out[1] = a[1] + b[1];
  out[2] = a[2] + b[2];
  out[3] = a[3] + b[3];
  return 0;
}

extern "C" long long tn_simd_v4i_mul(long long* out, const long long* a, const long long* b){
  out[0] = a[0] * b[0];
  out[1] = a[1] * b[1];
  out[2] = a[2] * b[2];
  out[3] = a[3] * b[3];
  return 0;
}

extern "C" long long tn_simd_v4f_add_strided(double* out, const double* a, const double* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] + b[0];
  out[s] = a[s] + b[s];
  out[s*2] = a[s*2] + b[s*2];
  out[s*3] = a[s*3] + b[s*3];
  return 0;
}

extern "C" long long tn_simd_v4f_sub_strided(double* out, const double* a, const double* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] - b[0];
  out[s] = a[s] - b[s];
  out[s*2] = a[s*2] - b[s*2];
  out[s*3] = a[s*3] - b[s*3];
  return 0;
}

extern "C" long long tn_simd_v4f_mul_strided(double* out, const double* a, const double* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] * b[0];
  out[s] = a[s] * b[s];
  out[s*2] = a[s*2] * b[s*2];
  out[s*3] = a[s*3] * b[s*3];
  return 0;
}

extern "C" long long tn_simd_v4i_add_strided(long long* out, const long long* a, const long long* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] + b[0];
  out[s] = a[s] + b[s];
  out[s*2] = a[s*2] + b[s*2];
  out[s*3] = a[s*3] + b[s*3];
  return 0;
}

extern "C" long long tn_simd_v4i_mul_strided(long long* out, const long long* a, const long long* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] * b[0];
  out[s] = a[s] * b[s];
  out[s*2] = a[s*2] * b[s*2];
  out[s*3] = a[s*3] * b[s*3];
  return 0;
}

extern "C" long long tn_simd_v4f_load(double* out, const double* p){
  out[0] = p[0];
  out[1] = p[1];
  out[2] = p[2];
  out[3] = p[3];
  return 0;
}

extern "C" long long tn_simd_v4f_store(double* p, const double* v){
  p[0] = v[0];
  p[1] = v[1];
  p[2] = v[2];
  p[3] = v[3];
  return 0;
}

extern "C" long long tn_simd_v4i_load(long long* out, const long long* p){
  out[0] = p[0];
  out[1] = p[1];
  out[2] = p[2];
  out[3] = p[3];
  return 0;
}

extern "C" long long tn_simd_v4i_store(long long* p, const long long* v){
  p[0] = v[0];
  p[1] = v[1];
  p[2] = v[2];
  p[3] = v[3];
  return 0;
}

extern "C" long long tn_gpu_has_backend(void){
  int count = 0;
  cudaError_t st = cudaGetDeviceCount(&count);
  if(st != cudaSuccess) return 0;
  return (count > 0) ? 1 : 0;
}

extern "C" long long tn_gpu_init(void){
  cudaError_t st = cudaFree(0);
  return (st == cudaSuccess) ? 0 : -1;
}

extern "C" long long tn_gpu_shutdown(void){
  cudaError_t st = cudaDeviceReset();
  return (st == cudaSuccess) ? 0 : -1;
}

extern "C" long long tn_gpu_device_count(void){
  int count = 0;
  cudaError_t st = cudaGetDeviceCount(&count);
  if(st != cudaSuccess) return 0;
  return (long long)count;
}

extern "C" long long tn_gpu_set_device(long long id){
  cudaError_t st = cudaSetDevice((int)id);
  return (st == cudaSuccess) ? 0 : -1;
}

extern "C" unsigned char* tn_gpu_alloc(long long bytes){
  void* p = NULL;
  cudaError_t st = cudaMalloc(&p, (size_t)bytes);
  if(st != cudaSuccess) return NULL;
  return (unsigned char*)p;
}

extern "C" long long tn_gpu_free(unsigned char* p){
  cudaError_t st = cudaFree((void*)p);
  return (st == cudaSuccess) ? 0 : -1;
}

extern "C" long long tn_gpu_memcpy_to_device(unsigned char* dst, unsigned char* src, long long bytes){
  cudaError_t st = cudaMemcpy(dst, src, (size_t)bytes, cudaMemcpyHostToDevice);
  return (st == cudaSuccess) ? 0 : -1;
}

extern "C" long long tn_gpu_memcpy_to_host(unsigned char* dst, unsigned char* src, long long bytes){
  cudaError_t st = cudaMemcpy(dst, src, (size_t)bytes, cudaMemcpyDeviceToHost);
  return (st == cudaSuccess) ? 0 : -1;
}

extern "C" long long tn_gpu_memset(unsigned char* dst, long long value, long long bytes){
  cudaError_t st = cudaMemset(dst, (int)value, (size_t)bytes);
  return (st == cudaSuccess) ? 0 : -1;
}

extern "C" long long tn_gpu_launch_1d(long long kernel, long long grid, long long block, unsigned char* args, long long args_size){
  (void)args_size;
  void* params[] = { &args };
  cudaError_t st = cudaLaunchKernel((const void*)kernel, dim3((unsigned int)grid), dim3((unsigned int)block), params, 0, 0);
  if(st != cudaSuccess) return -1;
  st = cudaDeviceSynchronize();
  return (st == cudaSuccess) ? 0 : -1;
}

extern "C" long long tn_gpu_launch_dxil_1d(unsigned char* path, long long grid, long long block, unsigned char* args){
  (void)path; (void)grid; (void)block; (void)args;
  return -1;
}

extern "C" unsigned char* tn_gpu_backend_name(void){
  return (unsigned char*)"cuda";
}

extern "C" long long tn_gpu_supports_dxil(void){
  return 0;
}

extern "C" long long tn_npu_has_backend(void){ return tn_gpu_has_backend(); }
extern "C" long long tn_npu_init(void){ return tn_gpu_init(); }
extern "C" long long tn_npu_shutdown(void){ return tn_gpu_shutdown(); }
extern "C" long long tn_npu_device_count(void){ return tn_gpu_device_count(); }
extern "C" long long tn_npu_set_device(long long id){ return tn_gpu_set_device(id); }
extern "C" unsigned char* tn_npu_alloc(long long bytes){ return tn_gpu_alloc(bytes); }
extern "C" long long tn_npu_free(unsigned char* p){ return tn_gpu_free(p); }
extern "C" long long tn_npu_memcpy_to_device(unsigned char* dst, unsigned char* src, long long bytes){
  return tn_gpu_memcpy_to_device(dst, src, bytes);
}
extern "C" long long tn_npu_memcpy_to_host(unsigned char* dst, unsigned char* src, long long bytes){
  return tn_gpu_memcpy_to_host(dst, src, bytes);
}
extern "C" long long tn_npu_memset(unsigned char* dst, long long value, long long bytes){
  return tn_gpu_memset(dst, value, bytes);
}
extern "C" long long tn_npu_launch_1d(long long kernel, long long grid, long long block, unsigned char* args, long long args_size){
  return tn_gpu_launch_1d(kernel, grid, block, args, args_size);
}
extern "C" long long tn_npu_launch_dxil_1d(unsigned char* path, long long grid, long long block, unsigned char* args){
  (void)path; (void)grid; (void)block; (void)args;
  return -1;
}
extern "C" unsigned char* tn_npu_backend_name(void){
  return (unsigned char*)"cuda";
}
extern "C" long long tn_npu_supports_dxil(void){
  return 0;
}
extern "C" long long tn_npu_model_load(unsigned char* path){
  (void)path;
  return -1;
}
extern "C" long long tn_npu_model_free(long long h){
  (void)h;
  return -1;
}
extern "C" long long tn_npu_model_run_f64_1d(long long h, double* input, long long input_len, double* output, long long output_len){
  (void)h; (void)input; (void)input_len; (void)output; (void)output_len;
  return -1;
}
