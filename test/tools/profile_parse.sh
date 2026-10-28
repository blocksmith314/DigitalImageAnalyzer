#!/usr/bin/env bash
# Profile the parser benchmark with macOS `sample` and print the hottest
# functions. Developer tool, not part of the automated test suite.
#
#   profile_parse.sh <file> [options]
#
# Options:
#   -m MODE      raw-h264 | raw-h265 | buffer | mp4-index | mp4-avcc (default raw-h264)
#   -s SECONDS   sampling duration (default 6)
#   -r REPEATS   benchmark repetitions, keep the process alive long enough (default 60)
#   -n TOP       number of hot functions to print (default 12)
#   -o FILE      sample output file (default /tmp/dia_profile_<pid>.txt)
#   -b BINARY    bench_parse binary (default <repo>/build/test/tools/bench_parse)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

MODE="raw-h264"
SECONDS_TO_SAMPLE=6
REPEATS=60
TOP=12
OUT=""
BENCH="${BENCH:-$ROOT_DIR/build/test/tools/bench_parse}"

usage() {
    sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

FILE=""
while [ $# -gt 0 ]; do
    case "$1" in
        -m) MODE="$2"; shift 2 ;;
        -s) SECONDS_TO_SAMPLE="$2"; shift 2 ;;
        -r) REPEATS="$2"; shift 2 ;;
        -n) TOP="$2"; shift 2 ;;
        -o) OUT="$2"; shift 2 ;;
        -b) BENCH="$2"; shift 2 ;;
        -h|--help) usage 0 ;;
        -*) echo "unknown option: $1" >&2; usage 2 ;;
        *) FILE="$1"; shift ;;
    esac
done

if [ -z "$FILE" ]; then
    echo "error: missing input file" >&2
    usage 2
fi
if [ ! -f "$FILE" ]; then
    echo "error: file not found: $FILE" >&2
    exit 1
fi
if [ ! -x "$BENCH" ]; then
    cat >&2 <<MSG
error: benchmark binary not found: $BENCH
build it first:
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
  cmake --build build --target bench_parse
MSG
    exit 1
fi

if ! command -v sample >/dev/null 2>&1; then
    cat >&2 <<'MSG'
error: `sample` is a macOS tool and was not found.
On Linux use: perf record -g -- ./build/test/tools/bench_parse raw-h264 <file> --repeat 20
MSG
    exit 1
fi

[ -n "$OUT" ] || OUT="/tmp/dia_profile_$$.txt"

echo "bench: $BENCH"
echo "mode=$MODE file=$FILE repeats=$REPEATS sample=${SECONDS_TO_SAMPLE}s"

LOG="/tmp/dia_bench_$$.log"
"$BENCH" "$MODE" "$FILE" --repeat "$REPEATS" >"$LOG" 2>&1 &
BENCH_PID=$!

sleep 1.5
if ! kill -0 "$BENCH_PID" 2>/dev/null; then
    echo "error: benchmark exited before sampling started" >&2
    tail -5 "$LOG" >&2
    exit 1
fi

echo "sampling pid $BENCH_PID ..."
sample "$BENCH_PID" "$SECONDS_TO_SAMPLE" -file "$OUT" >/dev/null 2>&1 || true

if kill -0 "$BENCH_PID" 2>/dev/null; then
    kill "$BENCH_PID" 2>/dev/null || true
fi
wait "$BENCH_PID" 2>/dev/null || true

echo
echo "=== benchmark timings ==="
grep -E "^run |^mode=|^median:|^throughput:" "$LOG" | tail -4 || true

echo
echo "=== hot functions (top $TOP) ==="
if grep -q "Sort by top of stack" "$OUT" 2>/dev/null; then
    sed -n '/Sort by top of stack/,$p' "$OUT" | sed -n "2,$((TOP + 1))p"
else
    echo "no samples captured (process too short? increase -r)" >&2
fi

echo
echo "raw sample: $OUT"
echo "bench log:  $LOG"
