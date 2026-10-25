#!/usr/bin/env bash
# Bundles Homebrew FFmpeg libraries into a macOS .app so it can run on a
# machine without Homebrew. Qt frameworks are handled by macdeployqt first.
set -euo pipefail

APP="${1:?usage: bundle_macos.sh <App.app>}"
BIN="$APP/Contents/MacOS/DigitalImageAnalyzer"
FW="$APP/Contents/Frameworks"

[ -x "$BIN" ] || { echo "missing $BIN" >&2; exit 1; }
mkdir -p "$FW"

if command -v macdeployqt >/dev/null 2>&1; then
  macdeployqt "$APP" -always-overwrite >/dev/null 2>&1 || true
fi

copy_deps() {
  local target="$1"
  local lib base dest
  while IFS= read -r lib; do
    case "$lib" in
      /usr/lib/*|/System/*|@*) continue ;;
    esac
    [ -f "$lib" ] || continue
    base="$(basename "$lib")"
    dest="$FW/$base"
    if [ ! -f "$dest" ]; then
      cp "$lib" "$dest"
      chmod u+w "$dest"
      install_name_tool -id "@executable_path/../Frameworks/$base" "$dest" 2>/dev/null || true
      copy_deps "$dest"
    fi
    install_name_tool -change "$lib" "@executable_path/../Frameworks/$base" "$target" 2>/dev/null || true
  done < <(otool -L "$target" | awk 'NR>1 {print $1}')
}

copy_deps "$BIN"
install_name_tool -add_rpath "@executable_path/../Frameworks" "$BIN" 2>/dev/null || true

codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true
echo "bundled dylibs: $(find "$FW" -maxdepth 1 -name '*.dylib' | wc -l | tr -d ' ')"
