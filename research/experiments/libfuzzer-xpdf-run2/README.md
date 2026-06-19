# libFuzzer + libprotobuf-mutator -- xpdf 4.06 -- Run 2

## Setup

| Field | Value |
|---|---|
| Target | xpdf 4.06 `pdftotext` (via `PDFDoc`) |
| Fuzzer | libFuzzer + libprotobuf-mutator |
| Schema | `pdf-proto-schema/pdf.proto` Stage 1+2 **+ 4 gap fixes** |
| Binary | `pdf-proto-schema/build/pdf_fuzzer` |
| Sanitizer | ASan |
| Max time | 86400s (24h) -- ran to completion |
| Corpus seed | run1 final corpus (274 files) |
| Artifacts | `-artifact_prefix=artifacts/` (crash inputs preserved) |
| Start | 2026-06-18 09:43 +07 |
| Finished | 2026-06-19 09:43 +07 (clean, full 24h) |

### Schema changes since Run 1 (the "4 gap fixes")

1. `sint32 length_delta` on `ContentStream` -- /Length over-read (+) and under-read (-) mutations.
2. `bool skip_compression` on `ContentStream` -- declare `/Filter /FlateDecode` but write raw bytes -> corrupt-zlib paths in `FlateStream`.
3. Removed MediaBox width/height clamp in serializer -- allows NaN / 0 / negative / huge floats to reach xpdf geometry code.
4. `repeated ContentStream` per page -- `/Contents [N 0 R M 0 R]` arrays; xpdf concatenates streams before parsing.

## Results -- Run 2

| Metric | Value |
|---|---|
| Actual runtime | 86,401 s (full 24h) |
| Total executions | 49,251,822 |
| Avg exec/s | 570 |
| Peak RSS | 759 MB |
| Final coverage | **756 edges** / 27,768 counters (~2.7%) |
| Final feature count (ft) | 3,557 |
| Final corpus size | 346 files / 135 KB |
| new_units_added | 6,306 |
| Crashes | 0 |
| Hangs | 0 |
| coverage.log rows | 1,440 snapshots |

## Run 1 vs Run 2

| Metric | Run 1 | Run 2 |
|---|---|---|
| Coverage (edges) | 729 | **756** (+27) |
| Total executions | 36.8M | 49.3M |
| Runtime | ~17h (stopped early) | 24h (full) |
| Crashes | 0 | 0 |

The 4 gap fixes produced a clean, reproducible **+27 edges**. Coverage reached 756
within the first minute and stayed flat for the remaining ~24h -- only `REDUCE` lines,
never `NEW`. `new_units_added: 6306` reflects new *feature buckets* (edge hit-counts),
not new edges: `ft` kept creeping up (3534 -> 3557) while `cov` was frozen.

### Observation

Same structural ceiling as Run 1, just shifted up by the gap fixes. The remaining bulk of
xpdf -- font parser (`GfxFont`/`makeFont`), image decoder, color space, annotations -- is
still unreachable because the schema emits no Font/Image/ColorSpace/Annotation objects.
This is the evidence motivating the Stage 3 schema expansion (P1 Font first).

## Files

| File | Purpose |
|---|---|
| `run.sh` | exact command used (reproducibility) |
| `coverage.log` | CSV time-series: `timestamp,exec,cov,ft,corp,rss` -- use for plotting |
| `fuzzer.log` | libFuzzer stderr (stat lines + discovered dictionary + final stats) |
| `corpus/` | evolved corpus (346 files) |
| `artifacts/` | crash/timeout inputs (empty -- no crashes) |

## How to start

```bash
cd research/experiments/libfuzzer-xpdf-run2
bash run.sh
```
