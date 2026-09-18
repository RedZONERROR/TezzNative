#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

DOWNLOAD_DIR="web/tn_site/public/download"
mkdir -p "$DOWNLOAD_DIR"

hash_file() {
  local path="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$path" | awk '{print $1}'
  else
    shasum -a 256 "$path" | awk '{print $1}'
  fi
}

copied=0
for src in \
  "build/tezznative-sdk-linux.tar.gz" \
  "build/tezznative-sdk-macos.tar.gz" \
  "build/tezznative-sdk.zip"; do
  if [[ -f "$src" ]]; then
    base="$(basename "$src")"
    dst="$DOWNLOAD_DIR/$base"
    cp -f "$src" "$dst"
    sum="$(hash_file "$dst")"
    printf "%s  %s\n" "$sum" "$base" > "$dst.sha256"
    copied=1
  fi
done

if [[ -f "build/release_artifacts.tnx" ]]; then
  cp -f "build/release_artifacts.tnx" "$DOWNLOAD_DIR/release_artifacts.tnx"
fi
if [[ -f "build/reproducible_build.tnx" ]]; then
  cp -f "build/reproducible_build.tnx" "$DOWNLOAD_DIR/reproducible_build.tnx"
fi

if [[ "$copied" -eq 0 ]]; then
  echo "publish_download_bundle: no SDK archives found under build/"
  exit 1
fi

echo "publish_download_bundle: updated $DOWNLOAD_DIR"
