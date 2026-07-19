#!/bin/bash
# Retry-launch a run4 fuzzer until it survives the (nondeterministic) sanitizer-teardown
# startup gremlin, then detach it + a coverage monitor for the full 24h.
# Usage: launch_run4.sh <xpdf|podofo>
# Run sequentially (one target at a time) -- it cleans /tmp/libFuzzerTemp between attempts.
set -u
T="${1:?usage: launch_run4.sh <xpdf|podofo>}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
ROOT="${RESEARCH:-$REPO_ROOT/research}"
DIR="$ROOT/experiments/libfuzzer-${T}-run4"
if [ "$T" = "podofo" ]; then BIN="${PODOFO_FUZZER_BIN:-$ROOT/thesis/pdf-proto-schema/build/pdf_fuzzer_podofo}";
else BIN="${PDF_FUZZER_BIN:-$ROOT/thesis/pdf-proto-schema/build/pdf_fuzzer}"; fi
CORPUS="$DIR/corpus"; ARTIFACTS="$DIR/artifacts"
LOG="$DIR/fuzzer.log"; COV="$DIR/coverage.log"

export ASAN_OPTIONS=detect_container_overflow=0:detect_leaks=0:allocator_may_return_null=1
mkdir -p "$CORPUS" "$ARTIFACTS"
echo "# timestamp_unix,exec_count,cov_edges,ft_count,corpus_count,rss_mb" > "$COV"

PID=""
for attempt in $(seq 1 15); do
    rm -rf /tmp/libFuzzerTemp.FuzzWithFork* 2>/dev/null   # safe: sequential, no other live fuzzer
    nohup "$BIN" -fork=1 -rss_limit_mb=2048 -max_total_time=86400 \
        -print_final_stats=1 -artifact_prefix="$ARTIFACTS/" "$CORPUS" 2>"$LOG" &
    CAND=$!
    sleep 25
    if kill -0 "$CAND" 2>/dev/null && grep -qa 'cov: [0-9]' "$LOG"; then
        PID="$CAND"
        echo "$(date): [$T] started on attempt $attempt (PID $PID)" >> "$LOG"
        break
    fi
    kill -9 "$CAND" 2>/dev/null; wait "$CAND" 2>/dev/null
done

if [ -z "$PID" ]; then
    echo "FAILED: [$T] did not survive startup after 15 attempts"
    exit 1
fi
echo "$PID" > "$DIR/fuzzer.pid"

# Coverage monitor (fork-mode stat line format) + crash alert.
(
    while kill -0 "$PID" 2>/dev/null; do
        sleep 60
        LAST=$(grep -aoP '#\d+: cov: \d+ ft: \d+ corp: \d+.*' "$LOG" 2>/dev/null | tail -1)
        if [ -n "$LAST" ]; then
            EXEC=$(echo "$LAST" | grep -oP '(?<=#)\d+' | head -1)
            CV=$(echo "$LAST" | grep -oP '(?<=cov: )\d+')
            FT=$(echo "$LAST" | grep -oP '(?<=ft: )\d+')
            CP=$(echo "$LAST" | grep -oP '(?<=corp: )\d+')
            echo "$(date +%s),$EXEC,$CV,$FT,$CP," >> "$COV"
        fi
        N=$(ls "$ARTIFACTS" 2>/dev/null | grep -c "^crash-")
        [ "$N" -gt 0 ] && echo "$(date): *** $N CRASH(ES) in $ARTIFACTS ***" >> "$LOG"
    done
    echo "$(date): [$T] fuzzer PID $PID exited." >> "$LOG"
    rm -f "$DIR/fuzzer.pid" "$DIR/monitor.pid"
) &
echo "$!" > "$DIR/monitor.pid"
echo "RUNNING: [$T] fuzzer PID $PID, monitor $(cat $DIR/monitor.pid)"
exit 0
