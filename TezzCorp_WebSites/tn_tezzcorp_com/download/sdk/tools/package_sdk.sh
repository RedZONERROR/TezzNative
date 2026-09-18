#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

COMPILER=""
if [[ -x bin/tezzc ]]; then
  COMPILER="bin/tezzc"
elif [[ -x build/tezzc ]]; then
  COMPILER="build/tezzc"
else
  echo "package_sdk: missing bin/tezzc or build/tezzc (run tools/build_core_strict.sh first)"
  exit 1
fi

case "$(uname -s)" in
  Darwin) ARCHIVE="build/tezznative-sdk-macos.tar.gz" ;;
  Linux) ARCHIVE="build/tezznative-sdk-linux.tar.gz" ;;
  *) echo "package_sdk: unsupported OS"; exit 1 ;;
esac

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
STAGE="$TMP_DIR/TezzNative-language"
mkdir -p "$STAGE/build" "$STAGE/bin"

for item in CMakeLists.txt README.md SECURITY.md benchmarks docs examples include lib modules projects runtime src tests tezznative-vscode tools web tezz tezz.cmd tezz.ps1 tezz.mod tezz.lock registry.tnx version.json; do
  cp -R "$item" "$STAGE/"
done

cp "$COMPILER" "$STAGE/build/tezzc"
cp "$COMPILER" "$STAGE/bin/tezzc"
cp "$COMPILER" "$STAGE/bin/tezzc-linux-x64"
if [[ -f bin/tezzc.exe ]]; then
  cp bin/tezzc.exe "$STAGE/bin/tezzc.exe"
fi
if [[ -f bin/tezzc-windows-x64.exe ]]; then
  cp bin/tezzc-windows-x64.exe "$STAGE/bin/tezzc-windows-x64.exe"
fi
for launcher in "bin/tezz" "bin/tezz.cmd" "bin/tezz.ps1"; do
  if [[ -f "$launcher" ]]; then
    cp "$launcher" "$STAGE/bin/"
  fi
done
chmod +x "$STAGE/tezz" "$STAGE/build/tezzc" "$STAGE/bin/tezzc" "$STAGE/bin/tezzc-linux-x64"
if [[ -f "$STAGE/bin/tezz" ]]; then
  chmod +x "$STAGE/bin/tezz"
fi

tar -czf "$ARCHIVE" -C "$TMP_DIR" TezzNative-language
echo "package_sdk: wrote $ARCHIVE"
