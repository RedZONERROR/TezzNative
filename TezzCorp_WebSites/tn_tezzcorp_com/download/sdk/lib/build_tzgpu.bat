@echo off
REM build_tzgpu.bat -- Build tzgpu_kernels.cu as tzgpu.dll on Windows (CUDA 12.9 + VS2022 + sm_86)
REM Run from cmd.exe on the GPU machine: C:\Users\aveng\build_tzgpu.bat

set CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9
set NVCC="%CUDA_PATH%\bin\nvcc.exe"
set SRC=C:\Users\aveng\tzgpu_kernels.cu
set OUT=C:\Users\aveng\tzgpu.dll
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"

echo === Setting up MSVC environment ===
call %VCVARS% x64
if errorlevel 1 ( echo ERROR: vcvarsall failed & exit /b 1 )

echo.
echo === nvcc version ===
%NVCC% --version

echo.
echo === Compiling tzgpu_kernels.cu to tzgpu.dll ===
%NVCC% ^
  -O3 ^
  -arch=sm_86 ^
  -I"%CUDA_PATH%\include" ^
  --shared ^
  "%SRC%" ^
  -o "%OUT%" ^
  -L"%CUDA_PATH%\lib\x64" ^
  -lcublas ^
  2>&1

if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)

echo.
if exist "%OUT%" (
  echo SUCCESS: tzgpu.dll built at %OUT%
  dir "%OUT%"
) else (
  echo FAILED: output not found
  exit /b 1
)
