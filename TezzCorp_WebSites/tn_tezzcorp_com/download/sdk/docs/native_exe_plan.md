# Native Executable Plan

## Backend Matrix
- Windows x64 host: PE x64 emit + verify + host run.
- Windows cross verification: PE x86 verify and PE arm64 verify.
- Linux x64 host: ELF emit + verify + host run.
- macOS arm64 host: Mach-O emit + verify + host run.
- GPU backends: `hlsl` and `dxil` remain explicit backends; `metal`, `vulkan`, and `directml` are hard failures until real lowering exists.

## Honest Failure Rules
- If PE lowering fails, `buildexe` returns failure and emits no stub executable.
- If a target backend is declared experimental but has no lowering implementation, the compiler exits with a direct error.
- Release workflows may not treat placeholder binaries or placeholder shader files as successful outputs.

## Build And Packaging Inputs
- `CMakeLists.txt` provides a cross-platform source build entry point.
- `tools/build_core_strict.sh` and `tools/build_core_strict.ps1` are the release-lane bootstrap compilers and publish canonical outputs into `bin/`.
- `tools/package_sdk.sh` and `tools/package_sdk.ps1` create host-native SDK archives from the same source build.
- `tools/publish_download_bundle.sh` stages SDK archives and checksum files into `web/tn_site/public/download` for website deployment.

## Promotion Checklist
- Source build succeeds on Linux, Windows, and macOS.
- `buildexe --verify` passes for every platform manifest.
- Host-run parity passes for the tier-1 executable lane on each host platform.
- Unsupported GPU targets fail loudly and are excluded from GA claims.
