#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: scripts/run_fuzz_phase.sh smoke|short|long

Environment overrides:
  BUILD_DIR       CMake build directory (default: build if present, otherwise build-env-check-clang18)
  FUZZER          Fuzzer binary path (default: $BUILD_DIR/pdf_fuzzer)
  CORPUS_DIR      Corpus directory for short/long (default: fuzz-corpus/main)
  RUN_ROOT        Directory for run logs (default: fuzz-runs)
  SMOKE_RUNS      libFuzzer smoke run count (default: 100)
  SHORT_SECONDS   short fuzz duration (default: 1800)
  LONG_SECONDS    long fuzz duration (default: 28800)
  MAX_LEN         libFuzzer max_len (smoke default: 1024, short/long default: 65536)
  ASAN_OPTIONS    sanitizer options
USAGE
}

phase="${1:-}"
case "$phase" in
  smoke|short|long) ;;
  -h|--help|"") usage; exit 0 ;;
  *) usage >&2; exit 2 ;;
esac

repo_root="$(git rev-parse --show-toplevel)"
project_dir="$repo_root/research/schema/pdf-proto"
cd "$project_dir"

if [[ -n "${BUILD_DIR:-}" ]]; then
  build_dir="$BUILD_DIR"
elif [[ -d build ]]; then
  build_dir="build"
else
  build_dir="build-env-check-clang18"
fi
fuzzer="${FUZZER:-./$build_dir/pdf_fuzzer}"
corpus_dir="${CORPUS_DIR:-fuzz-corpus/main}"
run_root="${RUN_ROOT:-fuzz-runs}"
smoke_runs="${SMOKE_RUNS:-100}"
short_seconds="${SHORT_SECONDS:-1800}"
long_seconds="${LONG_SECONDS:-28800}"
asan_options="${ASAN_OPTIONS:-detect_container_overflow=0:detect_leaks=0}"

if [[ "$phase" == "smoke" ]]; then
  max_len="${MAX_LEN:-1024}"
else
  max_len="${MAX_LEN:-65536}"
fi

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
commit="$(git rev-parse --short HEAD)"
run_dir="$run_root/${stamp}-${commit}-${phase}"
mkdir -p "$run_dir"

log="$run_dir/run.log"
metadata="$run_dir/metadata.txt"

{
  echo "phase=$phase"
  echo "utc_start=$stamp"
  echo "repo_root=$repo_root"
  echo "project_dir=$project_dir"
  echo "commit=$(git rev-parse HEAD)"
  echo "build_dir=$build_dir"
  echo "fuzzer=$fuzzer"
  echo "corpus_dir=$corpus_dir"
  echo "max_len=$max_len"
  echo "asan_options=$asan_options"
  echo "git_status_short:"
  git status --short
} > "$metadata"

run_logged() {
  echo
  echo "+ $*"
  "$@"
}

run_logged_env() {
  echo
  echo "+ ASAN_OPTIONS=$asan_options $*"
  env ASAN_OPTIONS="$asan_options" "$@"
}

{
  echo "== fuzz phase: $phase =="
  cat "$metadata"

  if [[ "$phase" == "smoke" ]]; then
    echo "Build step: cmake regenerates protobuf sources from .proto files when they are out of date."
    run_logged cmake --build "$build_dir" --target verify_serializer verify_cff pdf_fuzzer
    run_logged_env "./$build_dir/verify_cff"
    run_logged_env "./$build_dir/verify_serializer"
    run_logged_env "$fuzzer" "-runs=$smoke_runs" "-max_len=$max_len"
  elif [[ "$phase" == "short" ]]; then
    mkdir -p "$corpus_dir"
    run_logged cmake --build "$build_dir" --target pdf_fuzzer
    run_logged_env "$fuzzer" "-max_total_time=$short_seconds" "-max_len=$max_len" "$corpus_dir"
  else
    mkdir -p "$corpus_dir"
    run_logged cmake --build "$build_dir" --target pdf_fuzzer
    run_logged_env "$fuzzer" "-max_total_time=$long_seconds" "-max_len=$max_len" "$corpus_dir"
  fi
} 2>&1 | tee "$log"

if grep -E "CRASH|ERROR: AddressSanitizer|runtime error:" "$log" >/dev/null; then
  echo "Finding: sanitizer/crash marker found in $log" >&2
  exit 1
fi

if [[ "$phase" == "smoke" ]]; then
  grep -q "ALL PASS (0 failures)" "$log"
  grep -q "escape-operators-byte-encoding" "$log"
  grep -q "hintmask-cntrmask-encoding" "$log"
  grep -q "wrong-arity-underflow-valid-container" "$log"
  grep -q "fontfile3-structured-cff-extended-ops" "$log"
  grep -q "found LLVMFuzzerCustomMutator" "$log"
  grep -q "INITED" "$log"
  grep -q "DONE" "$log"
fi

echo "Run log: $log"
