#!/usr/bin/env bash
# Bundles Homebrew FFmpeg libraries into a macOS .app so it can run on a
# machine without Homebrew. Qt frameworks are handled by macdeployqt first.
set -euo pipefail

APP="${1:?usage: bundle_macos.sh <App.app>}"
BIN="$APP/Contents/MacOS/DigitalImageAnalyzer"
FW="$APP/Contents/Frameworks"

[ -x "$BIN" ] || { echo "missing $BIN" >&2; exit 1; }
mkdir -p "$FW"

MACDEPLOYQT="$(command -v macdeployqt 2>/dev/null || true)"
if [ -z "$MACDEPLOYQT" ] && command -v brew >/dev/null 2>&1; then
  MACDEPLOYQT="$(brew --prefix qt 2>/dev/null)/bin/macdeployqt"
fi
if [ -n "$MACDEPLOYQT" ] && [ -x "$MACDEPLOYQT" ]; then
  "$MACDEPLOYQT" "$APP" -always-overwrite >/dev/null 2>&1 || true
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

# Report the minimum macOS version required by the bundled Mach-O files, and
# optionally fail when it exceeds DIA_MIN_MACOS (useful in CI).
min_os_of() {
    otool -l "$1" 2>/dev/null | awk '/minos/ {print $2; exit}'
}

max_version() {
    awk 'NR == 1 { split($0, m, "."); next }
         {
             split($0, v, ".")
             for (i = 1; i <= 3; i++) {
                 if (v[i] + 0 > m[i] + 0) { for (j = 1; j <= 3; j++) m[j] = v[j]; break }
                 if (v[i] + 0 < m[i] + 0) break
             }
         }
         END { printf "%d.%d.%d\n", m[1], m[2], m[3] }' | tail -1
}

versions=""
while IFS= read -r file; do
    [ -n "$file" ] || continue
    v="$(min_os_of "$file")"
    [ -n "$v" ] || continue
    printf '  %-40s %s\n' "$(basename "$file")" "$v"
    versions="$versions$v\n"
done < <(printf '%s\n' "$BIN"; find "$FW" -type f \( -name '*.dylib' -o -name 'Qt*' \) 2>/dev/null)

highest="$(printf "%b" "$versions" | grep -v '^$' | max_version)"
echo "minimum macOS supported by the bundle: ${highest:-unknown}"

if [ -n "${DIA_MIN_MACOS:-}" ] && [ -n "$highest" ]; then
    target="$(printf '%s\n' "$DIA_MIN_MACOS" | max_version)"
    if [ "$(printf '%s\n%s\n' "$DIA_MIN_MACOS" "$highest" | max_version)" != "$target" ]; then
        echo "warning: bundle requires macOS $highest, target is $DIA_MIN_MACOS" >&2
        if [ "${DIA_STRICT_MINOS:-0}" = "1" ]; then
            exit 1
        fi
    fi
fi
