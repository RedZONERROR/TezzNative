#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${TEZZ_INSTALL_BASE:-https://tezznative.org/download}"
PORTAL_URL="${TEZZ_PORTAL_BASE:-https://tezznative.org}"
PORTAL_URL="${PORTAL_URL%/}"
MODE="${1:-${TEZZ_INSTALL_MODE:-install}}"
MODE="$(printf '%s' "$MODE" | tr '[:upper:]' '[:lower:]')"

INSTALL_SCOPE="${TEZZ_INSTALL_SCOPE:-user}"
INSTALL_SCOPE="$(printf '%s' "$INSTALL_SCOPE" | tr '[:upper:]' '[:lower:]')"
if [[ "$INSTALL_SCOPE" != "user" && "$INSTALL_SCOPE" != "system" ]]; then
  INSTALL_SCOPE="user"
fi

DEST_DEFAULT="$HOME/TezzNative"
if [[ "$INSTALL_SCOPE" == "system" ]]; then
  DEST_DEFAULT="/opt/TezzNative"
fi
DEST="${TEZZ_INSTALL_DIR:-$DEST_DEFAULT}"
SDK_DIR="$DEST/sdk"
BIN_DIR="${TEZZ_INSTALL_BIN:-$HOME/.local/bin}"
TMP_ARCHIVE="${TMPDIR:-/tmp}/tezznative-sdk-linux-$RANDOM-$RANDOM.tar.gz"
PATH_MODE="${TEZZ_INSTALL_AUTO_PATH:-user}"
CACHE_BUST="${TEZZ_INSTALL_CACHE_BUST:-$(date +%s)-$$}"

http_get() {
  local url="$1"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$url"
    return $?
  fi
  if command -v wget >/dev/null 2>&1; then
    wget -qO- "$url"
    return $?
  fi
  return 1
}

download_file() {
  local url="$1"
  local out="$2"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$url" -o "$out"
    return $?
  fi
  if command -v wget >/dev/null 2>&1; then
    wget -qO "$out" "$url"
    return $?
  fi
  return 1
}

hash_file() {
  local path="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$path" | awk '{print toupper($1)}'
    return 0
  fi
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$path" | awk '{print toupper($1)}'
    return 0
  fi
  return 1
}

verify_archive_checksum() {
  local archive="$1"
  local file_name="$2"
  local sha_file="${TMPDIR:-/tmp}/${file_name}.$RANDOM.$RANDOM.sha256"
  if ! download_file "$BASE_URL/${file_name}.sha256?nocache=$CACHE_BUST" "$sha_file"; then
    echo "install failed: could not download checksum for $file_name"
    rm -f "$sha_file"
    return 1
  fi
  local expected
  expected="$(awk '{print toupper($1)}' "$sha_file" | head -n1)"
  rm -f "$sha_file"
  if ! printf '%s' "$expected" | grep -Eq '^[0-9A-F]{64}$'; then
    echo "install failed: invalid checksum file for $file_name"
    return 1
  fi
  local actual
  actual="$(hash_file "$archive")"
  if [[ "$actual" != "$expected" ]]; then
    echo "install failed: checksum mismatch for $file_name"
    echo "  expected: $expected"
    echo "  actual:   $actual"
    return 1
  fi
  echo "Verified SHA-256: $actual"
}

json_version() {
  sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n1
}

get_remote_version() {
  local local_version
  local_version="$(get_local_version || true)"
  local api
  api="$PORTAL_URL/api/update_check.php?platform=linux-x64&version=$local_version&mode=$MODE"
  http_get "$api" | json_version && return 0
  http_get "$BASE_URL/sdk/version.json" | json_version || true
}

send_install_event() {
  local status="$1"
  local message="$2"
  local install_id="${HOSTNAME:-tezznative-cli}"
  local version="${remote_version:-unknown}"
  if command -v curl >/dev/null 2>&1; then
    curl -fsS -X POST "$PORTAL_URL/api/install_event.php" \
      -d "platform=linux-x64" \
      -d "version=$version" \
      -d "status=$status" \
      -d "install_id=$install_id" \
      -d "message=$message" >/dev/null 2>&1 || true
  fi
}

get_local_version() {
  if [[ -f "$SDK_DIR/version.json" ]]; then
    cat "$SDK_DIR/version.json" | json_version
    return 0
  fi
  if [[ -f "$SDK_DIR/tezz.mod" ]]; then
    sed -n 's/^[[:space:]]*version[[:space:]]*=[[:space:]]*//p' "$SDK_DIR/tezz.mod" | head -n1
    return 0
  fi
  echo ""
}

require_tools() {
  if ! command -v tar >/dev/null 2>&1; then
    echo "install failed: tar is required"
    exit 1
  fi
  if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
    echo "install failed: curl or wget is required"
    exit 1
  fi
}

remove_install() {
  rm -rf "$DEST"
  rm -f "$BIN_DIR/tezz" "$BIN_DIR/tezzc"
}

normalize_sdk_layout() {
  mkdir -p "$SDK_DIR/bin"
  if [[ -x "$SDK_DIR/build/tezzc" && ! -x "$SDK_DIR/bin/tezzc-linux-x64" ]]; then
    cp "$SDK_DIR/build/tezzc" "$SDK_DIR/bin/tezzc-linux-x64"
  fi
  if [[ -x "$SDK_DIR/tezzc" && ! -x "$SDK_DIR/bin/tezzc-linux-x64" ]]; then
    cp "$SDK_DIR/tezzc" "$SDK_DIR/bin/tezzc-linux-x64"
  fi
  chmod +x "$SDK_DIR/tezz" 2>/dev/null || true
  chmod +x "$SDK_DIR/bin/tezzc-linux-x64" 2>/dev/null || true
}

assert_sdk_integrity() {
  local missing=0
  local required=(
    "$SDK_DIR/tezz"
    "$SDK_DIR/tezz.mod"
    "$SDK_DIR/tools/tezz.tn"
    "$SDK_DIR/tools/probes/tls_connect_ex_probe.tn"
    "$SDK_DIR/lib/std.tn"
    "$SDK_DIR/lib/io.tn"
    "$SDK_DIR/bin/tezzc-linux-x64"
  )
  for item in "${required[@]}"; do
    if [[ ! -e "$item" ]]; then
      echo "missing SDK file: $item"
      missing=1
    fi
  done
  if [[ $missing -ne 0 ]]; then
    return 1
  fi
  return 0
}

write_user_profile() {
  local profile="$HOME/.profile"
  local path_line="export PATH=\"$BIN_DIR:\$PATH\""
  local sdk_line="export TEZZ_SDK_ROOT=\"$SDK_DIR\""
  mkdir -p "$(dirname "$profile")"
  touch "$profile"
  if ! grep -Fq "$path_line" "$profile"; then
    printf '\n%s\n' "$path_line" >> "$profile"
  fi
  if grep -Fq 'export TEZZ_SDK_ROOT=' "$profile"; then
    if command -v sed >/dev/null 2>&1; then
      sed -i "s|^export TEZZ_SDK_ROOT=.*$|$sdk_line|" "$profile" || true
    fi
  else
    printf '%s\n' "$sdk_line" >> "$profile"
  fi
}

write_system_profile() {
  local file="/etc/profile.d/tezznative.sh"
  local payload="export PATH=\"$BIN_DIR:\$PATH\"\nexport TEZZ_SDK_ROOT=\"$SDK_DIR\""
  if command -v sudo >/dev/null 2>&1; then
    printf '%b\n' "$payload" | sudo tee "$file" >/dev/null
  else
    printf '%b\n' "$payload" > "$file"
  fi
}

configure_path() {
  local mode="$PATH_MODE"
  case "$mode" in
    user|"") write_user_profile ;;
    system)
      write_system_profile || true
      ;;
    none)
      ;;
    *)
      write_user_profile
      ;;
  esac
}

install_shims() {
  mkdir -p "$BIN_DIR"
  ln -sf "$SDK_DIR/tezz" "$BIN_DIR/tezz"
  if [[ -x "$SDK_DIR/bin/tezzc-linux-x64" ]]; then
    ln -sf "$SDK_DIR/bin/tezzc-linux-x64" "$BIN_DIR/tezzc"
  fi
}

smoke_test_install() {
  local tezz_bin="$BIN_DIR/tezz"
  if [[ ! -x "$tezz_bin" ]]; then
    tezz_bin="$SDK_DIR/tezz"
  fi
  "$tezz_bin" --version >/dev/null

  local probe="${TMPDIR:-/tmp}/tezznative_smoke_$RANDOM.tn"
  cat > "$probe" <<'TN'
fn main() -> int:
  say "tezz-smoke-ok"
  ret 0
TN
  local out
  out="$("$tezz_bin" "$probe" 2>/dev/null || true)"
  rm -f "$probe"
  if [[ "$out" != *"tezz-smoke-ok"* ]]; then
    echo "install failed: post-install smoke test failed"
    return 1
  fi
}

install_payload() {
  local remote_version="$1"
  require_tools

  echo "Downloading TezzNative SDK..."
  download_file "$BASE_URL/tezznative-sdk-linux.tar.gz?nocache=$CACHE_BUST" "$TMP_ARCHIVE"
  verify_archive_checksum "$TMP_ARCHIVE" "tezznative-sdk-linux.tar.gz"

  if [[ "$INSTALL_SCOPE" == "system" ]]; then
    if [[ ! -w "$(dirname "$DEST")" ]]; then
      if ! command -v sudo >/dev/null 2>&1; then
        echo "install failed: system scope requires sudo"
        return 1
      fi
      sudo rm -rf "$DEST"
      sudo mkdir -p "$DEST"
      sudo tar -xzf "$TMP_ARCHIVE" -C "$DEST"
      sudo chown -R "$USER":"$USER" "$DEST"
    else
      rm -rf "$DEST"
      mkdir -p "$DEST"
      tar -xzf "$TMP_ARCHIVE" -C "$DEST"
    fi
  else
    rm -rf "$DEST"
    mkdir -p "$DEST"
    tar -xzf "$TMP_ARCHIVE" -C "$DEST"
  fi
  rm -f "$TMP_ARCHIVE"

  normalize_sdk_layout
  assert_sdk_integrity
  install_shims
  configure_path
  smoke_test_install

  echo ""
  echo "TezzNative installed."
  echo "Install root: $DEST"
  echo "SDK root: $SDK_DIR"
  echo "Bin dir: $BIN_DIR"
  echo ""
  echo "Next steps:"
  echo "  source ~/.profile"
  echo "  tezz --version"
  echo "  tezz my_script.tn"
  echo ""
  echo "Executable script mode (Python-like):"
  echo "  1. Add shebang: #!/usr/bin/env tezz"
  echo "  2. chmod +x your_script.tn"
  echo "  3. ./your_script.tn"

  if [[ -n "$remote_version" ]]; then
    echo "Installed version: $remote_version"
  fi
}

remote_version="$(get_remote_version)"
local_version="$(get_local_version)"

case "$MODE" in
  check)
    echo "Local version:  ${local_version:-not-installed}"
    echo "Remote version: ${remote_version:-unknown}"
    if [[ -n "$remote_version" && -n "$local_version" && "$remote_version" == "$local_version" ]]; then
      echo "Status: up-to-date"
    elif [[ -n "$local_version" ]]; then
      echo "Status: update-available"
    else
      echo "Status: not-installed"
    fi
    send_install_event "check" "local=$local_version remote=$remote_version"
    ;;
  uninstall)
    remove_install
    echo "TezzNative removed from $DEST"
    send_install_event "uninstall" "removed"
    ;;
  reinstall)
    remove_install
    install_payload "$remote_version"
    send_install_event "reinstall" "completed"
    ;;
  update)
    if [[ -z "$local_version" ]]; then
      install_payload "$remote_version"
      send_install_event "install" "installed from update mode"
    elif [[ -n "$remote_version" && "$local_version" == "$remote_version" ]]; then
      echo "TezzNative is already up-to-date ($local_version)."
      send_install_event "check" "already up-to-date"
    else
      install_payload "$remote_version"
      send_install_event "update" "updated"
    fi
    ;;
  install|"")
    install_payload "$remote_version"
    send_install_event "install" "completed"
    ;;
  *)
    echo "Unknown mode: $MODE"
    echo "Usage: install.sh [install|update|reinstall|uninstall|check]"
    exit 1
    ;;
esac
