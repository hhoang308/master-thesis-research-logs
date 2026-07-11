# libFuzzer + libprotobuf-mutator -- xpdf 4.06 -- Run 1

## Setup

| Field | Value |
|---|---|
| Target | xpdf 4.06 `pdftotext` (via `PDFDoc`) |
| Fuzzer | libFuzzer + libprotobuf-mutator |
| Schema | `schema/pdf-proto/pdf.proto` Stage 1+2 (document skeleton + FlateDecode stream) |
| Binary | `schema/pdf-proto/build/pdf_fuzzer` |
| Sanitizer | ASan |
| Max time | 86400s (24h) configured; stopped early at ~17h |
| Start | 2026-06-17 15:12 +07 |
| Stopped | 2026-06-18 ~08:12 +07 (manual stop -- coverage plateaued, schema too limited) |

## Results -- Run 1

| Metric | Value |
|---|---|
| Actual runtime | ~17 hours |
| Total executions | 36,823,712 |
| Avg exec/s | 602 |
| Peak RSS | 627 MB |
| Final coverage | 729 edges / 27,712 counters (~2.6%) |
| Final feature count (ft) | 3,143 |
| Final corpus size | 274 files / 97 KB |
| Crashes | 0 |
| Hangs | 0 |
| coverage.log rows | 1,017 snapshots |

### Observation

Coverage reached 729 edges within the first 30 seconds and **never grew further** for the
entire 17-hour run. All activity after the first minute was `REDUCE` lines (corpus
minimization), never `NEW` (new coverage). This confirms that the Stage 1+2 schema
(MediaBox + FlateDecode content stream only) exhausts all reachable code paths almost
immediately.

Root cause: xpdf's font parser, image decoder, color space handler, and annotation code --
which together account for the majority of xpdf's codebase -- are unreachable because the
schema generates no Font, Image, ColorSpace, or Annotation objects.

### What to fix before Run 2

1. Add `sint32 length_delta` to `ContentStream` -- enables /Length mismatch mutations.
2. Add `bool skip_compression` to `ContentStream` -- enables corrupt zlib data into FlateStream.
3. Remove width/height clamp in serializer -- enables NaN/0/negative MediaBox mutations.
4. Add Font object (Type1 dummy) to schema -- unlocks font parser code paths.

See `master-thesis.md` section "Giai đoan 2b" and "Giai đoan 3 P1" for details.

## How to start

```bash
cd research/experiments/libfuzzer-xpdf-run1
bash run.sh
```

## How to monitor

```bash
# Live fuzzer output
tail -f fuzzer.log

# Coverage snapshots (updated every 60s)
tail coverage.log

# Latest coverage number only
grep 'cov:' fuzzer.log | tail -1

# Check process is alive
kill -0 $(cat fuzzer.pid) && echo running || echo stopped
```

## How to stop early

```bash
kill $(cat fuzzer.pid)
```

## Output files

| File | Description |
|---|---|
| `fuzzer.log` | Raw libFuzzer stderr (all output) |
| `coverage.log` | CSV: timestamp_unix, exec_count, cov_edges, ft_count, corpus_count, rss_mb -- one row per minute |
| `corpus/` | Grows during run -- protobuf-format seeds libFuzzer finds interesting |
| `artifacts/` | Crashes and timeouts saved here |
| `fuzzer.pid` | PID of running fuzzer (deleted on exit) |

## coverage.log format

```
# timestamp_unix,exec_count,cov_edges,ft_count,corpus_count,rss_mb
1750000000,12345,716,1234,45,123
```

- `cov_edges`: number of edge coverage counters hit -- this is the main metric for comparison vs AFL++ baseline (AFL++ reports 15,006 / 31,464 edges = 47.74% at 93h)
- `ft_count`: feature count (finer than edges, internal to libFuzzer)

## Plotting coverage over time (Python)

```python
import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("coverage.log", comment="#",
                 names=["ts","exec","cov","ft","corp","rss"])
df["hour"] = (df["ts"] - df["ts"].iloc[0]) / 3600

plt.plot(df["hour"], df["cov"])
plt.xlabel("Time (hours)")
plt.ylabel("Edges covered")
plt.title("libFuzzer+protobuf -- xpdf run 1")
plt.savefig("coverage_curve.png", dpi=150)
```
