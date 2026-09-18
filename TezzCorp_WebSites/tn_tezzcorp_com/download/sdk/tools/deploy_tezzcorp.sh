#!/usr/bin/env bash
set -euo pipefail

: "${TEZZ_DEPLOY_HOST:?set TEZZ_DEPLOY_HOST}"
: "${TEZZ_DEPLOY_USER:?set TEZZ_DEPLOY_USER}"
: "${TEZZ_DEPLOY_PATH:?set TEZZ_DEPLOY_PATH}"
: "${TEZZ_DEPLOY_KEY:?set TEZZ_DEPLOY_KEY (base64/private-key path)}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ -f "$TEZZ_DEPLOY_KEY" ]]; then
  KEY_PATH="$TEZZ_DEPLOY_KEY"
else
  KEY_PATH="build/deploy.key"
  mkdir -p build
  printf "%s" "$TEZZ_DEPLOY_KEY" > "$KEY_PATH"
  chmod 600 "$KEY_PATH"
fi

rsync -az --delete \
  -e "ssh -i $KEY_PATH -o StrictHostKeyChecking=accept-new" \
  web/tn_site/public/ "$TEZZ_DEPLOY_USER@$TEZZ_DEPLOY_HOST:$TEZZ_DEPLOY_PATH/"

echo "deploy_tezzcorp: OK"
