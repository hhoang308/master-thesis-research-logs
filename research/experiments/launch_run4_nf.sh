#!/bin/bash
# Non-fork relaunch-loop launcher for a run4 fuzzer (24h budget).
# Non-fork is ~80% stable per launch on this box; fork mode's in-process merges
# crash the parent too often, and an ASan run intermittently SIGSEGVs at startup.
# So: run non-fork (single process, saves crashes), and relaunch whenever it exits
# (startup hiccup, teardown SIGSEGV, real crash, or hitting max_total_time) until the
# 24h wall-clock budget is spent. libFuzzer resumes from CORPUS each time (it grows),
# so coverage accumulates. Crashes are saved to artifacts/ (non-fork saves on crash).
# Usage: launch_run4_nf.sh <xpdf|podofo>   (run sequentially -- cleans /tmp fork dirs)
set -u
T="${1:?usage: launch_run4_nf.sh <xpdf|podofo>}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
ROOT="${RESEARCH:-$REPO_ROOT/research}"
DIR="$ROOT/experiments/libfuzzer-${T}-run4"
if [ "$T" = "podofo" ]; then BIN="${PODOFO_FUZZER_BIN:-$ROOT/thesis/pdf-proto-schema/build/pdf_fuzzer_podofo}";
else BIN="${PDF_FUZZER_BIN:-$ROOT/thesis/pdf-proto-schema/build/pdf_fuzzer}"; fi
CORPUS="$DIR/corpus"; ARTIFACTS="$DIR/artifacts"; LOG="$DIR/fuzzer.log"

export ASAN_OPTIONS=detect_container_overflow=0:detect_leaks=0:allocator_may_return_null=1
mkdir -p "$CORPUS" "$ARTIFACTS"
echo "$$" > "$DIR/launcher.pid"

BUDGET=86400
START=$(date +%s); END=$((START + BUDGET))
launches=0; instant_fails=0
echo "$(date): [$T] launcher start, 24h budget" >> "$LOG"

while :; do
    NOW=$(date +%s); REM=$((END - NOW))
    [ "$REM" -lt 30 ] && break
    rm -rf /tmp/libFuzzerTemp.FuzzWithFork* 2>/dev/null   # sequential -> safe
    launches=$((launches+1))
    echo "=== $(date): [$T] launch #$launches, ${REM}s remaining ===" >> "$LOG"
    T0=$(date +%s)
    "$BIN" -max_total_time="$REM" -rss_limit_mb=2048 -print_final_stats=1 \
        -artifact_prefix="$ARTIFACTS/" "$CORPUS" >> "$LOG" 2>&1
    rc=$?
    DUR=$(( $(date +%s) - T0 ))
    echo "$(date): [$T] launch #$launches exited rc=$rc after ${DUR}s (crashes saved: $(ls "$ARTIFACTS" 2>/dev/null | grep -c '^crash-'))" >> "$LOG"
    # Back off if it keeps dying instantly (startup gremlin clustering)
    if [ "$DUR" -lt 5 ]; then
        instant_fails=$((instant_fails+1))
        [ "$instant_fails" -ge 30 ] && { echo "$(date): [$T] too many instant failures, aborting" >> "$LOG"; break; }
    else
        instant_fails=0
    fi
done

echo "$(date): [$T] 24h budget complete after $launches launches, $(ls "$ARTIFACTS" 2>/dev/null | grep -c '^crash-') crash artifacts" >> "$LOG"
rm -f "$DIR/launcher.pid"
