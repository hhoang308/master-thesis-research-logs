#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: scripts/run_fuzz_phase.sh smoke|short|long

Environment overrides:
  BUILD_DIR       CMake build directory (default: build; must be a clang libFuzzer+ASan build)
  FUZZER          Fuzzer binary path (default: $BUILD_DIR/pdf_fuzzer)
  CORPUS_DIR      Corpus directory for short/long (default: fuzz-corpus/main)
  RUN_ROOT        Directory for run logs (default: fuzz-runs)
  SMOKE_RUNS      libFuzzer smoke run count (default: 100)
  SHORT_SECONDS   short fuzz duration (default: 1800)
  LONG_SECONDS    long fuzz duration (default: 28800)
  MAX_LEN         libFuzzer max_len (smoke default: 1024, short/long default: 65536)
  ASAN_OPTIONS    sanitizer options
  DISABLE_ASLR    0/1 to force; default auto = disable ASLR (setarch -R) only for clang < 18
                  (dodges the clang<18 ASan startup SIGSEGV; clang>=18 keeps ASLR on)
  FORK            short/long: 1 = libFuzzer fork mode (keep fuzzing past crashes,
                  collect many), 0 = single process / stop on first crash (default: 1)
  FORK_JOBS       fork-mode parallel workers (default: nproc)
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
  echo "No build directory found." >&2
  echo "Expected ./build (README: cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ ...)," >&2
  echo "or set BUILD_DIR to an existing clang libFuzzer+ASan build tree." >&2
  echo "Existing build dirs: $(ls -d build*/ 2>/dev/null | tr '\n' ' ')" >&2
  exit 2
fi

if [[ ! -d "$build_dir" ]]; then
  echo "Build dir '$build_dir' does not exist." >&2
  echo "Existing build dirs: $(ls -d build*/ 2>/dev/null | tr '\n' ' ')" >&2
  exit 2
fi

# pdf_fuzzer requires clang (-fsanitize=fuzzer,address); a gcc build tree cannot
# build it. Warn early instead of failing later with a confusing linker error.
build_cc="$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$build_dir/CMakeCache.txt" 2>/dev/null | head -1 || true)"
if [[ -n "$build_cc" && "$build_cc" != *clang* ]]; then
  echo "Warning: build dir '$build_dir' uses compiler '$build_cc' (not clang);" >&2
  echo "         the pdf_fuzzer target needs clang for -fsanitize=fuzzer,address." >&2
fi
build_cc_version="$([[ -n "$build_cc" ]] && "$build_cc" --version 2>/dev/null | head -1 || echo unknown)"

# clang < 18's statically-linked ASan runtime intermittently SIGSEGVs during its
# own init (AsanInitInternal -> mmap) on kernels with high ASLR entropy
# (vm.mmap_rnd_bits) -- it hits EVERY ASan binary here (verify_cff,
# verify_serializer, pdf_fuzzer). clang >= 18's ASan tolerates it (verified: 0 vs
# clang-14's ~30% startup SIGSEGV on a trivial program). So keep ASLR on for
# clang >= 18, and otherwise re-exec the whole process tree once under `setarch -R`
# (ASLR off). DISABLE_ASLR=0/1 overrides the auto choice.
cc_major="$(printf '%s\n' "$build_cc_version" | grep -oE 'clang version [0-9]+' | grep -oE '[0-9]+' | head -1 || true)"
disable_aslr="${DISABLE_ASLR:-auto}"
if [[ "$disable_aslr" == "auto" ]]; then
  if [[ -n "$cc_major" && "$cc_major" -ge 18 ]]; then disable_aslr=0; else disable_aslr=1; fi
fi
if [[ "$disable_aslr" == "1" && -z "${_FUZZ_PHASE_NORANDOM:-}" ]] && command -v setarch >/dev/null 2>&1; then
  export _FUZZ_PHASE_NORANDOM=1
  exec setarch -R "$0" "$@"
fi

fuzzer="${FUZZER:-./$build_dir/pdf_fuzzer}"
corpus_dir="${CORPUS_DIR:-fuzz-corpus/main}"
run_root="${RUN_ROOT:-fuzz-runs}"
smoke_runs="${SMOKE_RUNS:-100}"
short_seconds="${SHORT_SECONDS:-1800}"
long_seconds="${LONG_SECONDS:-28800}"
asan_options="${ASAN_OPTIONS:-detect_container_overflow=0:detect_leaks=0}"
fork="${FORK:-1}"
fork_jobs="${FORK_JOBS:-$(nproc)}"

if [[ "$phase" == "smoke" ]]; then
  max_len="${MAX_LEN:-1024}"
else
  max_len="${MAX_LEN:-65536}"
fi

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
commit="$(git rev-parse --short HEAD)"
run_dir="$run_root/${stamp}-${commit}-${phase}"
mkdir -p "$run_dir"

# short/long fuzz in libFuzzer fork mode: keep fuzzing past crashes and collect
# every crash/oom/timeout under crash_dir (via -artifact_prefix), instead of the
# default "exit on first crash". FORK=0 restores single-process stop-on-first.
crash_dir="$run_dir/crashes"
fork_args=("-artifact_prefix=$crash_dir/")
if [[ "$fork" == "1" ]]; then
  fork_args+=("-fork=$fork_jobs" "-ignore_crashes=1" "-ignore_timeouts=1" "-ignore_ooms=1")
fi

log="$run_dir/run.log"
metadata="$run_dir/metadata.txt"

{
  echo "phase=$phase"
  echo "utc_start=$stamp"
  echo "repo_root=$repo_root"
  echo "project_dir=$project_dir"
  echo "commit=$(git rev-parse HEAD)"
  echo "build_dir=$build_dir"
  echo "build_cc=${build_cc:-unknown}"
  echo "build_cc_version=$build_cc_version"
  echo "fuzzer=$fuzzer"
  echo "corpus_dir=$corpus_dir"
  echo "max_len=$max_len"
  echo "asan_options=$asan_options"
  echo "fork=$fork"
  echo "fork_jobs=$fork_jobs"
  echo "crash_dir=$crash_dir"
  echo "disable_aslr=$disable_aslr"
  echo "no_randomize=${_FUZZ_PHASE_NORANDOM:-0}"
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

# Disable errexit for the parent so a non-zero pipeline (e.g. the fuzzer exiting
# after finding a crash) does not abort before crash detection below. The group
# re-enables errexit for itself so build/verify steps still fail fast.
set +e
{
  set -e
  echo "== fuzz phase: $phase =="
  cat "$metadata"

  if [[ "$phase" == "smoke" ]]; then
    echo "Build step: cmake regenerates protobuf sources from .proto files when they are out of date."
    run_logged cmake --build "$build_dir" --target verify_serializer verify_cff pdf_fuzzer
    run_logged_env "./$build_dir/verify_cff"
    run_logged_env "./$build_dir/verify_serializer"
    run_logged_env "$fuzzer" "-runs=$smoke_runs" "-max_len=$max_len"
  elif [[ "$phase" == "short" ]]; then
    mkdir -p "$corpus_dir" "$crash_dir"
    run_logged cmake --build "$build_dir" --target pdf_fuzzer
    run_logged_env "$fuzzer" "${fork_args[@]}" "-max_total_time=$short_seconds" "-max_len=$max_len" "$corpus_dir"
  else
    mkdir -p "$corpus_dir" "$crash_dir"
    run_logged cmake --build "$build_dir" --target pdf_fuzzer
    run_logged_env "$fuzzer" "${fork_args[@]}" "-max_total_time=$long_seconds" "-max_len=$max_len" "$corpus_dir"
  fi
} 2>&1 | tee "$log"
run_status=${PIPESTATUS[0]}
set -e

if [[ "$phase" == "smoke" ]]; then
  # Gate: any crash/sanitizer marker or non-zero exit fails the smoke. The crash
  # grep runs regardless of exit status so a real crash is always annotated.
  if grep -Eq "CRASH|ERROR: AddressSanitizer|runtime error:" "$log"; then
    echo "Finding: sanitizer/crash marker found in $log" >&2
    exit 1
  fi
  if [[ "$run_status" -ne 0 ]]; then
    echo "Finding: build/verify/fuzzer command exited $run_status (see $log)" >&2
    exit "$run_status"
  fi
  require_marker() {
    grep -q "$1" "$log" || {
      echo "smoke: missing expected marker: $1 (see $log)" >&2
      exit 1
    }
  }
  require_marker "ALL PASS (0 failures)"
  require_marker "escape-operators-byte-encoding"
  require_marker "hintmask-cntrmask-encoding"
  require_marker "wrong-arity-underflow-valid-container"
  require_marker "fontfile3-structured-cff-extended-ops"
  require_marker "found LLVMFuzzerCustomMutator"
  require_marker "INITED"
  require_marker "DONE"
else
  # short/long: in fork mode a crash is collected output, not a failure. Report
  # how many artifacts landed under crash_dir; only an infra/startup failure
  # (non-zero exit before the budget completed -- ignore_crashes keeps the
  # campaign running otherwise) is fatal.
  crash_n=0
  [[ -d "$crash_dir" ]] && crash_n=$(find "$crash_dir" -type f 2>/dev/null | wc -l | tr -d ' ')
  if [[ "$crash_n" -gt 0 ]]; then
    echo "Finding: $crash_n crash/oom/timeout artifact(s) saved under $crash_dir" >&2
  elif grep -Eq "CRASH|ERROR: AddressSanitizer|runtime error:" "$log"; then
    echo "Finding: sanitizer/crash marker in $log but no artifact captured (check $crash_dir)" >&2
  fi
  if [[ "$run_status" -ne 0 ]]; then
    echo "Finding: fuzzer exited $run_status before completing the budget (see $log)" >&2
    exit "$run_status"
  fi
fi

echo "Run log: $log"
