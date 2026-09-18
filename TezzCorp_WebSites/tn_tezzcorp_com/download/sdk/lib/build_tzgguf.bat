@echo off
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
call %VCVARS% x64
cd /d C:\Users\aveng\TezzNative\TezzNative-language
cl /nologo /O2 /LD lib\tzgguf_kernels.c /Fe:lib\tzgguf.dll
