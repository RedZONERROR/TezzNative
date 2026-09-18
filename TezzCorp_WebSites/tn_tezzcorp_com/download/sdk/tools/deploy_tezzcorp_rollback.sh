#!/usr/bin/env bash
set -euo pipefail

: "${TEZZ_ROLLBACK_HOST:?set TEZZ_ROLLBACK_HOST}"
: "${TEZZ_ROLLBACK_USER:?set TEZZ_ROLLBACK_USER}"
: "${TEZZ_ROLLBACK_PATH:?set TEZZ_ROLLBACK_PATH}"
: "${TEZZ_ROLLBACK_REF:?set TEZZ_ROLLBACK_REF}"
: "${TEZZ_ROLLBACK_KEY:?set TEZZ_ROLLBACK_KEY (base64/private-key path)}"

if [[ -f "$TEZZ_ROLLBACK_KEY" ]]; then
  KEY_PATH="$TEZZ_ROLLBACK_KEY"
else
  mkdir -p build
  KEY_PATH="build/rollback.key"
  printf "%s" "$TEZZ_ROLLBACK_KEY" > "$KEY_PATH"
  chmod 600 "$KEY_PATH"
fi

ssh -i "$KEY_PATH" -o StrictHostKeyChecking=accept-new \
  "$TEZZ_ROLLBACK_USER@$TEZZ_ROLLBACK_HOST" \
  "cd '$TEZZ_ROLLBACK_PATH' && ln -sfn '$TEZZ_ROLLBACK_REF' current"

echo "deploy_tezzcorp_rollback: OK"
