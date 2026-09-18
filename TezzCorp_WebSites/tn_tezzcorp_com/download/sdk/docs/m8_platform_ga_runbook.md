# M8 Platform GA Runbook

## Scope
- Validate that the same compiler source revision builds natively on Linux, Windows, and macOS.
- Validate BC-VM execution, buildexe verification, runtime IO smoke, and SDK packaging for each tier-1 host lane.

## GA Steps
1. Confirm the platform-validation matrix is green for Linux x64, Windows x64, and macOS arm64.
2. Confirm the runtime IO gate is green on the same revision.
3. Review packaged SDK archives and launcher bootstrap behavior on each platform.
4. Review signed release artifacts, reproducible-build output, and benchmark gates.
5. Approve GA only when the repo channel and documentation match the validated status.
