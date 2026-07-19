#!/bin/bash
# 1-hour libFuzzer campaign against xpdf-4.02 (structure-aware, libprotobuf-mutator).
# Reuses the prebuilt binary build-402/pdf_fuzzer (XPDF_SRC=xpdf-4.02, XPDF_LEGACY_DISPLAYPAGES=ON).
# Corpus is seeded with binary PdfDocument protos from `gen_corpus ... --clean --binary`.
#
# Modeled on ../launch_run4.sh: retry-launch until the (nondeterministic) ASan fork-teardown
# startup gremlin is survived, then detach a 60s coverage monitor for the full hour.
# Usage: ./run.sh          (runs in the foreground of whatever launches it; self-ends at 1h)
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
ROOT="${RESEARCH:-$REPO_ROOT/research}"
DIR="$ROOT/experiments/libfuzzer-xpdf402-1h"
BIN="${PDF_FUZZER_402_BIN:-$ROOT/thesis/pdf-proto-schema/build-402/pdf_fuzzer}"
CORPUS="$DIR/corpus"; CRASHES="$DIR/crashes"
LOG="$DIR/fuzzer.log"; COV="$DIR/coverage.log"
DURATION=3600   # 1 hour

# detect_container_overflow=0 : suppress the instrumented-pb.cc / non-instrumented libprotobuf
#                               false positive on RepeatedField<int> (FontDescriptor.font_bbox).
# detect_leaks=0             : silence the unrelated LSan teardown crash.
# Genuine heap-overflow / UAF still fire.
export ASAN_OPTIONS=detect_container_overflow=0:detect_leaks=0

mkdir -p "$CORPUS" "$CRASHES"
echo "# timestamp_unix,exec_count,cov_edges,ft_count,corpus_count,rss_mb" > "$COV"

PID=""
for attempt in $(seq 1 10); do
    rm -rf /tmp/libFuzzerTemp.FuzzWithFork* 2>/dev/null   # safe: single fuzzer on this box
    nohup "$BIN" -fork=1 -ignore_ooms=1 -ignore_timeouts=1 -ignore_crashes=1 \
        -rss_limit_mb=1024 -max_total_time="$DURATION" -print_final_stats=1 \
        -artifact_prefix="$CRASHES/" "$CORPUS" > "$LOG" 2>&1 &
    CAND=$!
    sleep 25
    if kill -0 "$CAND" 2>/dev/null && grep -qa 'cov: [0-9]' "$LOG"; then
        PID="$CAND"
        echo "$(date): started on attempt $attempt (PID $PID)" >> "$LOG"
        break
    fi
    kill -9 "$CAND" 2>/dev/null; wait "$CAND" 2>/dev/null
done

if [ -z "$PID" ]; then
    echo "FAILED: fuzzer did not survive startup after 10 attempts" | tee -a "$LOG"
    exit 1
fi
echo "$PID" > "$DIR/fuzzer.pid"

# Coverage monitor (fork-mode stat line: "#N: cov: X ft: Y corp: Z ... rss: R") + crash alert.
(
    while kill -0 "$PID" 2>/dev/null; do
        sleep 60
        LAST=$(grep -aoP '#\d+: cov: \d+ ft: \d+ corp: \d+.*' "$LOG" 2>/dev/null | tail -1)
        if [ -n "$LAST" ]; then
            EXEC=$(echo "$LAST" | grep -oP '(?<=#)\d+' | head -1)
            CV=$(echo "$LAST"   | grep -oP '(?<=cov: )\d+')
            FT=$(echo "$LAST"   | grep -oP '(?<=ft: )\d+')
            CP=$(echo "$LAST"   | grep -oP '(?<=corp: )\d+')
            RS=$(echo "$LAST"   | grep -oP '(?<=rss: )\d+')
            echo "$(date +%s),$EXEC,$CV,$FT,$CP,$RS" >> "$COV"
        fi
        N=$(ls "$CRASHES" 2>/dev/null | grep -c "^crash-")
        [ "$N" -gt 0 ] && echo "$(date): *** $N crash-* artifact(s) in $CRASHES ***" >> "$LOG"
    done
    echo "$(date): fuzzer PID $PID exited." >> "$LOG"
    rm -f "$DIR/fuzzer.pid" "$DIR/monitor.pid"
) &
echo "$!" > "$DIR/monitor.pid"
echo "RUNNING: fuzzer PID $PID, monitor $(cat "$DIR/monitor.pid"), ends in ${DURATION}s"
