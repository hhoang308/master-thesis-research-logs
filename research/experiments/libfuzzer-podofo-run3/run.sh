#!/bin/bash
# Start the 24h libFuzzer run -- PoDoFo 0.9.7 -- Run 3
# Schema: Stage 1+2 + 4 gap fixes + P1 (Font) + P1.5 (embedded FontFile/FontDescriptor)
#         + font-gap Step A (Encoding/Differences/Widths/ToUnicode)
#         + font-gap Step B (CID/Type0 DescendantFonts/CIDToGIDMap/W).
# Corpus seeded from run2 corpus (52) + 5 font seeds.
# Run from anywhere -- all paths are absolute.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
RESEARCH="${RESEARCH:-$REPO_ROOT/research}"
BINARY="${PODOFO_FUZZER_BIN:-$RESEARCH/thesis/pdf-proto-schema/build/pdf_fuzzer_podofo}"
CORPUS="$SCRIPT_DIR/corpus"
ARTIFACTS="$SCRIPT_DIR/artifacts"
FUZZER_LOG="$SCRIPT_DIR/fuzzer.log"
COVERAGE_LOG="$SCRIPT_DIR/coverage.log"
PID_FILE="$SCRIPT_DIR/fuzzer.pid"
MON_PID_FILE="$SCRIPT_DIR/monitor.pid"

if [ ! -x "$BINARY" ]; then
    echo "ERROR: binary not found at $BINARY"
    exit 1
fi
if [ -f "$PID_FILE" ]; then
    echo "ERROR: $PID_FILE exists -- is a run already active?"
    echo "  Remove it to force start: rm $PID_FILE"
    exit 1
fi

mkdir -p "$CORPUS" "$ARTIFACTS"

# REQUIRED from P1.5 on:
#  detect_container_overflow=0 -- suppress the false positive in protobuf
#    RepeatedField<int> cross-over (instrumented pdf.pb.cc vs non-instrumented
#    libprotobuf). Without it the run aborts on the first font_bbox/W cross-over.
#  detect_leaks=0 -- silence the unrelated LSan teardown crash.
# Real heap-overflow / UAF detection stays ON.
export ASAN_OPTIONS=detect_container_overflow=0:detect_leaks=0

echo "Starting pdf_fuzzer (PoDoFo run 3) at $(date)"
echo "Binary:    $BINARY"
echo "Corpus:    $CORPUS ($(ls $CORPUS | wc -l) seed files)"
echo "Artifacts: $ARTIFACTS  <-- crashes/timeouts saved here"
echo "ASAN_OPTIONS: $ASAN_OPTIONS"
echo ""

echo "# timestamp_unix,exec_count,cov_edges,ft_count,corpus_count,rss_mb" > "$COVERAGE_LOG"

nohup "$BINARY" \
    -max_total_time=86400 \
    -print_final_stats=1 \
    -artifact_prefix="$ARTIFACTS/" \
    "$CORPUS" \
    2>"$FUZZER_LOG" &

FUZZER_PID=$!
echo "$FUZZER_PID" > "$PID_FILE"
echo "Fuzzer PID: $FUZZER_PID"

# Coverage monitor: every 60s parse latest stat line; alert on crash artifacts.
(
    while kill -0 "$FUZZER_PID" 2>/dev/null; do
        sleep 60
        LAST=$(grep -oP '#\d+\s+\S+\s+cov: \d+ ft: \d+ corp: \d+.*rss: \d+Mb' "$FUZZER_LOG" 2>/dev/null | tail -1)
        if [ -n "$LAST" ]; then
            EXEC=$(echo "$LAST" | grep -oP '(?<=#)\d+' | head -1)
            COV=$(echo  "$LAST" | grep -oP '(?<=cov: )\d+')
            FT=$(echo   "$LAST" | grep -oP '(?<=ft: )\d+')
            CORP=$(echo "$LAST" | grep -oP '(?<=corp: )\d+')
            RSS=$(echo  "$LAST" | grep -oP '(?<=rss: )\d+')
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
echo "=== Crash inputs are saved to: $ARTIFACTS ==="
echo "=== Stop early: kill \$(cat $PID_FILE) ==="
