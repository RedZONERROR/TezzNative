#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-update}"
BASE_URL="${TEZZ_INSTALL_BASE_URL:-https://tn.tezzcorp.com/download}"
INSTALL_ROOT="${TEZZ_INSTALL_ROOT:-$HOME/.tezznative}"
BIN_DIR="${TEZZ_INSTALL_BIN_DIR:-$HOME/.local/bin}"
ARCHIVE="tezznative-sdk-linux.tar.gz"
case "$(uname -s)" in
  Darwin) ARCHIVE="tezznative-sdk-macos.tar.gz" ;;
  Linux) ARCHIVE="tezznative-sdk-linux.tar.gz" ;;
  *) echo "install.sh: unsupported OS"; exit 1 ;;
esac

if [[ "$MODE" == "check" ]]; then
  echo "install check: base=$BASE_URL archive=$ARCHIVE"
  exit 0
fi

if [[ "$MODE" == "uninstall" ]]; then
  rm -rf "$INSTALL_ROOT/current"
  rm -f "$BIN_DIR/tezz"
  echo "tezz uninstall complete"
  exit 0
fi

mkdir -p "$INSTALL_ROOT" "$BIN_DIR"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
ARCHIVE_PATH="$TMP_DIR/$ARCHIVE"
SHA_PATH="$ARCHIVE_PATH.sha256"

curl -fsSL "$BASE_URL/$ARCHIVE" -o "$ARCHIVE_PATH"
curl -fsSL "$BASE_URL/$ARCHIVE.sha256" -o "$SHA_PATH"

EXPECTED="$(awk '{print $1}' "$SHA_PATH" | head -n1)"
if command -v sha256sum >/dev/null 2>&1; then
  ACTUAL="$(sha256sum "$ARCHIVE_PATH" | awk '{print $1}')"
else
  ACTUAL="$(shasum -a 256 "$ARCHIVE_PATH" | awk '{print $1}')"
fi
if [[ -z "$EXPECTED" || "$EXPECTED" != "$ACTUAL" ]]; then
  echo "install.sh: checksum verification failed"
  exit 1
fi

if [[ "${TEZZ_INSTALL_SIGNATURE_REQUIRED:-0}" == "1" ]]; then
  curl -fsSL "$BASE_URL/$ARCHIVE.sig" -o "$ARCHIVE_PATH.sig"
  PUBKEY_PATH="${TEZZ_INSTALL_PUBKEY_PEM:-$TMP_DIR/tezz_release_pub.pem}"
  if [[ ! -f "$PUBKEY_PATH" ]]; then
    curl -fsSL "$BASE_URL/tezz_release_pub.pem" -o "$PUBKEY_PATH"
  fi
  openssl dgst -sha256 -verify "$PUBKEY_PATH" -signature "$ARCHIVE_PATH.sig" "$ARCHIVE_PATH" >/dev/null
fi

BACKUP=""
if [[ -d "$INSTALL_ROOT/current" ]]; then
  BACKUP="$INSTALL_ROOT/backup.$(date +%s)"
  mv "$INSTALL_ROOT/current" "$BACKUP"
fi

STAGE="$TMP_DIR/stage"
mkdir -p "$STAGE"
tar -xzf "$ARCHIVE_PATH" -C "$STAGE"

NEW_ROOT="$STAGE"
if [[ -d "$STAGE/TezzNative-language" ]]; then
  NEW_ROOT="$STAGE/TezzNative-language"
fi

if [[ ! -x "$NEW_ROOT/tezz" ]]; then
  if [[ -n "$BACKUP" && -d "$BACKUP" ]]; then
    mv "$BACKUP" "$INSTALL_ROOT/current"
  fi
  echo "install.sh: archive missing tezz launcher"
  exit 1
fi

rm -rf "$INSTALL_ROOT/current"
cp -a "$NEW_ROOT" "$INSTALL_ROOT/current"
ln -sf "$INSTALL_ROOT/current/tezz" "$BIN_DIR/tezz"

PROFILE_FILE="$HOME/.profile"
PATH_LINE='export PATH="$HOME/.local/bin:$PATH"'
if [[ -f "$PROFILE_FILE" ]]; then
  grep -q 'HOME/.local/bin' "$PROFILE_FILE" || printf '\n%s\n' "$PATH_LINE" >> "$PROFILE_FILE"
fi

echo "tezz install complete: $INSTALL_ROOT/current"
