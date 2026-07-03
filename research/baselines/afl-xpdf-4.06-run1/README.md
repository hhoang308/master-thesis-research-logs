# Baseline Run 1 -- AFL++ on xpdf 4.06 (pdftotext)

## Setup

| Parameter | Value |
|---|---|
| Tool | AFL++ 4.41a |
| Target | xpdf 4.06 `pdftotext` (instrumented build) |
| Seed corpus | `research/seeds/cmin` (minimized with `afl-cmin`) |
| Dictionary | `research/seeds/pdf.dict` |
| Sanitizers | none (coverage-only build) |
| Command | `afl-fuzz -i seeds/cmin -o experiments/afl-out -x seeds/pdf.dict -- pdftotext @@ /dev/null` |

> **Note (post-reorg):** the reproduction command blocks below were written against the
> pre-reorganization layout. Paths have since moved: `pdf-seeds-cmin` → `seeds/cmin`,
> `pdf.dict` → `seeds/pdf.dict`, `AFLplusplus/` → `reference/AFLplusplus/`,
> `xpdf-4.06/` → `thesis/xpdf-4.06/`, `afl-out` → `experiments/afl-out`. The verbatim
> original command (with absolute paths) is preserved in `fuzzer_stats`.

## How to run in the background

AFL++ prints an interactive TUI to the terminal. To run it in the background on a
shared server without keeping a terminal open, use one of these two methods:

**Option A -- nohup (simplest)**

```bash
cd research

nohup /home/hoangnh8/master-thesis-research-logs/research/AFLplusplus/afl-fuzz \
    -i pdf-seeds-cmin \
    -o afl-out \
    -x pdf.dict \
    -- xpdf-4.06/build-instrumented/xpdf/pdftotext @@ /dev/null \
    > afl-fuzz.log 2>&1 &

echo "PID: $!"
```

- `nohup` keeps the process alive after the terminal closes.
- `> afl-fuzz.log 2>&1` redirects all output (stdout + stderr) to a log file instead
  of creating `nohup.out` in the home directory.
- `&` sends it to the background immediately.
- Save the printed PID so you can check or kill it later.

**Option B -- tmux (recommended for shared servers)**

```bash
tmux new-session -d -s fuzzing

tmux send-keys -t fuzzing "
cd /home/hoangnh8/master-thesis-research-logs/research && \
AFL_SKIP_CPUFREQ=1 \
/home/hoangnh8/master-thesis-research-logs/research/AFLplusplus/afl-fuzz \
    -i pdf-seeds-cmin \
    -o afl-out \
    -x pdf.dict \
    -- xpdf-4.06/build-instrumented/xpdf/pdftotext @@ /dev/null
" ENTER
```

- `tmux new-session -d -s fuzzing` creates a detached session named `fuzzing`.
- You can reattach anytime with `tmux attach -t fuzzing` to see the live TUI.
- Detach again with `Ctrl+B` then `D`.

## How to monitor without the TUI

AFL++ writes a stats snapshot to `afl-out/default/fuzzer_stats` every few seconds.
Read it directly to check status from any terminal:

```bash
# one-shot status check
cat research/afl-out/default/fuzzer_stats

# key fields to watch
grep -E 'run_time|execs_done|execs_per_sec|corpus_count|bitmap_cvg|edges_found|saved_crashes|saved_hangs|peak_rss_mb' \
    research/afl-out/default/fuzzer_stats
```

**What each field means:**

| Field | What to watch for |
|---|---|
| `run_time` | seconds elapsed |
| `execs_done` | total executions |
| `execs_per_sec` | throughput -- big drop means a hang input is being processed |
| `corpus_count` | number of inputs in the queue |
| `bitmap_cvg` | % of coverage bitmap hit -- plateauing means diminishing returns |
| `edges_found` | unique edges discovered |
| `saved_crashes` | non-zero means a crash was found -- check `afl-out/default/crashes/` |
| `saved_hangs` | inputs that timed out -- worth triaging later |
| `peak_rss_mb` | memory usage -- watch this on shared servers, high value caused this run to OOM |
| `cycles_done` | how many full corpus passes completed |

**Live watch (refresh every 10 seconds):**

```bash
watch -n 10 "grep -E 'run_time|execs_per_sec|corpus_count|bitmap_cvg|edges_found|saved_crashes|saved_hangs|peak_rss_mb' \
    research/afl-out/default/fuzzer_stats"
```

**Check if process is still alive:**

```bash
ps aux | grep afl-fuzz | grep -v grep
# if empty -- the fuzzer has stopped
```

**Check why it stopped (OOM):**

```bash
# peak_rss_mb in fuzzer_stats shows max memory before death
grep peak_rss_mb research/afl-out/default/fuzzer_stats

# system journal may show OOM kill event
journalctl -k | grep -i "oom\|killed process" | tail -5
```

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
