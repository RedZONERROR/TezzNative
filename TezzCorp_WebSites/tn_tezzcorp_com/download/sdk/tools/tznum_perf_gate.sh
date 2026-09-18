#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
grep -q "micro_tznum_matmul_nn,bc" bench/perf_baseline_tznum.tnx
grep -q "micro_tznum_rmsnorm_nn,bc" bench/perf_baseline_tznum.tnx
echo "tznum_perf_gate: OK"
