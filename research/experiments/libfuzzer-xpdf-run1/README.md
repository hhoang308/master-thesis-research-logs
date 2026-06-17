# libFuzzer + libprotobuf-mutator -- xpdf 4.06 -- Run 1

## Setup

| Field | Value |
|---|---|
| Target | xpdf 4.06 `pdftotext` (via `PDFDoc`) |
| Fuzzer | libFuzzer + libprotobuf-mutator |
| Schema | `pdf-proto-schema/pdf.proto` Stage 1+2 (document skeleton + FlateDecode stream) |
| Binary | `pdf-proto-schema/build/pdf_fuzzer` |
| Sanitizer | ASan |
| Max time | 86400s (24h) |
| Start | (see fuzzer.log first line) |

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
