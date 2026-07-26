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
  LONG_ENGINE     long phase engine: afl (AFL++ persistent mode) or libfuzzer (default: afl)
  AFL_BUILD_DIR   AFL harness build dir (default: build-afl-asan)
  AFL_SEEDS       AFL seed dir of binary protos (auto-generated if empty; default: fuzz-corpus/afl-seeds)
  AFL_TIMEOUT_MS  afl-fuzz per-exec timeout in ms (default: 2000)
  CONDA_LIB       conda lib dir for the AFL toolchain (default: \$CONDA_PREFIX/lib)
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
# ASan libFuzzer workers are memory-hungry (~1-2 GB each under load); defaulting to
# nproc thrashes RAM on core-rich/RAM-poor hosts (coverage stalls at 0, the time
# budget overruns). Default to a few; raise FORK_JOBS when you have the RAM.
fork_jobs="${FORK_JOBS:-$(( $(nproc) < 4 ? $(nproc) : 4 ))}"

# long phase engine: 'afl' (AFL++ persistent mode via build-afl-asan + the proto
# custom mutator) or 'libfuzzer' (the fork-mode libFuzzer path).
long_engine="${LONG_ENGINE:-afl}"
afl_build_dir="${AFL_BUILD_DIR:-build-afl-asan}"
afl_harness="${AFL_HARNESS:-./$afl_build_dir/afl_harness_xpdf}"
afl_mutator="${AFL_MUTATOR:-$project_dir/afl/afl_pdf_mutator.so}"
afl_seeds="${AFL_SEEDS:-fuzz-corpus/afl-seeds}"
afl_timeout_ms="${AFL_TIMEOUT_MS:-2000}"
# AFL toolchain (afl-fuzz, afl-clang-fast target, gen_corpus) is conda-based and
# needs conda's lib on the loader path at runtime.
conda_lib="${CONDA_LIB:-${CONDA_PREFIX:-/home/hoangnh8/miniconda3}/lib}"

if [[ "$phase" == "smoke" ]]; then
  max_len="${MAX_LEN:-1024}"
else
  max_len="${MAX_LEN:-65536}"
fi

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
commit="$(git rev-parse --short HEAD)"
run_dir="$run_root/${stamp}-${commit}-${phase}"
mkdir -p "$run_dir"

# short/long keep fuzzing past crashes and collect every crash. libFuzzer fork
# mode saves them under crash_dir via -artifact_prefix (FORK=0 restores
# single-process stop-on-first); AFL long saves them under its own out dir.
if [[ "$phase" == "long" && "$long_engine" == "afl" ]]; then
  afl_out="$run_dir/afl-out"
  crash_dir="$afl_out/default/crashes"
else
  crash_dir="$run_dir/crashes"
fi
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
  if [[ "$phase" == "long" ]]; then
    echo "long_engine=$long_engine"
    if [[ "$long_engine" == "afl" ]]; then
      echo "afl_build_dir=$afl_build_dir"
      echo "afl_harness=$afl_harness"
      echo "afl_mutator=$afl_mutator"
      echo "afl_seeds=$afl_seeds"
      echo "afl_timeout_ms=$afl_timeout_ms"
    fi
  fi
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

# AFL++ long campaign: persistent-mode target (afl-clang-fast, ASan) + the proto
# custom mutator. Unlike libFuzzer it keeps fuzzing past crashes by default, saving
# each to $afl_out/default/crashes as a serialized PdfDocument proto (convert with
# proto2pdf for a portable .pdf). Runs for $long_seconds via afl-fuzz -V.
run_afl_long() {
  # Seeds are binary PdfDocument protos; seed a starter set if the dir is empty.
  if [[ ! -d "$afl_seeds" || -z "$(ls -A "$afl_seeds" 2>/dev/null)" ]]; then
    mkdir -p "$afl_seeds"
    run_logged cmake --build "$build_dir" --target gen_corpus
    echo
    echo "+ gen_corpus $afl_seeds 200 12345 --binary --with-cve-seeds"
    env LD_LIBRARY_PATH="$conda_lib:${LD_LIBRARY_PATH:-}" \
      "./$build_dir/gen_corpus" "$afl_seeds" 200 12345 --binary --with-cve-seeds
  fi
  run_logged env LD_LIBRARY_PATH="$conda_lib:${LD_LIBRARY_PATH:-}" \
    cmake --build "$afl_build_dir" --target afl_harness_xpdf
  if [[ ! -f "$afl_mutator" ]]; then
    echo "AFL custom mutator missing: $afl_mutator -- run afl/build_mutator.sh" >&2
    return 1
  fi
  mkdir -p "$afl_out"
  echo
  echo "+ afl-fuzz -i $afl_seeds -o $afl_out -m none -t $afl_timeout_ms -V $long_seconds -- $afl_harness"
  # AFL_CUSTOM_MUTATOR_ONLY=1: proto-aware mutation only (byte havoc would corrupt
  # the proto). AFL_I_DONT_CARE...: core_pattern is the apport pipe here; ASan (not
  # cores) reports crashes, so this only silences the check. -m none: ASan maps a
  # huge address space. CmpLog stays OFF (it patches bytes meaningless post-serialize).
  env \
    LD_LIBRARY_PATH="$conda_lib:${LD_LIBRARY_PATH:-}" \
    AFL_SKIP_CPUFREQ=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 AFL_NO_UI=1 AFL_AUTORESUME=1 \
    AFL_CUSTOM_MUTATOR_LIBRARY="$afl_mutator" AFL_CUSTOM_MUTATOR_ONLY=1 \
    ASAN_OPTIONS="$asan_options:abort_on_error=1:symbolize=0" \
    afl-fuzz -i "$afl_seeds" -o "$afl_out" -m none -t "$afl_timeout_ms" -V "$long_seconds" \
    -- "$afl_harness"
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
  elif [[ "$long_engine" == "afl" ]]; then
    run_afl_long
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
  # short/long: a crash is collected output (libFuzzer fork mode or AFL), not a
  # failure. Report how many landed under crash_dir; only an infra/startup failure
  # (non-zero exit before the budget completed) is fatal.
  crash_n=0
  [[ -d "$crash_dir" ]] && crash_n=$(find "$crash_dir" -type f -not -name 'README.txt' 2>/dev/null | wc -l | tr -d ' ')
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
