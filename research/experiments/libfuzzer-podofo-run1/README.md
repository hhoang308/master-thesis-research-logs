# libFuzzer + libprotobuf-mutator -- PoDoFo 0.9.7 -- Run 1

## Setup

| Field | Value |
|---|---|
| Target | PoDoFo 0.9.7 (`PdfMemDocument::LoadFromBuffer` + `GetPageCount`) |
| Fuzzer | libFuzzer + libprotobuf-mutator |
| Schema | `pdf-proto-schema/pdf.proto` Stage 1+2 (document skeleton + FlateDecode stream) |
| Binary | `pdf-proto-schema/build/pdf_fuzzer_podofo` |
| Sanitizer | ASan |
| Max time | 86400s (24h) |
| Start | 2026-06-17 16:39 +07 |

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
