# M8 Platform GA Signoff

## Required Evidence
- Product owner signoff with the intended version and channel.
- Runtime lead signoff confirming runtime-hardening, runtime IO smoke, and buildexe lanes passed.
- Release engineering signoff confirming source builds, SDK packaging, and release-policy checks passed.

## Blocking Conditions
- Any empty required manifest.
- Any CI matrix lane missing Linux, Windows, or macOS coverage.
- Any placeholder production claim that is not backed by a passing release gate.
- Any compiler path that reports success after lowering failure.
