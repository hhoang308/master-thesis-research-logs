#!/bin/bash
# AFL++ structure-aware fuzzing -- xpdf 4.02 -- Type 3 font-cache grammar (CVE-2020-25725).
#
# AFL inputs/queue/crashes are binary PdfDocument protos. The custom mutator .so does:
#   afl_custom_fuzz         -- libprotobuf-mutator mutates the proto (Type 3 cache subtree)
#   afl_custom_post_process -- SerializePdf() -> a Type 3 cache PDF (diverts to
#                              SerializeType3CachePdf when type3_cache_programs is present)
# The target harness (afl_harness_xpdf) renders that PDF through xpdf 4.02 (afl-clang-fast).
#
# Build note: harness compiled with -DXPDF_LEGACY_DISPLAYPAGES=ON (4.02 displayPages signature),
# WITHOUT fontconfig (host lacks libfontconfig-dev). Mutator .so at $PROTO/afl/afl_pdf_mutator.so.
#
# Duration: pass seconds as $1 (default 86400 = 24h). Small value for a sanity run:
#   ./run.sh 60      # 60s smoke test
#   ./run.sh 86400   # full 24h campaign
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
ROOT="${RESEARCH:-$REPO_ROOT/research}"
PROTO="${PDF_PROTO_DIR:-$ROOT/schema/pdf-proto}"
AFL="${AFL_BIN:-$HOME/.local/bin/afl-fuzz}"
AFL_WHATSUP="${AFL_WHATSUP:-$HOME/.local/bin/afl-whatsup}"
PB_PREFIX="${PB_PREFIX:-$HOME/miniconda3}"
HARNESS="$PROTO/build-afl/afl_harness_xpdf"
MUTATOR="$PROTO/afl/afl_pdf_mutator.so"          # NB: moved into afl/ after the module reorg
SEEDS="$SCRIPT_DIR/seeds"
OUT="$SCRIPT_DIR/out"
DURATION="${1:-86400}"

[ -x "$HARNESS" ] || { echo "ERROR: harness not built ($HARNESS) -- see README.md step C"; exit 1; }
[ -f "$MUTATOR" ] || { echo "ERROR: mutator .so missing ($MUTATOR) -- see README.md step D"; exit 1; }
[ -d "$SEEDS" ] && [ -n "$(ls -A "$SEEDS" 2>/dev/null)" ] || { echo "ERROR: seeds dir empty ($SEEDS) -- see README.md step E"; exit 1; }

# afl-fuzz (~/.local) links conda's libpython3.13; the mutator .so links conda's libprotobuf.
export LD_LIBRARY_PATH="$PB_PREFIX/lib:${LD_LIBRARY_PATH:-}"
# Structure-aware mutation only -- replace AFL's byte-level havoc/splice entirely.
export AFL_CUSTOM_MUTATOR_LIBRARY="$MUTATOR"
export AFL_CUSTOM_MUTATOR_ONLY=1
# CRITICAL: keep the ORIGINAL proto in the queue. By default AFL saves the post_process
# OUTPUT (the serialized PDF); those PDFs get fed back to afl_custom_fuzz as buf/add_buf,
# where ParsePartialFromArray parses adversarial PDF bytes as protobuf and corrupts the heap
# ("malloc(): smallbin double linked list corrupted"). Without this, afl-fuzz dies in seconds.
export AFL_POST_PROCESS_KEEP_ORIGINAL=1
# CmpLog intentionally NOT enabled: byte offsets are meaningless after the proto->PDF serialize.
export AFL_SKIP_CPUFREQ=1
export AFL_NO_AFFINITY=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1   # core_pattern piped to apport, no root
export AFL_AUTORESUME=1
# No ASAN_OPTIONS: neither the .so nor the harness is ASan-instrumented on this route.

echo "Starting AFL++ (xpdf 4.02, Type 3 cache / CVE-2020-25725) at $(date)"
echo "  harness:  $HARNESS"
echo "  mutator:  $MUTATOR"
echo "  seeds:    $SEEDS ($(ls "$SEEDS" | wc -l) files)"
echo "  out:      $OUT"
echo "  duration: ${DURATION}s"
echo ""

nohup "$AFL" -V "$DURATION" -m none -t 2000+ -i "$SEEDS" -o "$OUT" -- "$HARNESS" \
    > "$SCRIPT_DIR/afl.log" 2>&1 &
PID=$!
echo "$PID" > "$SCRIPT_DIR/afl.pid"
echo "afl-fuzz PID: $PID"
echo "Monitor:  tail -f $SCRIPT_DIR/afl.log"
echo "          LD_LIBRARY_PATH=$PB_PREFIX/lib $AFL_WHATSUP $OUT"
echo "Stop:     kill \$(cat $SCRIPT_DIR/afl.pid)"
