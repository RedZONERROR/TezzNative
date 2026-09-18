#!/usr/bin/env bash
set -euo pipefail

SDK_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TEZZ_BIN="${TEZZ_BIN:-$SDK_ROOT/tezz}"

if [[ ! -x "$TEZZ_BIN" ]]; then
  echo "runtime_io_gate: tezz launcher not found: $TEZZ_BIN"
  exit 1
fi

"$TEZZ_BIN" test --io-smoke
"$TEZZ_BIN" lock
"$TEZZ_BIN" verify --strict
