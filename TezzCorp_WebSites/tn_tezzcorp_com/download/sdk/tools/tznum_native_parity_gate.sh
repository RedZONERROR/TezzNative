#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if [[ ! -f bench/c/tznum_native_parity.c ]]; then
  echo "tznum_native_parity_gate: missing bench/c/tznum_native_parity.c"
  exit 1
fi
echo "tznum_native_parity_gate: OK"
