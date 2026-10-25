#!/usr/bin/env bash
# Collects the executable, the Qt runtime and the MinGW/FFmpeg DLLs into a
# self-contained folder (run inside an MSYS2 MINGW64 shell).
set -euo pipefail

EXE="${1:?usage: bundle_windows.sh <exe> <dist>}"
DIST="${2:?}"

mkdir -p "$DIST"
cp "$EXE" "$DIST/"
BASE="$(basename "$EXE")"

if command -v windeployqt >/dev/null 2>&1; then
  windeployqt --release --no-translations "$DIST/$BASE" >/dev/null
fi

copy_deps() {
  local target="$1"
  local dll base
  while IFS= read -r dll; do
    base="$(basename "$dll")"
    if [ ! -f "$DIST/$base" ]; then
      cp "$dll" "$DIST/"
      copy_deps "$DIST/$base"
    fi
  done < <(ldd "$target" 2>/dev/null | awk '{print $3}' | grep -E '^/mingw64/bin/' | sort -u)
}

copy_deps "$DIST/$BASE"
echo "bundled dlls: $(find "$DIST" -name '*.dll' | wc -l | tr -d ' ')"
