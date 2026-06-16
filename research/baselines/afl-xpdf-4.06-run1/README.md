# Baseline Run 1 -- AFL++ on xpdf 4.06 (pdftotext)

## Setup

| Parameter | Value |
|---|---|
| Tool | AFL++ 4.41a |
| Target | xpdf 4.06 `pdftotext` (instrumented build) |
| Seed corpus | `research/pdf-seeds-cmin` (minimized with `afl-cmin`) |
| Dictionary | `research/pdf.dict` |
| Sanitizers | none (coverage-only build) |
| Command | `afl-fuzz -i pdf-seeds-cmin -o afl-out -x pdf.dict -- pdftotext @@ /dev/null` |

## Final results

| Metric | Value |
|---|---|
| Total runtime | 93.2 hours (335,650 seconds) |
| Total executions | 109,107,952 |
| Avg exec/sec | 325 |
| Coverage (bitmap) | 47.74% |
| Edges found | 15,020 / 31,464 |
| Cycles completed | 2 |
| Corpus size | 31,208 inputs |
| Saved crashes | **0** |
| Saved hangs | 312 |
| Peak RSS | 3,988 MB |
| Stop reason | Killed by OOM -- peak RSS hit ~4 GB after 93h |

## Notes

- Coverage plateaued around 47.7% -- no new edges found in the final hours.
- 0 crashes after 109M executions confirms xpdf 4.06 is robust against dumb byte mutation on this corpus.
- 312 hangs saved in `hangs/` -- likely infinite loops or very deep recursion in edge cases. Worth triaging later.
- The queue (331 MB, 31,208 files) is stored locally at `research/afl-out/default/queue/` and is NOT committed to git due to size.

## Files in this directory

| File | Description |
|---|---|
| `fuzzer_stats` | Final AFL++ stats snapshot |
| `plot_data` | Coverage / corpus / exec data sampled every 5 seconds -- use for thesis graphs |
| `hangs/` | All 312 timeout-inducing inputs saved by AFL++ |

## How to use plot_data for graphs

Columns (from header line):
```
relative_time, cycles_done, cur_item, corpus_count, pending_total, pending_favs,
map_size, saved_crashes, saved_hangs, max_depth, execs_per_sec, total_execs,
edges_found, total_crashes, servers_count
```

Example: plot `edges_found` (col 13) vs `relative_time` (col 1) to get coverage-over-time curve.

```python
import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv('plot_data', comment='#', header=None)
df.columns = ['time','cycles','cur_item','corpus','pending','favs',
              'map_size','crashes','hangs','depth','eps','execs','edges','total_crashes','servers']
df['time_h'] = df['time'] / 3600

plt.plot(df['time_h'], df['edges'])
plt.xlabel('Time (hours)')
plt.ylabel('Edges found')
plt.title('AFL++ baseline -- xpdf 4.06 pdftotext')
plt.savefig('coverage_curve.png')
```
