#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p build bin

CC_BIN="${CC:-}"
if [[ -z "$CC_BIN" ]]; then
  for candidate in clang gcc cc; do
    if command -v "$candidate" >/dev/null 2>&1; then
      CC_BIN="$candidate"
      break
    fi
  done
fi

if [[ -z "$CC_BIN" ]]; then
  echo "build_core_strict: no C compiler found (set CC or install clang/gcc)"
  exit 1
fi

OPENSSL_CFLAGS=()
OPENSSL_LIBS=()
SDL_CFLAGS=()
SDL_LIBS=()
if [[ "${TN_NO_OPENSSL:-0}" == "1" ]]; then
  echo "build_core_strict: OpenSSL disabled by TN_NO_OPENSSL=1; TLS runtime will use explicit unsupported stubs"
elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists openssl; then
  while IFS= read -r flag; do
    [[ -n "$flag" ]] && OPENSSL_CFLAGS+=("$flag")
  done < <(pkg-config --cflags openssl | tr ' ' '\n')
  openssl_lib_mode=(--libs)
  if [[ "${TN_STATIC:-0}" == "1" ]]; then
    openssl_lib_mode=(--static --libs)
  fi
  while IFS= read -r flag; do
    [[ -n "$flag" ]] && OPENSSL_LIBS+=("$flag")
  done < <(pkg-config "${openssl_lib_mode[@]}" openssl | tr ' ' '\n')
else
  for prefix in /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl@3; do
    if [[ -d "$prefix/include" && -d "$prefix/lib" ]]; then
      OPENSSL_CFLAGS+=("-I$prefix/include")
      OPENSSL_LIBS+=("-L$prefix/lib" -lssl -lcrypto)
      break
    fi
  done
fi

if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2; then
  while IFS= read -r flag; do
    [[ -n "$flag" ]] && SDL_CFLAGS+=("$flag")
  done < <(pkg-config --cflags sdl2 | tr ' ' '\n')
  while IFS= read -r flag; do
    [[ -n "$flag" ]] && SDL_LIBS+=("$flag")
  done < <(pkg-config --libs sdl2 | tr ' ' '\n')
fi

CFLAGS=(-Iinclude -std=c11 -O2 -Wall -Wextra -Werror)
# Keep the strict build meaningful without letting platform stubs or GCC
# conservative analyzer warnings block Linux compiler refreshes.
CFLAGS+=(-Wno-error=unused-parameter -Wno-error=unused-function -Wno-error=unused-variable)
if "$CC_BIN" --version 2>/dev/null | grep -qi 'gcc'; then
  CFLAGS+=(-Wno-stringop-overflow -Wno-alloc-size-larger-than)
fi
LDFLAGS=()
if [[ ${#SDL_LIBS[@]} -gt 0 ]]; then
  CFLAGS+=(-DTN_ENABLE_HOST_GUI=1)
fi
if [[ ${#OPENSSL_LIBS[@]} -gt 0 ]]; then
  CFLAGS+=(-DTN_TLS_OPENSSL=1)
else
  echo "build_core_strict: OpenSSL development package not found; TLS runtime will use explicit unsupported stubs"
fi
case "$(uname -s)" in
  Linux)
    CFLAGS+=(-D_GNU_SOURCE -pthread)
    LDFLAGS+=(-pthread)
    if [[ "${TN_STATIC:-0}" == "1" ]]; then
      LDFLAGS+=(-static)
    fi
    ;;
  Darwin)
    CFLAGS+=(-D_DARWIN_C_SOURCE)
    # ld64 generates a random UUID by default; disable it for reproducible builds.
    LDFLAGS+=(-Wl,-no_uuid)
    ;;
  *)
    echo "build_core_strict: unsupported host OS for this script"
    exit 1
    ;;
esac

mapfile -t SRCS < <(printf '%s\n' src/*.c)
if [[ ${#SRCS[@]} -eq 0 ]]; then
  echo "build_core_strict: no source files found"
  exit 1
fi

OUT="build/tezzc"
BIN_CANON="bin/tezzc"
BIN_ALIAS="bin/tezzc-linux-x64"
rm -f "$OUT" "$BIN_CANON" "$BIN_ALIAS"
"$CC_BIN" "${CFLAGS[@]}" "${OPENSSL_CFLAGS[@]}" "${SDL_CFLAGS[@]}" "${SRCS[@]}" -o "$OUT" "${LDFLAGS[@]}" "${OPENSSL_LIBS[@]}" "${SDL_LIBS[@]}"
cp "$OUT" "$BIN_CANON"
cp "$OUT" "$BIN_ALIAS"
chmod +x "$OUT" "$BIN_CANON" "$BIN_ALIAS"
if [[ ${#SDL_LIBS[@]} -gt 0 ]]; then
  echo "build_core_strict: built $OUT and synced bin/tezzc (host GUI backend enabled)"
else
  echo "build_core_strict: built $OUT and synced bin/tezzc"
fi
