#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
./tezz test --io-smoke
./tezz test --simple-smoke
./tezz lock
./tezz verify --strict
bash tools/startup_latency_gate.sh
echo "m8_platform_ga_gate: OK"
