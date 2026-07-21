#!/bin/bash
# AFL++ structure-aware fuzzing -- xpdf 4.02 -- CFF/Type1C (FoFiType1C) via /FontFile3.
#
# Target is pdftops (PSOutputDev) -- the ONLY path that runs FoFiType1C::cvtGlyph
# (Type1C->Type1 conversion). The Splash render harness does NOT reach it (FreeType
# handles embedded CFF). See README.md.
#
# Custom mutator serializes a PdfDocument proto (with a structured pdf_cff.CffFont)
# into a PDF; AFL writes it to @@ and pdftops parses it. AFL_POST_PROCESS_KEEP_ORIGINAL
# keeps the proto in the queue so crashes can be re-serialised for triage.
#
# Duration: pass seconds as $1 (default 86400 = 24h). Small value for a sanity run.
set -uo pipefail

ROOT=/home/hoangnh8/master-thesis-research-logs/research
PROTO="$ROOT/schema/pdf-proto"
AFL=/home/hoangnh8/.local/bin/afl-fuzz
PDFTOPS="$ROOT/thesis/xpdf-4.02/build-afl-asan/xpdf/pdftops"   # afl-clang-fast + ASan (build: see README §1)
MUTATOR="$PROTO/afl/afl_pdf_mutator.so"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SEEDS="$SCRIPT_DIR/seeds"
OUT="$SCRIPT_DIR/out"
DURATION="${1:-86400}"

# --- preconditions (fail loudly until the target is built and seeds exist) ---
if [ ! -x "$PDFTOPS" ]; then
    echo "ERROR: AFL+ASan pdftops not built ($PDFTOPS)"; echo "  -> see README.md §1 (cmake with afl-clang-fast + -fsanitize=address)"; exit 1
fi
# grep -c (not grep -q): under `set -o pipefail`, grep -q closes the pipe early and nm
# racily exits with SIGPIPE, tripping a false "not instrumented". grep -c drains nm fully.
if [ "$(nm "$PDFTOPS" 2>/dev/null | grep -c __afl)" -eq 0 ]; then
    echo "ERROR: $PDFTOPS is NOT AFL-instrumented (no __afl symbols) -- coverage-blind. Rebuild with afl-clang-fast."; exit 1
fi
[ -f "$MUTATOR" ] || { echo "ERROR: mutator .so missing ($MUTATOR) -- run afl/build_mutator.sh"; exit 1; }
[ -d "$SEEDS" ] && [ -n "$(ls -A "$SEEDS" 2>/dev/null)" ] || { echo "ERROR: seeds dir empty ($SEEDS) -- run ./gen_seeds.sh"; exit 1; }

# AFL++ tools link conda's libpython; the mutator .so links conda's libprotobuf.
export LD_LIBRARY_PATH=/home/hoangnh8/miniconda3/lib:${LD_LIBRARY_PATH:-}
export AFL_CUSTOM_MUTATOR_LIBRARY="$MUTATOR"
export AFL_CUSTOM_MUTATOR_ONLY=1                 # structure-aware only; no byte-level havoc
export AFL_POST_PROCESS_KEEP_ORIGINAL=1          # keep proto in queue -> triage via proto2pdf_t3
export AFL_SKIP_CPUFREQ=1
export AFL_NO_AFFINITY=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_AUTORESUME=1
# ASan on the TARGET: abort so AFL catches it; leaks off (xpdf leaks); no symbolize for speed.
export ASAN_OPTIONS="abort_on_error=1:detect_leaks=0:symbolize=0:allocator_may_return_null=1:handle_abort=2:detect_odr_violation=0"

echo "Starting AFL++ (xpdf 4.02, CFF/Type1C via pdftops) at $(date)"
echo "  target:   $PDFTOPS @@ /dev/null"
echo "  mutator:  $MUTATOR"
echo "  seeds:    $SEEDS ($(ls "$SEEDS" | wc -l) files)"
echo "  out:      $OUT   duration: ${DURATION}s"
echo ""

# @@ mode: AFL writes the mutator's PDF output to @@; pdftops converts to PS (-> /dev/null).
# -t 2000+ : skip pathologically slow inputs (deep recursion) rather than counting them as hangs.
nohup "$AFL" -V "$DURATION" -m none -t 2000+ -i "$SEEDS" -o "$OUT" -- "$PDFTOPS" @@ /dev/null \
    > "$SCRIPT_DIR/afl.log" 2>&1 &
PID=$!
echo "$PID" > "$SCRIPT_DIR/afl.pid"
echo "afl-fuzz PID: $PID"
echo "Monitor:  tail -f $SCRIPT_DIR/afl.log   |   LD_LIBRARY_PATH=/home/hoangnh8/miniconda3/lib /home/hoangnh8/.local/bin/afl-whatsup $OUT"
echo "Crashes:  ls $OUT/default/crashes/   (re-serialise proto -> proto2pdf_t3 -> pdftops for triage; see README §5)"
echo "Stop:     kill \$(cat $SCRIPT_DIR/afl.pid)"
