# Release Candidate Runbook

## RC Workflow
1. Build the compiler from source on Linux, Windows, and macOS using the strict build scripts.
2. Run `tezz release-policy-check` and `tezz reprocheck` on the candidate commit.
3. Run the production CI matrix: conformance strict, stdlib-v1, tooling, GUI lane, runtime hardening, runtime IO smoke, SDK packaging, and `release-artifacts --verify-repro`.
4. Run the deploy workflow so `tools/publish_download_bundle.sh` updates `web/tn_site/public/download` before publishing `tn.tezzcorp.com`.
5. Produce release artifacts and signatures only after the CI matrix matches the candidate revision.
6. Record artifact hashes, uploaded archive names, website deploy run ID, and signoff evidence in the release ticket.

## Validation Notes
- Confirm that no manifest is empty and no lane still points only at `tests/smoke_basic.tn`.
- Confirm that Windows and macOS lanes are validating native bootstrap builds rather than relying on a prebuilt Linux compiler.
- Confirm that `buildexe` failures surface directly and do not fall back to stub executables.

## rollback
- Keep the previous signed artifact set and previous green CI revision available for immediate rollback.
- If a post-cut regression appears, remove the candidate artifact links, restore the previous signed release, and document the regression in the release log.
- Re-run policy, conformance, runtime, and packaging gates before attempting another promotion.
