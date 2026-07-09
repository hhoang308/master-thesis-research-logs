#!/bin/bash
# Start the 24h libFuzzer run with coverage logging.
# Run from anywhere — all paths are absolute.
# Usage: bash run.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY=/home/hoangnh8/master-thesis-research-logs/research/schema/pdf-proto/build/pdf_fuzzer
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

echo "Starting pdf_fuzzer at $(date)"
echo "Binary:   $BINARY"
echo "Corpus:   $CORPUS"
echo "Log:      $FUZZER_LOG"
echo "Coverage: $COVERAGE_LOG"
echo ""

# Write CSV header
echo "# timestamp_unix,exec_count,cov_edges,ft_count,corpus_count,rss_mb" > "$COVERAGE_LOG"

# Start fuzzer: 86400s = 24h
nohup "$BINARY" \
    -max_total_time=86400 \
    -print_final_stats=1 \
    -artifact_prefix="$ARTIFACTS/" \
    "$CORPUS" \
    2>"$FUZZER_LOG" &

FUZZER_PID=$!
echo "$FUZZER_PID" > "$PID_FILE"
echo "Fuzzer PID: $FUZZER_PID"

# Monitor loop: every 60s, parse latest stat line, append to coverage.log
(
    while kill -0 "$FUZZER_PID" 2>/dev/null; do
        sleep 60
        # Match lines like: #12345  NEW cov: 716 ft: 1234 corp: 45/5kb exec/s: 789 rss: 123Mb
        LAST=$(grep -oP '#\d+\s+\S+\s+cov: \d+ ft: \d+ corp: \d+.*rss: \d+Mb' "$FUZZER_LOG" 2>/dev/null | tail -1)
        if [ -n "$LAST" ]; then
            EXEC=$(echo "$LAST" | grep -oP '(?<=#)\d+' | head -1)
            COV=$(echo  "$LAST" | grep -oP '(?<=cov: )\d+')
            FT=$(echo   "$LAST" | grep -oP '(?<=ft: )\d+')
            CORP=$(echo "$LAST" | grep -oP '(?<=corp: )\d+')
            RSS=$(echo  "$LAST" | grep -oP '(?<=rss: )\d+')
            echo "$(date +%s),$EXEC,$COV,$FT,$CORP,$RSS" >> "$COVERAGE_LOG"
        fi
    done
    echo "$(date): Fuzzer PID $FUZZER_PID exited." >> "$FUZZER_LOG"
    rm -f "$PID_FILE" "$MON_PID_FILE"
) &

MONITOR_PID=$!
echo "$MONITOR_PID" > "$MON_PID_FILE"
echo "Monitor PID: $MONITOR_PID"

echo ""
echo "=== How to monitor ==="
echo "  tail -f $FUZZER_LOG"
echo "  watch -n 60 tail $COVERAGE_LOG"
echo "  grep 'cov:' $FUZZER_LOG | tail -5"
echo ""
echo "=== How to stop early ==="
echo "  kill \$(cat $PID_FILE)"
