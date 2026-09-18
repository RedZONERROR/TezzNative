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
if [ -n "${TEZZ_AI_DB:-}" ]; then
  DB="$TEZZ_AI_DB"
elif [ -f "build/ai_local/llm_tezzdb.tdb" ]; then
  DB="build/ai_local/llm_tezzdb.tdb"
else
  DB="TezzAi/data/llm_tezzdb.tdb"
fi
MODE="${TEZZ_AI_MODE:-llm}"
HOST="${TEZZ_AI_HOST:-127.0.0.1}"
PORT="${TEZZ_AI_PORT:-8099}"

echo "[TezzAi] local server start"
echo "  host:     $HOST"
echo "  port:     $PORT"
echo "  mode:     $MODE"
echo "  model:    $MODEL"
if [ "$MODE" = "coding" ]; then
  echo "  codebook: $CODEBOOK"
else
  echo "  tezzdb:   $DB"
fi
echo "  ui:       http://$HOST:$PORT/"
echo
echo "Press Ctrl+C to stop."
echo

if [ "$MODE" = "coding" ]; then
  "$TEZZ" ai serve --host "$HOST" --port "$PORT" --model "$MODEL" --codebook "$CODEBOOK" --v2
  exit $?
fi

"$TEZZ" ai llm serve --host "$HOST" --port "$PORT" --model "$MODEL" --db "$DB"
