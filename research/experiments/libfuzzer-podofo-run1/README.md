# libFuzzer + libprotobuf-mutator -- PoDoFo 0.9.7 -- Run 1

## Setup

| Field | Value |
|---|---|
| Target | PoDoFo 0.9.7 (`PdfMemDocument::LoadFromBuffer` + `GetPageCount`) |
| Fuzzer | libFuzzer + libprotobuf-mutator |
| Schema | `pdf-proto-schema/pdf.proto` Stage 1+2 (document skeleton + FlateDecode stream) |
| Binary | `pdf-proto-schema/build/pdf_fuzzer_podofo` |
| Sanitizer | ASan |
| Max time | 86400s (24h) configured; stopped early at ~15h |
| Start | 2026-06-17 16:39 +07 |
| Stopped | 2026-06-18 ~08:12 +07 (manual stop -- coverage plateaued, schema too limited) |

## Results -- Run 1

| Metric | Value |
|---|---|
| Actual runtime | ~15 hours |
| Total executions | 256,904,823 |
| Avg exec/s | 4,595 |
| Peak RSS | 625 MB |
| Final coverage | 161 edges (PoDoFo instrumented independently from xpdf -- not comparable) |
| Final feature count (ft) | 613 |
| Final corpus size | 52 files / 11 KB |
| Crashes | 0 |
| Hangs | 0 |
| coverage.log rows | 881 snapshots |

### Observation

Coverage reached 161 edges within the first 30 seconds and **never grew further** for the
entire 15-hour run. At 256M executions total, the fuzzer exhausted the state space of the
current schema completely. The `pulse` lines every 2^N executions confirmed zero new edges
throughout the run.

PoDoFo ran 7x more executions than xpdf (256M vs 36M) in the same time window due to
`LoadFromBuffer` eliminating temp-file I/O overhead (~4,600 exec/s vs ~600 exec/s).
Despite the higher throughput, coverage did not improve -- confirming the bottleneck is
schema expressiveness, not execution speed.

### Observation on corpus size

PoDoFo corpus (52 files / 11 KB) is much smaller than xpdf corpus (274 files / 97 KB).
This reflects that PoDoFo's parser is more strict -- fewer structural variations in the
generated PDFs produce distinct coverage, so fewer corpus entries are retained.

### What to fix before Run 2

Same schema fixes as xpdf run (length_delta, skip_compression, remove clamp, add Font).
Additionally, consider exploring PoDoFo-specific APIs beyond GetPageCount -- e.g.,
iterating annotations, reading stream content -- to expose more code paths per execution.

## Install PoDoFo

```bash
sudo apt install libpodofo-dev libpodofo0.9.7
```

Verify:

```bash
pkg-config --modversion libpodofo
```

## Build pdf_fuzzer_podofo

```bash
cd research/pdf-proto-schema
mkdir -p build && cd build
cmake .. \
  -DCMAKE_CXX_COMPILER=clang++-14 \
  -DCMAKE_C_COMPILER=clang-14 \
  -DCMAKE_EXE_LINKER_FLAGS="-L/usr/lib/gcc/x86_64-linux-gnu/11"
make pdf_fuzzer_podofo -j$(nproc)
```

Expected output:

```
-- PoDoFo found () -- pdf_fuzzer_podofo will be built
[100%] Built target pdf_fuzzer_podofo
```

## Key difference vs xpdf harness

The PoDoFo harness uses `LoadFromBuffer` to feed bytes directly in memory -- no temp file is
written to disk. This is possible because PoDoFo's API accepts a raw byte buffer:

```cpp
PoDoFo::PdfMemDocument pdf;
pdf.LoadFromBuffer(bytes.data(), static_cast<long>(bytes.size()));
pdf.GetPageCount();
```

Result: ~6,300 exec/s vs ~1,800 exec/s for the xpdf harness (2.6x faster).

## How to start

```bash
cd research/experiments/libfuzzer-podofo-run1
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
1750000000,208832,161,561,67,543
```

- `cov_edges`: edge coverage counters hit -- main metric for comparison
- `ft_count`: feature count (finer-grained than edges, internal to libFuzzer)

Note: PoDoFo and xpdf edge counts are not directly comparable -- each binary is instrumented
independently. Compare each target's curve against its own AFL++ baseline (if one exists).

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
plt.title("libFuzzer+protobuf -- PoDoFo 0.9.7 run 1")
plt.savefig("coverage_curve.png", dpi=150)
```
