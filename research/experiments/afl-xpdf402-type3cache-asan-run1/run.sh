#!/bin/bash
# AFL++ structure-aware fuzzing -- xpdf 4.02 -- Type 3 font-cache (CVE-2020-25725) -- SPLASH + ASan.
#
# This variant renders via SplashOutputDev (real rasterizer) so xpdf executes Type 3 char
# procs and reaches SplashOutputDev::endType3Char -- the heap-use-after-free of CVE-2020-25725.
# The target is built WITH AddressSanitizer, so the UAF becomes a catchable SIGABRT crash.
# (The non-Splash / non-ASan harness in ../afl-xpdf402-type3cache-run1 CANNOT reach this bug.)
#
# Requirements: afl-clang-fast backend (conda clang-18) must have its ASan runtime installed
# (conda install -c conda-forge compiler-rt). Harness built into build-afl-asan/ with
# -DAFL_SPLASH_ASAN=ON. See README.md.
#
# Duration: pass seconds as $1 (default 86400 = 24h). ASan + rasterization is SLOW -- expect
# far fewer exec/s than the non-ASan run.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
ROOT="${RESEARCH:-$REPO_ROOT/research}"
PROTO="${PDF_PROTO_DIR:-$ROOT/schema/pdf-proto}"
AFL="${AFL_BIN:-$HOME/.local/bin/afl-fuzz}"
AFL_WHATSUP="${AFL_WHATSUP:-$HOME/.local/bin/afl-whatsup}"
PB_PREFIX="${PB_PREFIX:-$HOME/miniconda3}"
HARNESS="$PROTO/build-afl-asan/afl_harness_xpdf"     # Splash + ASan build
MUTATOR="$PROTO/afl/afl_pdf_mutator.so"
SEEDS="$SCRIPT_DIR/seeds"
OUT="$SCRIPT_DIR/out"
DURATION="${1:-86400}"

[ -x "$HARNESS" ] || { echo "ERROR: Splash+ASan harness not built ($HARNESS) -- see README.md"; exit 1; }
[ -f "$MUTATOR" ] || { echo "ERROR: mutator .so missing ($MUTATOR)"; exit 1; }
[ -d "$SEEDS" ] && [ -n "$(ls -A "$SEEDS" 2>/dev/null)" ] || { echo "ERROR: seeds dir empty ($SEEDS)"; exit 1; }

export LD_LIBRARY_PATH="$PB_PREFIX/lib:${LD_LIBRARY_PATH:-}"
export AFL_CUSTOM_MUTATOR_LIBRARY="$MUTATOR"
export AFL_CUSTOM_MUTATOR_ONLY=1
export AFL_POST_PROCESS_KEEP_ORIGINAL=1          # keep proto in queue (see other run's README)
export AFL_SKIP_CPUFREQ=1
export AFL_NO_AFFINITY=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_AUTORESUME=1
# ASan on the TARGET: leaks off (xpdf leaks a lot), abort so AFL catches the crash, no symbolize
# for speed. If afl-fuzz complains at startup about ASAN_OPTIONS, follow its printed hint.
export ASAN_OPTIONS="abort_on_error=1:detect_leaks=0:symbolize=0:allocator_may_return_null=1:handle_abort=2:detect_odr_violation=0"

echo "Starting AFL++ (xpdf 4.02, Type 3 cache, SPLASH+ASan) at $(date)"
echo "  harness:  $HARNESS"
echo "  mutator:  $MUTATOR"
echo "  seeds:    $SEEDS ($(ls "$SEEDS" | wc -l) files)"
echo "  out:      $OUT"
echo "  duration: ${DURATION}s"
echo ""

# -t 5000+ : generous timeout so slow Splash+ASan renders of deep Type 3 caches aren't culled.
nohup "$AFL" -V "$DURATION" -m none -t 5000+ -i "$SEEDS" -o "$OUT" -- "$HARNESS" \
    > "$SCRIPT_DIR/afl.log" 2>&1 &
PID=$!
echo "$PID" > "$SCRIPT_DIR/afl.pid"
echo "afl-fuzz PID: $PID"
echo "Monitor:  tail -f $SCRIPT_DIR/afl.log"
echo "          LD_LIBRARY_PATH=$PB_PREFIX/lib $AFL_WHATSUP $OUT"
echo "Crashes:  ls $OUT/default/crashes/   (ASan report is in afl.log / re-run harness on the crash)"
echo "Stop:     kill \$(cat $SCRIPT_DIR/afl.pid)"
