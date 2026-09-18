#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

TEZZ_BIN="${TEZZ_BIN:-$ROOT/tezz}"
if [[ ! -x "$TEZZ_BIN" ]]; then
  echo "startup_latency_gate: tezz launcher not found: $TEZZ_BIN"
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "startup_latency_gate: python3 is required"
  exit 1
fi

mkdir -p build
PROFILE_OUT="build/startup_profile_gate.tnx"
rm -f "$PROFILE_OUT"

runs="${TEZZ_STARTUP_GATE_RUNS:-7}"
i=0
while [[ "$i" -lt "$runs" ]]; do
  "$TEZZ_BIN" run tests/smoke_basic.tn --startup-profile --startup-profile-out "$PROFILE_OUT" >/dev/null
  i=$((i + 1))
done

startup_p50_ms="$(python3 - "$PROFILE_OUT" <<'PY'
import csv
import statistics
import sys
path = sys.argv[1]
vals = []
with open(path, newline="") as f:
    reader = csv.DictReader(f)
    for row in reader:
        try:
            vals.append(int(row["total_ms"]))
        except Exception:
            pass
if not vals:
    print("0")
    sys.exit(0)
vals.sort()
print(vals[len(vals)//2])
PY
)"

os_name="$(uname -s)"
startup_max_ms="${TEZZ_STARTUP_P50_MAX_MS:-}"
if [[ -z "$startup_max_ms" ]]; then
  case "$os_name" in
    Linux*) startup_max_ms=120 ;;
    Darwin*) startup_max_ms=150 ;;
    MINGW*|MSYS*|CYGWIN*) startup_max_ms=170 ;;
    *) startup_max_ms=170 ;;
  esac
fi

read -r repl_prompt_ms repl_expr_ms <<EOF
$(python3 - "$TEZZ_BIN" <<'PY'
import statistics
import subprocess
import sys
import time

tezz = sys.argv[1]

def measure(inp: str, runs: int = 5) -> float:
    vals = []
    for _ in range(runs):
        t0 = time.perf_counter_ns()
        p = subprocess.run([tezz, "repl", "--mode", "simple"], input=inp, text=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if p.returncode != 0:
            raise SystemExit(f"repl command failed ({p.returncode})")
        vals.append((time.perf_counter_ns() - t0) / 1_000_000.0)
    vals.sort()
    return vals[len(vals)//2]

prompt_ms = measure(".quit\n")
expr_total_ms = measure("1+2\n.quit\n")
expr_ms = expr_total_ms - prompt_ms
if expr_ms < 0:
    expr_ms = 0.0
print(f"{prompt_ms:.3f} {expr_ms:.3f}")
PY
)
EOF

repl_prompt_max_ms="${TEZZ_REPL_FIRST_PROMPT_MAX_MS:-300}"
repl_expr_max_ms="${TEZZ_REPL_EXPR_MEDIAN_MAX_MS:-120}"

cat > build/startup_latency_gate.tnx <<EOF
metric,value
startup_p50_ms,$startup_p50_ms
startup_max_ms,$startup_max_ms
repl_first_prompt_ms,$repl_prompt_ms
repl_first_prompt_max_ms,$repl_prompt_max_ms
repl_expr_roundtrip_ms,$repl_expr_ms
repl_expr_roundtrip_max_ms,$repl_expr_max_ms
EOF

echo "startup_latency_gate: startup_p50_ms=$startup_p50_ms (max=$startup_max_ms)"
echo "startup_latency_gate: repl_first_prompt_ms=$repl_prompt_ms (max=$repl_prompt_max_ms)"
echo "startup_latency_gate: repl_expr_roundtrip_ms=$repl_expr_ms (max=$repl_expr_max_ms)"

python3 - "$startup_p50_ms" "$startup_max_ms" "$repl_prompt_ms" "$repl_prompt_max_ms" "$repl_expr_ms" "$repl_expr_max_ms" <<'PY'
import sys
startup = float(sys.argv[1])
startup_max = float(sys.argv[2])
repl_prompt = float(sys.argv[3])
repl_prompt_max = float(sys.argv[4])
repl_expr = float(sys.argv[5])
repl_expr_max = float(sys.argv[6])

bad = 0
if startup > startup_max:
    print(f"startup_latency_gate: FAIL startup p50 {startup:.3f} > {startup_max:.3f}")
    bad += 1
if repl_prompt > repl_prompt_max:
    print(f"startup_latency_gate: FAIL repl first prompt {repl_prompt:.3f} > {repl_prompt_max:.3f}")
    bad += 1
if repl_expr > repl_expr_max:
    print(f"startup_latency_gate: FAIL repl expr roundtrip {repl_expr:.3f} > {repl_expr_max:.3f}")
    bad += 1
if bad:
    sys.exit(1)
print("startup_latency_gate: OK")
PY
