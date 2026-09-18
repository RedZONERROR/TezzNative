#!/bin/sh
# build_tzgpu_msys2.sh -- Build tzgpu_kernels.cu using MSYS2 paths (CUDA 12.9, sm_86)
NVCC="/c/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9/bin/nvcc"
CUDA_INC="/c/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9/include"
CUDA_LIB="/c/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9/lib/x64"
SRC="/c/Users/aveng/tzgpu_kernels.cu"
OUT="/c/Users/aveng/libtzgpu.so"

echo "=== nvcc version ==="
"$NVCC" --version

echo ""
echo "=== Compiling tzgpu_kernels.cu -> libtzgpu.so ==="
"$NVCC" \
  -O3 \
  -arch=sm_86 \
  -I"$CUDA_INC" \
  --shared -Xcompiler -fPIC \
  "$SRC" \
  -o "$OUT" \
  -L"$CUDA_LIB" \
  -lcublas -lcuda 2>&1

echo "NVCC_EXIT=$?"

if [ -f "$OUT" ]; then
  echo ""
  echo "SUCCESS: libtzgpu.so built at $OUT"
  ls -lah "$OUT"
else
  echo "FAILED: output not found at $OUT"
  exit 1
fi
