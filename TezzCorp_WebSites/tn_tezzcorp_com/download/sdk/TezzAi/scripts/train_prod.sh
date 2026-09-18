#!/usr/bin/env sh
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

TEZZ="${TEZZ_CMD:-./tezz}"
DATA="${TEZZ_AI_DATA:-TezzAi/data/samples.tnxb}"
MODEL="${TEZZ_AI_MODEL:-TezzAi/data/model.taim}"
CODEBOOK="${TEZZ_AI_CODEBOOK:-TezzAi/data/codebook.tnxb}"

echo "[TezzAi] production training start"
echo "  data:     $DATA"
echo "  model:    $MODEL"
echo "  codebook: $CODEBOOK"

"$TEZZ" ai production --data "$DATA" --model "$MODEL" --codebook "$CODEBOOK"
"$TEZZ" ai stats --data "$DATA" --model "$MODEL" --v2

echo "[TezzAi] production training complete"
echo "Ask anytime:"
echo "  $TEZZ ai ask \"build api websocket route with auth middleware\" --model \"$MODEL\" --codebook \"$CODEBOOK\" --v2"
