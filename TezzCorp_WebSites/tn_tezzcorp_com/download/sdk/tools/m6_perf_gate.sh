#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ ! -x "./tezz" ]]; then
  echo "m6_perf_gate: missing ./tezz launcher"
  exit 1
fi

grep -q "bench_sum,bc" bench/perf_baseline_m6.tnx
grep -q "bench_fib,native" bench/perf_baseline_m6.tnx
grep -q "bench_sum,python,interp" bench/perf_compare_matrix.tnx
grep -q "bench_sum,c,native" bench/perf_compare_matrix.tnx
grep -q "bench_fib,python,interp" bench/perf_compare_matrix.tnx
grep -q "bench_fib,c,native" bench/perf_compare_matrix.tnx

bash tools/startup_latency_gate.sh
bash tools/tier1_perf_gate.sh

echo "m6_perf_gate: OK"
