#!/bin/bash
# AFL++ file-fuzzing -- xpdf 4.02 pdftops -- seeded with CVE-2020-35376 poc.pdf.
#
# Fuzz binary: build-afl/xpdf/pdftops  (afl-clang-fast instrumentation, NO ASan).
#   The CVE is an unbounded-recursion stack overflow -> the process SIGSEGVs, so
#   AFL catches it as a crash even without ASan. (conda clang-18 has no ASan
#   runtime; a separate gcc-11 ASan build under build-asan/ is used for triage.)
# poc.pdf crashes on the first run: AFL logs "results in a crash, skipping" and
#   keeps fuzzing from the 40 valid companion seeds.
#
# Usage: ./run.sh [seconds]   (default 86400 = 24h; pass e.g. 600 for a sanity run)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
RESEARCH="${RESEARCH:-$REPO_ROOT/research}"
EXP="$SCRIPT_DIR"
BUILD="${XPDF402_AFL_BUILD:-$RESEARCH/thesis/xpdf-4.02/build-afl}"
AFL="${AFL_BIN:-$HOME/.local/bin/afl-fuzz}"
AFL_WHATSUP="${AFL_WHATSUP:-$HOME/.local/bin/afl-whatsup}"
PB_PREFIX="${PB_PREFIX:-$HOME/miniconda3}"
TARGET="$BUILD/xpdf/pdftops"
SEEDS="$EXP/seeds"
OUT="$EXP/out"
DURATION="${1:-86400}"

[ -x "$TARGET" ] || { echo "ERROR: fuzz target not built ($TARGET)"; exit 1; }
[ -d "$SEEDS" ]  || { echo "ERROR: seeds dir missing ($SEEDS)"; exit 1; }

# afl-fuzz may link libpython from the selected protobuf/conda prefix.
export LD_LIBRARY_PATH="$PB_PREFIX/lib:${LD_LIBRARY_PATH:-}"
# poc.pdf crashes at dry-run -> skip it, keep the valid seeds.
export AFL_SKIP_CRASHES=1
# core_pattern is piped to apport and we have no root to change it.
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_SKIP_CPUFREQ=1
export AFL_AUTORESUME=1
# (optional) capture poc.pdf's crash into out/*/crashes at startup:
# export AFL_CRASHING_SEEDS_AS_NEW_CRASH=1

echo "Starting AFL++ (xpdf 4.02 pdftops) at $(date)"
echo "  target:   $TARGET"
echo "  seeds:    $SEEDS ($(ls "$SEEDS" | wc -l) files)"
echo "  out:      $OUT"
echo "  duration: ${DURATION}s"

nohup "$AFL" -V "$DURATION" -m none -t 2000+ -i "$SEEDS" -o "$OUT" -- \
    "$TARGET" @@ /dev/null > "$EXP/afl.log" 2>&1 &
echo $! > "$EXP/afl.pid"
echo "afl-fuzz PID $(cat "$EXP/afl.pid")"
echo "Monitor: tail -f $EXP/afl.log   |   LD_LIBRARY_PATH=$PB_PREFIX/lib $AFL_WHATSUP $OUT"
echo "Stop:    kill \$(cat $EXP/afl.pid)"
