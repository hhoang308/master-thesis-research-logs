# AFL++ structure-aware fuzzing — xpdf 4.02 (libprotobuf-mutator custom mutator)

Structure-aware AFL++ campaign against **xpdf 4.02**. Instead of AFL flipping raw PDF
bytes, a **libprotobuf-mutator (LPM)** custom mutator mutates a `PdfDocument` protobuf and a
serializer turns it into a real, structurally-valid PDF that the instrumented xpdf target
parses. This keeps mutations inside the PDF grammar, so far more inputs survive the parser
and reach the deep font/image/stream code paths.

```
seed .pb  ──►  afl_custom_fuzz (LPM mutates the PdfDocument proto)
                    │
                    ▼
          afl_custom_post_process ── SerializePdf() ──►  real PDF bytes
                    │
                    ▼
          afl_harness_xpdf  ──►  xpdf 4.02 PDFDoc::displayPages()  (instrumented)
```

All the mutation/serialization logic lives in **`afl_pdf_mutator.so`** (the AFL++ custom
mutator). The **`afl_harness_xpdf`** target is just instrumented xpdf that renders whatever
PDF the mutator hands it.

## Layout

| Path | What |
|------|------|
| `run.sh` | Launches the AFL++ campaign (custom-mutator mode). `./run.sh [seconds]`, default 24h. |
| `seeds/` | 1000 binary `PdfDocument` protos (`gen_000000.pb …`), the AFL seed corpus. |
| `out/` | AFL++ output (queue / crashes / hangs / stats). Created at run time. |

The two build artifacts live in the shared tooling dir `research/schema/pdf-proto/`:
`afl_pdf_mutator.so` (the mutator) and `build-afl/afl_harness_xpdf` (the target).

---

## Toolchain (this host)

- **AFL++** `++4.41a` at `~/.local/bin` (clang-18 conda backend, PCGUARD; `afl-clang-lto` N/A).
- **protobuf/abseil** from conda `~/miniconda3` (protoc 33.5, libprotobuf 6.33.5).
- **clang-14** (`/usr/bin/clang++-14`) — used **only** for the `.so` (matches its GCC-11 /
  llvm-14 include hacks; do **not** swap in conda clang-18).
- `afl-fuzz` links conda's `libpython3.13` → **`LD_LIBRARY_PATH=~/miniconda3/lib` is required**
  to launch it (handled by `run.sh`).

---

## Reproducing the build from scratch

Prelude:

```bash
REPO_ROOT=$(git rev-parse --show-toplevel)
ROOT=$REPO_ROOT/research
PROTO=$ROOT/schema/pdf-proto
XPDF402=$ROOT/thesis/xpdf-4.02          # 4.02 source root (CMakeLists.txt is directly here)
PB=${PB_PREFIX:-$HOME/miniconda3}
AFL_CC=${AFL_CC:-$HOME/.local/bin/afl-clang-fast}
AFL_CXX=${AFL_CXX:-$HOME/.local/bin/afl-clang-fast++}
EXP=$ROOT/experiments/afl-xpdf402-proto-run1
```

### A. Build the custom mutator `.so` (serializer + libprotobuf-mutator)

The mutator packs the LPM engine **and** the PDF serializer into one shared object that
afl-fuzz `dlopen`s. It is built **non-ASan** (`-O1`, `-fPIC -shared`) — it runs inside
afl-fuzz, not inside the instrumented target — and links conda's protobuf/abseil with an
rpath so it resolves at load time.

```bash
cd "$PROTO"
bash build_mutator.sh
```

`build_mutator.sh` (already patched — see *Fixes* below) does three things:

1. `protoc --cpp_out=. pdf.proto cff.proto` → regenerates `pdf.pb.*` / `cff.pb.*` locally
   (they are `.gitignore`d and otherwise exist only in the CMake build tree).
2. Compiles with **clang++-14**:
   `afl_mutator.cpp serializer.cpp cff_serializer.cc pdf.pb.cc cff.pb.cc` +
   the LPM core (`libprotobuf-mutator/src/{mutator,binary_format,text_format,utf8_fix}.cc`).
3. Links `-lprotobuf $ABSL -lutf8_range -lutf8_validity -lz -lpthread`, `-Wl,-rpath,$PB/lib`.

Verify (the script prints this):

```
nm -D afl_pdf_mutator.so | grep afl_custom_    # -> init / fuzz / post_process / deinit
nm -D afl_pdf_mutator.so | grep -c __asan      # -> 0
ldd afl_pdf_mutator.so | grep 'not found'      # -> (empty)
```

### B. Build the AFL target harness (instrumented xpdf 4.02)

```bash
cd "$PROTO"
export LD_LIBRARY_PATH="$PB/lib:$LD_LIBRARY_PATH"
cmake -S . -B build-afl \
  -DCMAKE_C_COMPILER="$AFL_CC" \
  -DCMAKE_CXX_COMPILER="$AFL_CXX" \
  -DCMAKE_PREFIX_PATH="$PB" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DAFL_BUILD=ON \
  -DXPDF_LEGACY_DISPLAYPAGES=ON \
  -DXPDF_SRC="$XPDF402"
cmake --build build-afl -j"$(nproc)" --target afl_harness_xpdf
# -> $PROTO/build-afl/afl_harness_xpdf
```

- `AFL_BUILD=ON` selects the AFL harness target (afl-clang-fast, `-O2 -g`, no ASan/libFuzzer).
- `XPDF_LEGACY_DISPLAYPAGES=ON` is **mandatory** for 4.02 (its `displayPages()` has no
  `LocalParams*` arg). Paired with the harness fix below.
- `XPDF_SRC=$XPDF402` — the 4.02 root (**not** `.../source`; that layout is the 4.06 default).
- `CMAKE_PREFIX_PATH=$PB` — the CMakeLists runs `find_package(Protobuf CONFIG)` unconditionally.
- `CMAKE_POLICY_VERSION_MINIMUM=3.5` — conda's CMake 4.x rejects xpdf 4.02's old
  `cmake_minimum_required`; this flag lets it configure.

### C. Generate the 1000 seed protos

```bash
mkdir -p "$EXP/seeds"     # gen_corpus does NOT create the dir itself
LD_LIBRARY_PATH="$PB/lib" "$PROTO/build-402/gen_corpus" "$EXP/seeds" 1000 12345 --clean --binary
```

- `--binary` → binary wire-format `.pb` (what `afl_custom_fuzz` parses). `--clean` → valid
  field values (no malformation toggles); the mutator introduces the malformations at runtime.
- Deterministic (seed `12345`). Rebuild `gen_corpus` only if `pdf.proto`/`gen_corpus.cpp`
  change: `cmake --build build-402 --target gen_corpus`.

### D. Run

```bash
./run.sh 60       # 60s smoke test (confirm the pipeline works)
./run.sh 86400    # 24h campaign
# monitor:
LD_LIBRARY_PATH=$PB/lib ${AFL_WHATSUP:-$HOME/.local/bin/afl-whatsup} out
# stop:
kill $(cat afl.pid)
```

---

## Fixes applied to the shared tooling (backward-compatible with the 4.06 run)

Three staleness bugs in `research/schema/pdf-proto/` were fixed so the 4.02 build is correct.
None of them change 4.06 behaviour.

1. **`build_mutator.sh`** — added `cff_serializer.cc cff.pb.cc` to the compile list (the
   serializer now `#include`s `cff_serializer.h` and `pdf.proto` imports `cff.proto`, so the
   `.so` would fail to link without them), added the `protoc` regeneration step, and added
   `-lutf8_range -lutf8_validity` (protobuf 6.x split these into their own libs).
2. **`afl_harness_xpdf.cpp`** — wrapped `displayPages()` in `#ifdef XPDF_LEGACY_DISPLAYPAGES`
   (4.02 signature) / `#else` (4.06 signature), mirroring `harness.cpp`. Without this the 4.02
   build silently mis-renders (the page count lands in the DPI argument).
3. `pdf.pb.*` / `cff.pb.*` are generated into the source dir by step A (they are gitignored and
   were previously only in `build-402/`).

---

## Critical: `AFL_POST_PROCESS_KEEP_ORIGINAL=1` (do not remove)

This is the single most important flag for this pipeline; `run.sh` sets it. **Without it,
afl-fuzz corrupts its own heap and dies within seconds** (`malloc(): smallbin double linked
list corrupted`, or a bare SIGSEGV, after a few thousand execs).

Why: with a `post_process` mutator, AFL by default stores the **post-processed output** — the
serialized *PDF* — as the queue entry (afl-fuzz-run.c saves `*mem` after it has been swapped to
the post-processed buffer). Those PDF queue entries then get fed back into `afl_custom_fuzz` as
`buf`/`add_buf` (splice), where `ParsePartialFromArray` interprets adversarial PDF bytes as
protobuf wire-format. protobuf's parser occasionally reads a bogus length/field from the PDF and
makes a pathological allocation that corrupts the glibc heap. `AFL_POST_PROCESS_KEEP_ORIGINAL=1`
tells AFL to keep the **original proto** in the queue, so every `buf`/`add_buf` handed back to the
mutator is a valid `PdfDocument` proto — the mutator's own format. (Diagnosed by capturing the
crashing `add_buf`, which began with `%PDF-1.4`.)

Verified: with the flag, a smoke run reached **~693k execs / ~1,870 queue items / 2,566 edges /
10% coverage in 90 s with zero fuzzer crashes**; without it, it aborted at ~7,700 execs.

## Design notes

- **`AFL_CUSTOM_MUTATOR_ONLY=1`** — AFL's byte-level havoc/splice is disabled; all mutation is
  structural (via LPM). This is why seeds must be valid binary protos.
- **CmpLog is intentionally off** — comparison-solving on the *serialized* PDF bytes is useless
  because the byte layout is regenerated on every mutation.
- **No ASan on this route** — the `.so` and the harness are both non-sanitized, so the libFuzzer
  pipeline's `ASAN_OPTIONS=detect_container_overflow=0:...` does not apply here. Real crashes
  surface as SIGSEGV/SIGABRT and AFL catches them. For memory-safety triage of a crashing input,
  reproduce it as a real PDF (`build-402/proto2pdf <crash.pb> crash.pdf`) and run it through the
  separate gcc-11 ASan xpdf build under `thesis/xpdf-4.02/build-asan/`.
- **Comparable to the byte-level AFL baseline** — same AFL engine and same xpdf instrumentation
  as `experiments/afl-xpdf402-cve35376-seed`; only the mutator differs.
