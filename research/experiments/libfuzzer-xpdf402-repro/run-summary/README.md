# xpdf 4.02 — stock-grammar baseline run (24h)

**Date:** 2026-06-27 → 2026-06-28 (completed full 24h)
**Target:** xpdf 4.02, libFuzzer harness `build-402/pdf_fuzzer` (ASan, `XPDF_LEGACY_DISPLAYPAGES=ON`).
**Engine:** libFuzzer + libprotobuf-mutator, fork mode.
**Purpose:** baseline — what does the *current* (stock) `pdf.proto` schema reach when fuzzed
with plain protobuf-tree mutation, before adding weakness-class-directed grammar fields.

## Command
```
ASAN_OPTIONS=detect_container_overflow=0:detect_leaks=0 \
  pdf_fuzzer -fork=1 -ignore_ooms=1 -ignore_timeouts=1 -ignore_crashes=1 \
    -rss_limit_mb=1024 -max_total_time=86400 -artifact_prefix=crashes/ corpus/
```
Notes: `detect_container_overflow=0` suppresses the instrumented-pb.cc / non-instrumented
libprotobuf false positive. Genuine alloc/overflow bugs still fire (no `allocator_may_return_null`).
Fork mode recycles the child at the 1 GB RSS limit → bounds xpdf's per-iteration leak so a 24h
run completes on a memory-tight shared box (the prior single-process run aborted at ~11h on OOM).

## Result — NO CVE reproduced (clean negative baseline)
| Metric | Value |
|---|---|
| Wall time | 86,528 s (24.0 h), completed |
| Total executions | **15,853,035** |
| Final edge coverage | `cov: 27437` (of 29,021 instrumented counters) |
| Features (`ft`) | 25,469 |
| Final corpus | 5,073 (from 4,400 seeds) |
| **Real crashes (`crash-*`)** | **0** |
| **ASan memory-error signatures** | **0** |
| OOM artifacts | 50 (benign — JPX/large-alloc resource pressure, NOT a bug; see `../../../findings/xpdf402-jpx-oom/`) |
| Timeouts | 0 |

The `oom/timeout/crash: 46/0/107` counter in the log is fork-mode bookkeeping of child
restarts (each `-ignore_*` restart is bucketed), **not** memory-safety crashes. Ground truth =
the artifact dir (0 `crash-*`) and the log (0 AddressSanitizer error lines).

## Coverage curve (hourly)
Saturates almost immediately, then crawls: **+1.06%** edges from 1h→24h (27150 → 27437).
Full series in `coverage-progression.csv` (532 samples).

```
 1h cov=27150   7h cov=27300   13h cov=27393   19h cov=27416
 2h cov=27202   8h cov=27338   14h cov=27395   20h cov=27428
 3h cov=27240   9h cov=27347   15h cov=27398   21h cov=27432
 4h cov=27257  10h cov=27357   16h cov=27404   22h cov=27435
 5h cov=27279  11h cov=27378   17h cov=27404   23h cov=27436
 6h cov=27294  12h cov=27386   18h cov=27412   24h cov=27437
```

## Key finding: "reached but not triggered"
The run **did reach the vulnerable functions** of the target CVEs — it is NOT a reachability
problem (see `discovered-funcs.txt`, 423 unique NEW_FUNC):

- `FoFiType1C::getOp`, `::parse`, `::readTopDict`, `::readPrivateDict` — **CVE-2020-35376**
  crashing function (`getOp`, bad subroutine ref in Type1C charstring) is executed.
- `DCTStream::readHeader/readScanInfo/readBaselineSOF/readProgressiveSOF` — **CVE-2024-7868 /
  CVE-2022-24106** DCT paths executed.
- `JPXStream::readCodestream/readBoxes/getImageParams` — JPX path executed.
- `FoFiType1::parse/getEncoding/undoPFB` — Type1 (**CVE-2024-4141**) path executed.

Yet zero crashes. Conclusion: **plain protobuf mutation reaches the decoders but cannot
synthesize the specific malformed *structure* that triggers the bug** (e.g. a charstring with an
out-of-range subroutine index, a DCT header with an inconsistent interleaved flag). That gap —
reaching code vs. constructing the triggering structure — is precisely what the
weakness-class-directed grammar fields are designed to close. This is the baseline the grammar
treatment must beat.

## Files
- `coverage-progression.csv` — `time_s,execs,cov,ft,corpus,oom,timeout,crash` (532 rows; plot cov vs time_s).
- `discovered-funcs.txt` — 423 unique functions reached (NEW_FUNC), for the coverage-of-security-functions analysis.
- `../fuzzer.log` — the fork-mode driver log (123 KB; per-job logs went to /tmp, not retained).
