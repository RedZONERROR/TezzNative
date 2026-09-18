#!/bin/bash
# build_tzgpu.sh -- Build tzgpu_kernels.cu on RTX 3060 machine (sm_86, CUDA 12.9)
set -e

NVCC="/mnt/c/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9/bin/nvcc"
CUDA_INC="/mnt/c/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9/include"
CUDA_LIB="/mnt/c/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9/lib/x64"
SRC="/c/Users/aveng/tzgpu_kernels.cu"
OUT="/c/Users/aveng/libtzgpu.so"
OUT_DLL="/c/Users/aveng/tzgpu.dll"

echo "=== nvcc version ==="
"$NVCC" --version

echo ""
echo "=== Compiling tzgpu_kernels.cu → libtzgpu.so ==="
"$NVCC" -O3 -arch=sm_86 \
  -I"$CUDA_INC" \
  --shared -Xcompiler -fPIC \
  "$SRC" -o "$OUT" \
  -L"$CUDA_LIB" -lcublas -lcuda

echo "NVCC_EXIT=$?"

if [ -f "$OUT" ]; then
  echo ""
  echo "=== libtzgpu.so built OK ==="
  ls -lah "$OUT"

  echo ""
  echo "=== Running GPU info test ==="
  cat > /tmp/tzgpu_test.c << 'CEOF'
#include <stdio.h>
#include <stdint.h>

extern int64_t tz_gpu_info();
extern int64_t tz_cuda_device_count();
extern int64_t tz_cuda_mem_free();
extern int64_t tz_cuda_mem_total();

int main(){
  int64_t n = tz_cuda_device_count();
  printf("GPUs detected: %lld\n", n);
  printf("Free VRAM: %.2f GB\n", tz_cuda_mem_free() / 1073741824.0);
  printf("Total VRAM: %.2f GB\n", tz_cuda_mem_total() / 1073741824.0);
  tz_gpu_info();
  return 0;
}
CEOF
  gcc /tmp/tzgpu_test.c -L/c/Users/aveng -ltzgpu \
    -L"$CUDA_LIB" -lcublas -lcuda \
    -Wl,-rpath,/c/Users/aveng \
    -o /tmp/tzgpu_test 2>&1

  if [ $? -eq 0 ]; then
    echo "=== GPU test binary built ==="
    LD_LIBRARY_PATH="/c/Users/aveng:$LD_LIBRARY_PATH" /tmp/tzgpu_test
  else
    echo "(link test failed - shared library may still work at runtime)"
  fi
else
  echo "=== BUILD FAILED ==="
  exit 1
fi
