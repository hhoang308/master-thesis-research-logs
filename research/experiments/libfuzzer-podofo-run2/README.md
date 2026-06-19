# libFuzzer + libprotobuf-mutator -- PoDoFo 0.9.7 -- Run 2

## Setup

| Field | Value |
|---|---|
| Target | PoDoFo 0.9.7 `PdfMemDocument::LoadFromBuffer` (in-memory parse) |
| Fuzzer | libFuzzer + libprotobuf-mutator |
| Schema | `pdf-proto-schema/pdf.proto` Stage 1+2 **+ 4 gap fixes** (shared with xpdf) |
| Binary | `pdf-proto-schema/build/pdf_fuzzer_podofo` |
| Sanitizer | ASan |
| Max time | 86400s (24h) -- ran to completion |
| Corpus seed | run1 final corpus (52 files) |
| Artifacts | `-artifact_prefix=artifacts/` (crash inputs preserved) |
| Start | 2026-06-18 09:43 +07 |
| Finished | 2026-06-19 09:43 +07 (clean, full 24h) |

## Results -- Run 2

| Metric | Value |
|---|---|
| Actual runtime | 86,401 s (full 24h) |
| Total executions | 259,470,607 |
| Avg exec/s | 3,003 |
| Peak RSS | 738 MB |
| Final coverage | **176 edges** / 1,172 counters (~15%) |
| Final feature count (ft) | 847 |
| Final corpus size | 85 files / 30 KB |
| new_units_added | 1,477 |
| Crashes | 0 |
| Hangs | 0 |
| coverage.log rows | 1,344 snapshots |

## Run 1 vs Run 2

| Metric | Run 1 | Run 2 |
|---|---|---|
| Coverage (edges) | 161 | **176** (+15) |
| Total executions | 256.9M | 259.5M |
| Crashes | 0 | 0 |

PoDoFo runs ~5x faster than the xpdf harness (in-memory parse, no temp file), so it reaches
far more executions (259M) in the same wall-clock. Coverage still plateaus almost
immediately at 176 edges -- the strict PoDoFo parser rejects most malformed structures
early, so the reachable surface under the current schema is small and the corpus stays tiny
(85 files).

### Observation

The instrumented module is small (1,172 counters total) because the harness links only the
PoDoFo parsing core, not rendering. 176/1172 ~ 15% of that core is exercised. As with xpdf,
the ceiling is structural: no Font/Image/ColorSpace/Annotation objects in the schema means
the corresponding PoDoFo handlers are never entered. The Stage 3 schema expansion (P1 Font
first) is the lever to move this.

## Note on `fuzzer.log`

The PoDoFo harness prints the serialized PDF skeleton (`<</Root 1 0 R/Size N>>`) to stderr on
every execution, so the raw log was **5.8 GB** (259M lines). It has been trimmed to the
analytically useful lines only (INFO header, periodic `cov:`/`ft:` stat lines, the discovered
dictionary, and the final `stat::` block). The per-execution skeleton dumps were dropped --
they carry no analytical value. `coverage.log` (the CSV time-series) is unaffected.

## Files

| File | Purpose |
|---|---|
| `run.sh` | exact command used (reproducibility) |
| `coverage.log` | CSV time-series: `timestamp,exec,cov,ft,corp,rss` -- use for plotting |
| `fuzzer.log` | trimmed libFuzzer stderr (stats + dictionary + final block; per-exec spam removed) |
| `corpus/` | evolved corpus (85 files) |
| `artifacts/` | crash/timeout inputs (empty -- no crashes) |

## How to start

```bash
cd research/experiments/libfuzzer-podofo-run2
bash run.sh
```
