#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

TEZZC_BIN="${TEZZC_BIN:-$ROOT/build/tezzc}"
if [[ ! -x "$TEZZC_BIN" ]]; then
  TEZZC_BIN="$ROOT/bin/tezzc-linux-x64"
fi
if [[ ! -x "$TEZZC_BIN" ]]; then
  echo "tier1_perf_gate: missing tezzc binary"
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "tier1_perf_gate: python3 is required"
  exit 1
fi
if ! command -v cc >/dev/null 2>&1; then
  echo "tier1_perf_gate: cc is required"
  exit 1
fi

measure_cmd() {
  local cmd="$1"
  local runs="${2:-5}"
  python3 - "$cmd" "$runs" <<'PY'
import statistics
import subprocess
import sys
import time

cmd = sys.argv[1]
runs = int(sys.argv[2])
vals = []
for _ in range(runs):
    t0 = time.perf_counter_ns()
    rc = subprocess.run(cmd, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
    if rc != 0:
        print(f"command failed ({rc}): {cmd}", file=sys.stderr)
        sys.exit(2)
    vals.append(time.perf_counter_ns() - t0)
vals.sort()
print(vals[len(vals) // 2])
PY
}

mkdir -p build/perf_gate

"$TEZZC_BIN" buildexe bench/tezz_bench_sum.tn build/perf_gate/tezz_sum_native --target linux --vectorize >/dev/null
"$TEZZC_BIN" buildexe bench/tezz_bench_fib.tn build/perf_gate/tezz_fib_native --target linux --vectorize >/dev/null
cc -O3 -std=c11 bench/external/bench_sum.c -o build/perf_gate/c_sum
cc -O3 -std=c11 bench/external/bench_fib.c -o build/perf_gate/c_fib

runs="${TEZZ_TIER1_PERF_RUNS:-5}"

tezz_sum_bc_ns="$(measure_cmd "$TEZZC_BIN run bench/tezz_bench_sum.tn --bc" "$runs")"
tezz_sum_native_ns="$(measure_cmd "./build/perf_gate/tezz_sum_native" "$runs")"
py_sum_ns="$(measure_cmd "python3 bench/external/bench_sum.py" "$runs")"
c_sum_ns="$(measure_cmd "./build/perf_gate/c_sum" "$runs")"

tezz_fib_bc_ns="$(measure_cmd "$TEZZC_BIN run bench/tezz_bench_fib.tn --bc" "$runs")"
tezz_fib_native_ns="$(measure_cmd "./build/perf_gate/tezz_fib_native" "$runs")"
py_fib_ns="$(measure_cmd "python3 bench/external/bench_fib.py" "$runs")"
c_fib_ns="$(measure_cmd "./build/perf_gate/c_fib" "$runs")"

read -r tezz_native_ns tezz_bc_ns py_ns c_ns <<EOF
$(python3 - "$tezz_sum_native_ns" "$tezz_fib_native_ns" "$tezz_sum_bc_ns" "$tezz_fib_bc_ns" "$py_sum_ns" "$py_fib_ns" "$c_sum_ns" "$c_fib_ns" <<'PY'
import sys
vals = [int(x) for x in sys.argv[1:]]
tn = (vals[0] + vals[1]) // 2
tb = (vals[2] + vals[3]) // 2
py = (vals[4] + vals[5]) // 2
c = (vals[6] + vals[7]) // 2
print(tn, tb, py, c)
PY
)
EOF

read -r ratio_native_vs_py ratio_bc_vs_py ratio_native_vs_c <<EOF
$(python3 - "$tezz_native_ns" "$tezz_bc_ns" "$py_ns" "$c_ns" <<'PY'
import sys
tn = max(1, int(sys.argv[1]))
tb = max(1, int(sys.argv[2]))
py = max(1, int(sys.argv[3]))
c = max(1, int(sys.argv[4]))
print(f"{py/tn:.4f}", f"{py/tb:.4f}", f"{tn/c:.4f}")
PY
)
EOF

native_vs_py_min="${TEZZ_GATE_NATIVE_VS_PY_MIN:-1.20}"
bc_vs_py_min="${TEZZ_GATE_BC_VS_PY_MIN:-0.10}"
native_vs_c_max="${TEZZ_GATE_NATIVE_VS_C_MAX:-2.50}"

target_native_vs_py_min="${TEZZ_TARGET_NATIVE_VS_PY_MIN:-3.00}"
target_bc_vs_py_min="${TEZZ_TARGET_BC_VS_PY_MIN:-1.50}"
target_native_vs_c_max="${TEZZ_TARGET_NATIVE_VS_C_MAX:-1.50}"
strict_targets="${TEZZ_GATE_STRICT_TARGETS:-0}"

mkdir -p build
cat > build/perf_tier1_gate.tnx <<EOF
metric,value
tezz_sum_bc_ns,$tezz_sum_bc_ns
tezz_sum_native_ns,$tezz_sum_native_ns
python_sum_ns,$py_sum_ns
c_sum_ns,$c_sum_ns
tezz_fib_bc_ns,$tezz_fib_bc_ns
tezz_fib_native_ns,$tezz_fib_native_ns
python_fib_ns,$py_fib_ns
c_fib_ns,$c_fib_ns
tezz_native_median_ns,$tezz_native_ns
tezz_bc_median_ns,$tezz_bc_ns
python_median_ns,$py_ns
c_median_ns,$c_ns
ratio_native_vs_python,$ratio_native_vs_py
ratio_bc_vs_python,$ratio_bc_vs_py
ratio_native_vs_c,$ratio_native_vs_c
EOF

echo "tier1_perf_gate: ratios native_vs_python=$ratio_native_vs_py bc_vs_python=$ratio_bc_vs_py native_vs_c=$ratio_native_vs_c"

python3 - "$ratio_native_vs_py" "$ratio_bc_vs_py" "$ratio_native_vs_c" "$native_vs_py_min" "$bc_vs_py_min" "$native_vs_c_max" "$target_native_vs_py_min" "$target_bc_vs_py_min" "$target_native_vs_c_max" "$strict_targets" <<'PY'
import sys
r_native_py = float(sys.argv[1])
r_bc_py = float(sys.argv[2])
r_native_c = float(sys.argv[3])
b_native_py = float(sys.argv[4])
b_bc_py = float(sys.argv[5])
b_native_c = float(sys.argv[6])
t_native_py = float(sys.argv[7])
t_bc_py = float(sys.argv[8])
t_native_c = float(sys.argv[9])
strict_targets = int(sys.argv[10]) != 0

bad = 0
if r_native_py < b_native_py:
    print(f"tier1_perf_gate: FAIL blocker native_vs_python {r_native_py:.3f} < {b_native_py:.3f}")
    bad += 1
if r_bc_py < b_bc_py:
    print(f"tier1_perf_gate: FAIL blocker bc_vs_python {r_bc_py:.3f} < {b_bc_py:.3f}")
    bad += 1
if r_native_c > b_native_c:
    print(f"tier1_perf_gate: FAIL blocker native_vs_c {r_native_c:.3f} > {b_native_c:.3f}")
    bad += 1

target_bad = 0
if r_native_py < t_native_py:
    print(f"tier1_perf_gate: WARN target native_vs_python {r_native_py:.3f} < {t_native_py:.3f}")
    target_bad += 1
if r_bc_py < t_bc_py:
    print(f"tier1_perf_gate: WARN target bc_vs_python {r_bc_py:.3f} < {t_bc_py:.3f}")
    target_bad += 1
if r_native_c > t_native_c:
    print(f"tier1_perf_gate: WARN target native_vs_c {r_native_c:.3f} > {t_native_c:.3f}")
    target_bad += 1

if strict_targets and target_bad:
    print("tier1_perf_gate: strict target mode enabled")
    bad += target_bad

if bad:
    sys.exit(1)
print("tier1_perf_gate: OK")
PY
