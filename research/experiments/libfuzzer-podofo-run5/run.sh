#!/bin/bash
# libFuzzer campaign -- PoDoFo 0.9.7 -- Run 5 -- COVERAGE-GUIDED INTO PODOFO SOURCE.
#
# First podofo run where the target library is instrumented from source: the harness links
# thesis/podofo-0.9.7/build-asan/src/podofo/libpodofo.a (clang -fsanitize=address,
# fuzzer-no-link), so libFuzzer coverage feedback reaches podofo's OWN parser (base/*.cpp)
# and ASan poisons podofo's heap. Runs 1-4 linked the uninstrumented SYSTEM libpodofo and
# were coverage-blind inside podofo. Mirrors the CFF campaign (xpdf built from source).
# Build: ./build.sh   |   Duration: pass seconds as $1 (default 86400 = 24h).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
RESEARCH="${RESEARCH:-$REPO_ROOT/research}"
BINARY="${PODOFO_FUZZER_BIN:-$RESEARCH/schema/pdf-proto/build-podofo-fuzz/pdf_fuzzer_podofo}"
CORPUS="$SCRIPT_DIR/corpus"
ARTIFACTS="$SCRIPT_DIR/artifacts"
FUZZER_LOG="$SCRIPT_DIR/fuzzer.log"
COVERAGE_LOG="$SCRIPT_DIR/coverage.log"
PID_FILE="$SCRIPT_DIR/fuzzer.pid"
MON_PID_FILE="$SCRIPT_DIR/monitor.pid"
DURATION="${1:-86400}"
FORKS="${PODOFO_FORKS:-3}"          # only ~4GB RAM free on this host; each ASan worker ~0.4GB

# conda libs (libprotobuf/ssl/crypto/unistring/z) are dynamic -- required at runtime.
export LD_LIBRARY_PATH=/home/hoangnh8/miniconda3/lib:${LD_LIBRARY_PATH:-}

# --- preconditions (fail loudly, CFF-style) ---
if [ ! -x "$BINARY" ]; then
    echo "ERROR: fuzzer not built ($BINARY)"; echo "  -> run ./build.sh"; exit 1
fi
# Guard against silently linking the UNINSTRUMENTED system libpodofo (the runs 1-4 trap).
if ldd "$BINARY" 2>/dev/null | grep -qi podofo; then
    echo "ERROR: $BINARY links a DYNAMIC libpodofo -- that is the uninstrumented system lib."
    echo "  -> rebuild with ./build.sh (must statically link build-asan/.../libpodofo.a)"; exit 1
fi
if [ "$(nm "$BINARY" 2>/dev/null | grep -c '__sanitizer_cov')" -eq 0 ]; then
    echo "ERROR: $BINARY has no SanitizerCoverage -- coverage-blind. Rebuild with ./build.sh"; exit 1
fi
if [ -f "$PID_FILE" ]; then
    echo "ERROR: $PID_FILE exists -- a run may be active. Remove it to force start: rm $PID_FILE"; exit 1
fi
[ -d "$CORPUS" ] && [ -n "$(ls -A "$CORPUS" 2>/dev/null)" ] || { echo "ERROR: corpus empty ($CORPUS)"; exit 1; }

mkdir -p "$CORPUS" "$ARTIFACTS"

# detect_container_overflow=0 -- suppress the protobuf RepeatedField cross-over false
#   positive (instrumented pdf.pb.cc vs non-instrumented conda libprotobuf).
# detect_leaks=0 -- silence unrelated LSan teardown noise.
# allocator_may_return_null=1 -- the KNOWN alloc-size-too-big (PdfPredictorDecoder,
#   findings/podofo-alloc-size) returns null -> podofo raises OutOfMemory -> no re-crash;
#   lets the campaign hunt NEW bugs. Real heap-overflow / UAF detection stays ON.
export ASAN_OPTIONS=detect_container_overflow=0:detect_leaks=0:allocator_may_return_null=1:abort_on_error=1

echo "Starting pdf_fuzzer_podofo (PoDoFo run 5, coverage-into-source) at $(date)"
echo "Binary:    $BINARY"
echo "Corpus:    $CORPUS ($(ls "$CORPUS" | wc -l) seed files)"
echo "Artifacts: $ARTIFACTS   <-- crashes/timeouts saved here"
echo "Duration:  ${DURATION}s   Forks: $FORKS"
echo "ASAN_OPTIONS: $ASAN_OPTIONS"
echo ""

echo "# timestamp_unix,exec_count,cov_edges,ft_count,corpus_count,rss_mb" > "$COVERAGE_LOG"

nohup "$BINARY" \
    -max_total_time="$DURATION" \
    -print_final_stats=1 \
    -artifact_prefix="$ARTIFACTS/" \
    -fork="$FORKS" -ignore_crashes=1 -rss_limit_mb=2048 \
    "$CORPUS" \
    2>"$FUZZER_LOG" &

FUZZER_PID=$!
echo "$FUZZER_PID" > "$PID_FILE"
echo "Fuzzer PID: $FUZZER_PID"

# Coverage monitor: every 60s parse the latest stat line; flag crash artifacts.
(
    while kill -0 "$FUZZER_PID" 2>/dev/null; do
        sleep 60
        # Fork-mode stat lines look like:
        #   #215003: cov: 14033 ft: 17252 corp: 1215 exec/s: 449 oom/timeout/crash: 0/0/0 ...
        # (no per-input "rss:" field), so parse those and sample RSS from ps instead.
        LAST=$(grep -oP '#\d+: cov: \d+ ft: \d+ corp: \d+' "$FUZZER_LOG" 2>/dev/null | tail -1)
        if [ -n "$LAST" ]; then
            EXEC=$(echo "$LAST" | grep -oP '^#\K\d+')
            COV=$(echo  "$LAST" | grep -oP 'cov: \K\d+')
            FT=$(echo   "$LAST" | grep -oP 'ft: \K\d+')
            CORP=$(echo "$LAST" | grep -oP 'corp: \K\d+')
            RSS=$(ps -o rss= -C pdf_fuzzer_podofo 2>/dev/null | awk '{s+=$1} END{printf "%d", s/1024}')
            echo "$(date +%s),$EXEC,$COV,$FT,$CORP,$RSS" >> "$COVERAGE_LOG"
        fi
        N=$(ls "$ARTIFACTS" 2>/dev/null | grep -c "^crash-")
        if [ "$N" -gt 0 ]; then
            echo "$(date): *** $N CRASH(ES) SAVED IN $ARTIFACTS ***" >> "$FUZZER_LOG"
        fi
    done
    echo "$(date): Fuzzer PID $FUZZER_PID exited." >> "$FUZZER_LOG"
    rm -f "$PID_FILE" "$MON_PID_FILE"
) &

MONITOR_PID=$!
echo "$MONITOR_PID" > "$MON_PID_FILE"
echo "Monitor PID: $MONITOR_PID"
echo ""
echo "Monitor:  tail -f $FUZZER_LOG    |    column-parse $COVERAGE_LOG"
echo "Crashes:  ls $ARTIFACTS/         (crash-* are ASan aborts inside podofo)"
echo "Stop:     kill \$(cat $PID_FILE)"
