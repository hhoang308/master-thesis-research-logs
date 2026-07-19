# PoDoFo 0.9.7 - allocation-size crash candidate

**Target:** PoDoFo 0.9.7 via the libFuzzer harness.
**Status:** Needs triage against a debug/ASan PoDoFo build.

## Material

- `crash-input.txtpb` - protobuf text form of the minimized input, suitable for inspection.
- `crash-input.pdf` / `crash.pdf` - serialized PDF repro material kept for direct target replay.
- `crash-input.txt` - original one-byte libFuzzer companion artifact.

## Reproduce

```sh
REPO_ROOT=$(git rev-parse --show-toplevel)
RESEARCH=$REPO_ROOT/research
PODOFO_FUZZER=${PODOFO_FUZZER:-$RESEARCH/thesis/pdf-proto-schema/build/pdf_fuzzer_podofo}
ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=1 "$PODOFO_FUZZER" crash-input.txtpb
```

If the harness expects binary protobuf rather than text protobuf on the current build, replay
`crash-input.pdf` directly with the PoDoFo CLI/tool under triage instead.

## Notes

This bucket is kept as a full finding artifact because it represents the same allocation-size
failure pattern discussed in the xpdf `GMemException` notes. Classify severity only after
confirming whether the allocation size is malformed input handling, an expected OOM guard, or a
real parser arithmetic bug.
