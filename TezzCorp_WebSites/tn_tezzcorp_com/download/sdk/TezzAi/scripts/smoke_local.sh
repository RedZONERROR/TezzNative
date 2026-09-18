#!/usr/bin/env sh
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

TEZZ="${TEZZ_CMD:-./tezz}"
if [ -n "${TEZZ_AI_MODEL:-}" ]; then
  MODEL="$TEZZ_AI_MODEL"
elif [ -f "build/ai_local/model.taim" ]; then
  MODEL="build/ai_local/model.taim"
else
  MODEL="TezzAi/data/model.taim"
fi
if [ -n "${TEZZ_AI_CODEBOOK:-}" ]; then
  CODEBOOK="$TEZZ_AI_CODEBOOK"
elif [ -f "build/ai_local/codebook.tnxb" ]; then
  CODEBOOK="build/ai_local/codebook.tnxb"
else
  CODEBOOK="TezzAi/data/codebook.tnxb"
fi

HOST="${TEZZ_AI_HOST:-127.0.0.1}"
PORT="${TEZZ_AI_PORT:-8099}"
PROMPT="${TEZZ_AI_SMOKE_PROMPT:-build api websocket route with auth middleware}"
PROMPT_Q="$(printf "%s" "$PROMPT" | sed 's/ /%20/g')"
LOG="build/ai_local/serve_smoke_$$.log"

mkdir -p build/ai_local
rm -f "$LOG" >/dev/null 2>&1 || true

echo "[TezzAi smoke] start serve"
echo "  host:     $HOST"
echo "  port:     $PORT"
echo "  model:    $MODEL"
echo "  codebook: $CODEBOOK"

"$TEZZ" ai serve --host "$HOST" --port "$PORT" --model "$MODEL" --codebook "$CODEBOOK" --max-clients 3 --v2 >"$LOG" 2>&1 &
SRV_PID=$!

cleanup() {
  if kill -0 "$SRV_PID" >/dev/null 2>&1; then
    kill "$SRV_PID" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT INT TERM

i=1
while [ "$i" -le 30 ]; do
  if HEALTH="$(curl -sS --connect-timeout 1 --max-time 2 "http://$HOST:$PORT/health" 2>/dev/null)"; then
    break
  fi
  i=$((i + 1))
  sleep 1
done

if [ "${HEALTH:-}" = "" ]; then
  echo "[TezzAi smoke] FAIL: /health did not respond"
  echo "--- serve log ---"
  cat "$LOG" || true
  exit 1
fi

echo "$HEALTH" | grep -q '"ok":true' || {
  echo "[TezzAi smoke] FAIL: /health missing ok=true"
  echo "$HEALTH"
  exit 1
}
echo "$HEALTH" | grep -q '"identity"' || {
  echo "[TezzAi smoke] FAIL: /health missing identity payload"
  echo "$HEALTH"
  exit 1
}

CODE_JSON="$(curl -sS --connect-timeout 1 --max-time 4 "http://$HOST:$PORT/ai/code?prompt=$PROMPT_Q" 2>/dev/null || true)"
echo "$CODE_JSON" | grep -q '"ok":true' || {
  echo "[TezzAi smoke] FAIL: /ai/code missing ok=true"
  echo "$CODE_JSON"
  echo "--- serve log ---"
  cat "$LOG" || true
  exit 1
}
echo "$CODE_JSON" | grep -q '"code"' || {
  echo "[TezzAi smoke] FAIL: /ai/code missing code payload"
  echo "$CODE_JSON"
  exit 1
}

ROOT_HTML="$(curl -sS --connect-timeout 1 --max-time 2 "http://$HOST:$PORT/" 2>/dev/null || true)"
echo "$ROOT_HTML" | grep -q 'TezzAi Local' || {
  echo "[TezzAi smoke] FAIL: / did not return TezzAi UI"
  echo "$ROOT_HTML"
  exit 1
}

wait "$SRV_PID" || true

echo "[TezzAi smoke] PASS"
echo "  /health and /ai/code validated"
