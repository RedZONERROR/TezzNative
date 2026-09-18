#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

TEZZ="${TEZZ_CMD:-$ROOT/tezz}"
OUT="${TEZZ_AI_OUT:-build/ai_local}"
DATA="$OUT/samples.tnxb"
MODEL="$OUT/model.taim"
CODEBOOK="$OUT/codebook.tnxb"

mkdir -p "$OUT"

echo "[TezzAi] seed local data start"
echo "  out:      $OUT"
echo "  data:     $DATA"
echo "  model:    $MODEL"
echo "  codebook: $CODEBOOK"

"$TEZZ" ai learn-corpus \
  --root examples \
  --root lib \
  --root TezzAi \
  --root tools \
  --data "$DATA" \
  --model "$MODEL" \
  --codebook "$CODEBOOK" \
  --no-train

if [ -s "TezzAi/data/codebook.tnxb" ] && [ "$CODEBOOK" != "TezzAi/data/codebook.tnxb" ]; then
  cat "TezzAi/data/codebook.tnxb" >> "$CODEBOOK"
fi

"$TEZZ" ai autolearn api "build api health route with auth middleware" --data "$DATA" --model "$MODEL" --v2
"$TEZZ" ai autolearn api "build api websocket endpoint with token auth" --data "$DATA" --model "$MODEL" --v2
"$TEZZ" ai autolearn api "build api crud routes for users resource" --data "$DATA" --model "$MODEL" --v2
"$TEZZ" ai autolearn api "build api request validation and json response" --data "$DATA" --model "$MODEL" --v2

"$TEZZ" ai autolearn cli "build cli command parser with args and help output" --data "$DATA" --model "$MODEL" --v2
"$TEZZ" ai autolearn cli "build cli tool with subcommands and flags" --data "$DATA" --model "$MODEL" --v2
"$TEZZ" ai autolearn cli "build cli status command with table output" --data "$DATA" --model "$MODEL" --v2
"$TEZZ" ai autolearn cli "build cli init command with boilerplate files" --data "$DATA" --model "$MODEL" --v2

"$TEZZ" ai autolearn lib "build tokenizer utility with safe bounds checks" --data "$DATA" --model "$MODEL" --v2
"$TEZZ" ai autolearn lib "build lib string normalizer and slug helper" --data "$DATA" --model "$MODEL" --v2
"$TEZZ" ai autolearn lib "build lib config parser with defaults" --data "$DATA" --model "$MODEL" --v2
"$TEZZ" ai autolearn lib "build lib path helper with validation" --data "$DATA" --model "$MODEL" --v2

"$TEZZ" ai autolearn service "build worker loop service with periodic heartbeat" --data "$DATA" --model "$MODEL" --v2
"$TEZZ" ai autolearn service "build service supervisor restart policy" --data "$DATA" --model "$MODEL" --v2
"$TEZZ" ai autolearn service "build service queue processor with retry logic" --data "$DATA" --model "$MODEL" --v2
"$TEZZ" ai autolearn service "build service health monitor and metrics" --data "$DATA" --model "$MODEL" --v2

"$TEZZ" ai train --data "$DATA" --model "$MODEL" --v2
"$TEZZ" ai stats --data "$DATA" --model "$MODEL" --v2

echo "[TezzAi] seed local data complete"
echo "Test generate:"
echo "  $TEZZ ai code \"build api websocket route with auth middleware\" --model \"$MODEL\" --codebook \"$CODEBOOK\" --pick-debug --v2"
echo "Start local UI:"
echo "  export TEZZ_AI_MODEL=\"$MODEL\""
echo "  export TEZZ_AI_CODEBOOK=\"$CODEBOOK\""
echo "  ./TezzAi/scripts/serve_local.sh"
