#!/usr/bin/env bash
# Run clang-tidy on changed files (default), explicit files, or --all.
#
#   tidy.sh [--all] [--fix] [file ...]
#
# Requires build/compile_commands.json (CMake exports it automatically).
# Set CLANG_TIDY to override the binary.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"

TIDY="${CLANG_TIDY:-}"
if [ -z "$TIDY" ]; then
    for candidate in /opt/homebrew/opt/llvm/bin/clang-tidy "$(command -v clang-tidy 2>/dev/null || true)"; do
        if [ -n "$candidate" ] && [ -x "$candidate" ]; then
            TIDY="$candidate"
            break
        fi
    done
fi
if [ -z "$TIDY" ]; then
    echo "error: clang-tidy not found; install LLVM (brew install llvm) or set CLANG_TIDY" >&2
    exit 1
fi
if [ ! -f "$BUILD_DIR/compile_commands.json" ]; then
    echo "error: $BUILD_DIR/compile_commands.json missing; configure the project first" >&2
    exit 1
fi

ALL=0
FIX=0
FILES=()
for arg in "$@"; do
    case "$arg" in
        --all) ALL=1 ;;
        --fix) FIX=1 ;;
        -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "unknown option: $arg" >&2; exit 2 ;;
        *) FILES+=("$arg") ;;
    esac
done

cd "$ROOT_DIR"

if [ "$ALL" = "1" ]; then
    while IFS= read -r file; do FILES+=("$file"); done < <(git ls-files '*.cpp' '*.h')
elif [ ${#FILES[@]} -eq 0 ]; then
    while IFS= read -r file; do FILES+=("$file"); done < <(
        { git diff --name-only --cached; git diff --name-only; } | sort -u
    )
    if [ ${#FILES[@]} -eq 0 ]; then
        while IFS= read -r file; do FILES+=("$file"); done < <(
            git diff --name-only origin/main...HEAD 2>/dev/null || true
        )
    fi
fi

# Keep only C++ sources we track, and never touch third_party/build output.
SELECTED=()
for file in "${FILES[@]}"; do
    case "$file" in
        third_party/*|build/*) continue ;;
        *.cpp|*.h) SELECTED+=("$file") ;;
    esac
done
if [ ${#SELECTED[@]} -eq 0 ]; then
    echo "no C++ files to check"
    exit 0
fi

echo "clang-tidy: $TIDY"
"$TIDY" --version | head -1
echo "checking ${#SELECTED[@]} file(s) with $BUILD_DIR/compile_commands.json"

ARGS=(-p "$BUILD_DIR")
[ "$FIX" = "1" ] && ARGS+=(--fix)
if "$TIDY" "${ARGS[@]}" "${SELECTED[@]}" 2>&1 | tee /tmp/dia_tidy.log; then
    :
fi
WARNINGS=$(grep -c "warning:" /tmp/dia_tidy.log || true)
echo
echo "warnings: $WARNINGS (full log: /tmp/dia_tidy.log)"
[ "$WARNINGS" -eq 0 ]
