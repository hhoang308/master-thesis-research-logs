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
BUILD_DIR=build-env-check-clang18 ./scripts/run_fuzz_phase.sh smoke
SHORT_SECONDS=7200 MAX_LEN=65536 ./scripts/run_fuzz_phase.sh short
LONG_SECONDS=86400 CORPUS_DIR=fuzz-corpus/main ./scripts/run_fuzz_phase.sh long
```

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

- The fuzzer exits 0 after `SHORT_SECONDS`.
- Coverage/feature counts grow or remain explainably stable.
- Inputs are not rejected before the target area most of the time.
- Any crash is triaged before merging its corpus into a long run.

Long passes when:

- The run finishes its configured time budget or is intentionally stopped.
- Crashes are reproducible under the agreed triage path.
- The log records commit, command, corpus, sanitizer options, and run directory.

## Default Sanitizer Options

The script uses:

```text
ASAN_OPTIONS=detect_container_overflow=0:detect_leaks=0
```

This matches the project README: it avoids a protobuf mutation-layer false
positive and LeakSanitizer teardown noise while keeping normal ASan bug classes
such as heap overflows and UAFs enabled.

## Portability

The runbook and script are intended to be committed. Generated run artifacts are
not: `fuzz-runs/` and `fuzz-corpus/` are ignored by git.

The script has no absolute machine paths. It uses the repo root from
`git rev-parse`, then picks `BUILD_DIR` from the environment, `build/` if it
exists, or `build-env-check-clang18/` as the fallback because that is the current
local build tree. If another machine uses a different CMake build directory, set:

```bash
BUILD_DIR=build ./scripts/run_fuzz_phase.sh smoke
```

The smoke build step is:

```bash
cmake --build "$BUILD_DIR" --target verify_serializer verify_cff pdf_fuzzer
```

CMake regenerates protobuf C++ sources from the `.proto` schema when the schema
is newer than the generated files.
