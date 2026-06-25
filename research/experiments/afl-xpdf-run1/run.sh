#!/bin/bash
# AFL++ structure-aware fuzzing -- xpdf 4.06 -- custom mutator (libprotobuf-mutator).
#
# AFL inputs/queue/crashes are binary PdfDocument protos. The custom mutator .so does:
#   afl_custom_fuzz        -- LPM mutates the proto
#   afl_custom_post_process-- SerializePdf() turns the proto into the PDF the target reads
# The target harness (afl_harness_xpdf) renders the PDF through xpdf (instrumented).
#
# Same engine + same xpdf instrumentation as the AFL++ baseline -> directly comparable.
# Duration: pass seconds as $1 (default 86400 = 24h). Use a small value for a sanity run.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT=/home/hoangnh8/master-thesis-research-logs/research
AFL="$ROOT/AFLplusplus"
PROTO="$ROOT/pdf-proto-schema"
HARNESS="$PROTO/build-afl/afl_harness_xpdf"
MUTATOR="$PROTO/afl_pdf_mutator.so"
SEEDS="$SCRIPT_DIR/seeds"
OUT="$SCRIPT_DIR/out"
DURATION="${1:-86400}"

[ -x "$HARNESS" ] || { echo "ERROR: harness not built ($HARNESS) -- cmake --build build-afl"; exit 1; }
[ -f "$MUTATOR" ] || { echo "ERROR: mutator .so missing ($MUTATOR) -- run build_mutator.sh"; exit 1; }
[ -d "$SEEDS" ]   || { echo "ERROR: seeds dir missing ($SEEDS)"; exit 1; }

# AFL++ tools link conda's libpython; the mutator .so links conda's libprotobuf.
export LD_LIBRARY_PATH=/home/hoangnh8/miniconda3/lib:$LD_LIBRARY_PATH
# Structure-aware mutation only -- replace AFL's byte-level havoc/splice entirely.
export AFL_CUSTOM_MUTATOR_LIBRARY="$MUTATOR"
export AFL_CUSTOM_MUTATOR_ONLY=1
# CmpLog intentionally NOT enabled: byte offsets are meaningless after proto->PDF serialize.
export AFL_SKIP_CPUFREQ=1
export AFL_NO_AFFINITY=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_AUTORESUME=1

echo "Starting AFL++ (xpdf, custom mutator) at $(date)"
echo "  harness:  $HARNESS"
echo "  mutator:  $MUTATOR"
echo "  seeds:    $SEEDS ($(ls "$SEEDS" | wc -l) files)"
echo "  out:      $OUT"
echo "  duration: ${DURATION}s"
echo ""

nohup "$AFL/afl-fuzz" -V "$DURATION" -i "$SEEDS" -o "$OUT" -- "$HARNESS" \
    > "$SCRIPT_DIR/afl.log" 2>&1 &
PID=$!
echo "$PID" > "$SCRIPT_DIR/afl.pid"
echo "afl-fuzz PID: $PID"
echo "Monitor:  tail -f $SCRIPT_DIR/afl.log   |   $AFL/afl-whatsup $OUT"
echo "Stop:     kill \$(cat $SCRIPT_DIR/afl.pid)"
