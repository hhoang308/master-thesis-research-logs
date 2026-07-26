# Fuzzing Runbook

This runbook is the source of truth for the schema fuzzing loop. Do not rely on
chat memory for whether a phase was run; rely on the run log created by
`scripts/run_fuzz_phase.sh`.

## When To Run Each Phase

Run `smoke` after every schema, serializer, verifier, harness, or seed change.
It checks that generated protobuf sources rebuild, CFF/PDF verifier cases pass,
and the xpdf libFuzzer pipeline can complete a tiny run.

This script does not replace lower-level scripts such as `afl/build_mutator.sh`
or `coverage/cov_compare.sh`. It is an orchestration wrapper for the routine
smoke/short/long loop: build the relevant target, run the agreed verifier/fuzzer
commands, and write a timestamped log.

Run `short` after a schema change adds a new semantic path or mutation class.
Examples: new Type2 charstring operators, explicit bad subr index patterns, or
controlled offset corruption knobs. Use this to compare coverage and reject
rate before spending long fuzzing time.

Run `long` only after `smoke` and `short` are clean and the new schema actually
reaches useful parser paths. Long runs should be tied to a commit and corpus,
not just "whenever the machine is free".

## Commands

From `research/schema/pdf-proto`:

```bash
./scripts/run_fuzz_phase.sh smoke
./scripts/run_fuzz_phase.sh short
./scripts/run_fuzz_phase.sh long
```

Useful overrides:

```bash
BUILD_DIR=build ./scripts/run_fuzz_phase.sh smoke
SHORT_SECONDS=7200 MAX_LEN=65536 ./scripts/run_fuzz_phase.sh short
LONG_SECONDS=86400 ./scripts/run_fuzz_phase.sh long          # AFL long (default engine)
LONG_ENGINE=libfuzzer ./scripts/run_fuzz_phase.sh long       # libFuzzer fork-mode long instead
FORK_JOBS=8 ./scripts/run_fuzz_phase.sh short                # more fork workers (RAM permitting)
FORK=0 ./scripts/run_fuzz_phase.sh short                     # single process, stop on first crash
```

## Engines per phase

`smoke` is a single-process libFuzzer gate: the first crash fails it. `short` runs
**libFuzzer fork mode**; `long` runs **AFL++ persistent mode** by default, or
libFuzzer fork mode with `LONG_ENGINE=libfuzzer`.

### libFuzzer fork mode (short; long with LONG_ENGINE=libfuzzer)

`FORK=1` (default). `FORK_JOBS` defaults to a few workers -- **not** nproc, because
ASan workers are memory-hungry and too many thrash RAM (coverage stalls at 0, the
time budget overruns). Behaviour:

- Keeps fuzzing **past** crashes instead of exiting on the first
  (`-fork -ignore_crashes -ignore_timeouts -ignore_ooms`), collecting many distinct
  crashes over the whole time budget.
- Every crash/oom/timeout reproducer is saved under `<run_dir>/crashes/`
  (`-artifact_prefix`); the run reports how many it collected.
- A collected crash is **not** a failure: the run exits 0 when it completes its
  budget. Only an infra/startup failure (non-zero exit before the budget completes)
  is fatal. `FORK=0` restores single-process, stop-on-first-crash.

### AFL++ long (default: LONG_ENGINE=afl)

`long` defaults to AFL++ persistent mode against the `build-afl-asan` harness
(`afl-clang-fast` + ASan) driven by the proto custom mutator -- a different engine
from libFuzzer, so the two find different bugs.

- AFL corpus/seeds/crashes are **binary `PdfDocument` protos**. Seeds are
  auto-generated with `gen_corpus --binary` into `AFL_SEEDS` (default
  `fuzz-corpus/afl-seeds`) when empty. Crashes stay protos -- convert one to a
  portable `.pdf` with `proto2pdf`.
- Runs for `LONG_SECONDS` via `afl-fuzz -V`, `-m none` (ASan), custom mutator only
  (`AFL_CUSTOM_MUTATOR_ONLY=1`; CmpLog stays off). Output goes to `<run_dir>/afl-out/`;
  crashes under `<run_dir>/afl-out/default/crashes/`.
- Like fork mode, a collected crash is not a failure: the run exits 0 on budget
  completion and reports the crash count. The AFL toolchain is conda-based and needs
  `CONDA_LIB` (default `$CONDA_PREFIX/lib`) on the loader path -- the runner sets it.

## Pass Criteria

Smoke passes when:

- `verify_cff` exits 0 and reports `ALL PASS`.
- `verify_serializer` exits 0.
- The CFF extended-operator verifier cases appear in the log:
  `escape-operators-byte-encoding`, `hintmask-cntrmask-encoding`,
  `wrong-arity-underflow-valid-container`.
- The PDF serializer case `fontfile3-structured-cff-extended-ops` appears in the log.
- `pdf_fuzzer -runs=100 -max_len=1024` exits 0.
- The fuzzer log contains `found LLVMFuzzerCustomMutator`, `INITED`, and `DONE`.
- There is no `CRASH`, `ERROR: AddressSanitizer`, or `runtime error:` report.

Short passes when:

- The fork campaign runs its full `SHORT_SECONDS` and exits 0 (crashes collected
  under `<run_dir>/crashes/` do not fail it; an early non-zero exit does).
- Coverage/feature counts grow or remain explainably stable.
- Inputs are not rejected before the target area most of the time.
- Any collected crash is triaged before merging its corpus into a long run.

Long passes when:

- The campaign (AFL++ by default, or libFuzzer fork mode) finishes its configured
  time budget (or is intentionally stopped) with exit 0; collected crashes are the
  expected output, not a failure.
- Collected crashes are reproducible under the agreed triage path -- for AFL under
  `<run_dir>/afl-out/default/crashes/` (protos; `proto2pdf` them first), for
  libFuzzer under `<run_dir>/crashes/`.
- The log/metadata records commit, command, engine, sanitizer options, and run
  directory.

## Default Sanitizer Options

The script uses:

```text
ASAN_OPTIONS=detect_container_overflow=0:detect_leaks=0
```

This matches the project README: it avoids a protobuf mutation-layer false
positive and LeakSanitizer teardown noise while keeping normal ASan bug classes
such as heap overflows and UAFs enabled.

## ASLR / ASan startup crash

With clang < 18, every ASan binary (`verify_cff`, `verify_serializer`,
`pdf_fuzzer`) intermittently SIGSEGVs at startup inside ASan's own allocator init,
because that ASan runtime clashes with the kernel's high ASLR entropy (README
Issue 10). Left unhandled this makes the smoke gate flap (~15-25% false failures).

The script picks the mitigation automatically from the build compiler
(`metadata.txt` records `build_cc`, `build_cc_version`, `disable_aslr`,
`no_randomize`):

- **clang >= 18** (e.g. the `build-clang18` tree): ASLR stays **on** -- its ASan
  tolerates high entropy. Verified 0/25 (fuzzer) / 0/25 (verify_cff) / 0/15 (gate).
- **clang < 18** (e.g. the clang-14 `build` tree): the script re-execs once under
  `setarch -R` (ASLR off for the whole process tree). Verified 0/15. This is a
  stopgap -- it does not weaken ASan detection (layout-independent), only turns
  ASLR off for the run.

`DISABLE_ASLR=0` / `=1` forces either way (e.g. `=0` after
`sudo sysctl -w vm.mmap_rnd_bits=28`). Prefer the clang-18 build so ASLR stays on.

## Portability

The runbook and script are intended to be committed. Generated run artifacts are
not: `fuzz-runs/` and `fuzz-corpus/` are ignored by git.

The script has no absolute machine paths. It uses the repo root from
`git rev-parse`, then picks the build directory from `BUILD_DIR` if set, else
`build/` (the canonical build tree in the README). If neither exists, the script
exits with an error listing the `build*` directories it found instead of failing
later inside CMake. It also warns if the chosen build tree uses a non-clang
compiler, since the `pdf_fuzzer` target needs clang for
`-fsanitize=fuzzer,address`. Create the build once with the README's
**"Verified working command (this machine)"** under *Build the fuzzer* -- it lists
the exact flags this environment needs (CMake policy, GCC 11 headers, fontconfig)
and their troubleshooting Issues. Or point at an existing clang libFuzzer+ASan tree:

```bash
BUILD_DIR=build ./scripts/run_fuzz_phase.sh smoke
```

The smoke build step is:

```bash
cmake --build "$BUILD_DIR" --target verify_serializer verify_cff pdf_fuzzer
```

CMake regenerates protobuf C++ sources from the `.proto` schema when the schema
is newer than the generated files.
