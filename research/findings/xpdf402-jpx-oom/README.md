# xpdf 4.02 - JPX OOM artifact

**Target:** xpdf 4.02 via the libFuzzer/LPM harness.
**Status:** OOM finding candidate; keep as repro material, not a confirmed memory-safety bug.

## Material

- `oom-artifact.lpm` - libFuzzer artifact saved from the run.
- `oom-c475dbfe21b10589878b8f7509930dfc4897be82` - duplicate preserved with the original
  content-addressed artifact name.

## Reproduce

```sh
REPO_ROOT=$(git rev-parse --show-toplevel)
RESEARCH=$REPO_ROOT/research
XPDF_FUZZER=${XPDF_FUZZER:-$RESEARCH/thesis/pdf-proto-schema/build-402/pdf_fuzzer}
ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=1 "$XPDF_FUZZER" oom-artifact.lpm
```

## Notes

The interesting path is JPX image decoding. Treat this as a resource-exhaustion artifact until a
bounded-memory replay shows whether the OOM comes from expected rejection of a large image,
malformed dimension handling, or a parser bug worth reporting.
