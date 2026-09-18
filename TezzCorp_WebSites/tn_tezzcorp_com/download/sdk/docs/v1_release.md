# TezzNative v1 Release

- Current repo channel: `release-candidate`
- Target production label: `1.0.0`
- Primary workloads: CLI tools, backend services, scripting, native host utilities

## Support Matrix
- Linux x64: tier-1 source build, BC-VM run, native packaging, and buildexe host verification in CI.
- Windows x64: tier-1 source build, BC-VM run, SDK packaging, PE host verification, and x86/arm64 PE verification in CI.
- macOS arm64: tier-1 source build, BC-VM run, SDK packaging, and Mach-O verification in CI.
- Experimental GPU targets (`metal`, `vulkan`, `directml`) are intentionally non-release until real lowering exists; the compiler must stop with an explicit error.

## Release Inputs
- `tools/build_core_strict.sh` and `tools/build_core_strict.ps1` must build the compiler from source with warnings treated as errors and sync canonical binaries into `bin/`.
- `.github/workflows/tezz-production-gates.yml` must validate Linux, Windows, and macOS lanes and publish native SDK artifacts.
- `.github/workflows/tezz-production-gates.yml` must also run the GUI lane (`tezz test --gui`) on Linux, Windows, and macOS.
- `.github/workflows/tezz-runtime-io-gate.yml` must validate runtime IO and simple-mode smoke coverage on Linux, Windows, and macOS.
- `.github/workflows/tezz-deploy.yml` must stage a download bundle (`tools/publish_download_bundle.sh`) before syncing `web/tn_site/public` to `tn.tezzcorp.com`.
- `tezz release-policy-check` must pass, including manifest non-emptiness, CI matrix checks, and required release docs.
- Conformance, stdlib-v1, tooling, runtime-hardening, buildexe, ABI, IR, and freestanding manifests must all execute from repo fixtures rather than placeholder references.
- GUI lane manifests (`tests/gui_manifest.tnx` and `tests/gui_check_manifest.tnx`) must stay non-empty and runnable in headless CI.
- The first GUI reference app (`examples/gui_first_app.tn`) must remain compilable in GUI check lanes.

## Artifact Set
- Native compiler binaries produced from source in `bin/tezzc` / `bin/tezzc.exe` with compatibility mirrors in `build/tezzc` / `build/tezzc.exe`.
- SDK archives from `tools/package_sdk.sh` and `tools/package_sdk.ps1`.
- Signed release manifest output from `tezz release-artifacts`.
- Reproducible-build report from `tezz reprocheck`.

## Promotion Rule
- Keep `version.json` on `release-candidate` until every tier-1 CI lane is green on the same revision.
- Promotion to `production` requires passing `release-policy-check`, `reprocheck`, runtime IO smoke, conformance strict, stdlib-v1, tooling, GUI lane, runtime hardening, platform packaging, and `release-artifacts --verify-repro`.
- Promotion also requires that unsupported backends fail explicitly and that no release workflow depends on placeholder docs or empty manifests.
- Once those conditions are met, the channel can be flipped to `production` without additional code-path changes.
