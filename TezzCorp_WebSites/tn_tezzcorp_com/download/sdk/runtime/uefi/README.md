# UEFI Package Layout (EDK2)

This folder contains a minimal EDK2 package skeleton for a TezzNative kernel.

Files:
- `TezzKernel.inf`: component definition (sources + entry point)
- `TezzKernel.dsc`: platform description template

Usage:
1) Run `tools/uefi_build.ps1` to generate `kernel.s` and copy stubs.
2) Copy the output folder into your EDK2 workspace as `TezzKernel/`.
3) Build:
   build -a X64 -t VS2019 -p TezzKernel/TezzKernel.dsc

Note: This is a template. You may need to adjust toolchain or paths for your EDK2 setup.
