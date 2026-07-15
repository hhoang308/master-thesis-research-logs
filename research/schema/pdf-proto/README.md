# PDF Proto Schema

A libFuzzer + libprotobuf-mutator pipeline for fuzzing xpdf with structure-aware PDF inputs.

## Pipeline overview

`pdf.proto` $\to$ libprotobuf-mutator mutates PdfDocument $\to$ `SerializePdf()` $\to$ raw PDF bytes $\to$ xpdf PDFDoc

## Files

| File | Purpose |
|------|---------|
| `pdf.proto` | Protobuf schema defining the PDF structure. libprotobuf-mutator uses this to generate and mutate valid `PdfDocument` messages. |
| `modules/` | CVE-specific structured grammar, serializer, verifier, and .pb seed. |
| `tools/` | Generic corpus, conversion, and validation tools. |
| `afl/` | AFL++ harness and custom-mutator build script. |
| `coverage/` | Coverage driver and comparison script. |
| `tests/` | Standalone serializer verification programs. |
| `*.pb.h` / `*.pb.cc` | C++ classes generated into the CMake build directory by `protoc`. Do not edit or commit manually. |
| `serializer.h` / `serializer.cpp` | Converts a `PdfDocument` proto message into raw PDF bytes. Assigns object numbers, computes xref offsets, and writes a valid `%%EOF` trailer. |
| `harness.cpp` | libFuzzer entry point. Uses `DEFINE_PROTO_FUZZER` to receive a mutated `PdfDocument`, calls `SerializePdf`, writes to a temp file, and feeds it to xpdf's `PDFDoc`. |
| `tests/verify_serializer.cpp` | Standalone test binary (no fuzzer, no xpdf). Constructs hardcoded documents and validates serializer output. |
| `CMakeLists.txt` | Build config for the fuzzer binary. Requires Clang, links xpdf as an object library, and compiles with `-fsanitize=fuzzer,address`. |
| `libprotobuf-mutator/` | Git submodule -- google/libprotobuf-mutator. Provides `DEFINE_PROTO_FUZZER` and the libprotobuf-mutator integration with libFuzzer. |

---

## How to create the base pipeline combining libFuzzer and libprotobuf-mutator

This section documents all the issues encountered and fixes applied when building
the pipeline on Ubuntu 22.04 with clang-14 and protobuf installed via miniconda.

### Issue 1 -- Missing `#include "pdf.pb.h"` in serializer.cpp

`serializer.cpp` uses `PdfDocument` and `Page` from the generated protobuf code but
did not include the generated header.

**Fix:** add to the top of `serializer.cpp`:

```cpp
#include "pdf.pb.h"
```

### Issue 2 -- Name collision between protobuf Catalog and xpdf Catalog

Both the protobuf-generated code and xpdf define a class named `Catalog` in the
global namespace. When `harness.cpp` includes both `pdf.pb.h` and xpdf's `PDFDoc.h`,
the compiler resolves xpdf's `catalog->findDest()` calls against the wrong class
and fails.

**Fix:** add a package declaration to `pdf.proto` so all generated types live in
the `pdf_proto` namespace:

```proto
package pdf_proto;
```

Then update all references in `serializer.h`, `serializer.cpp`, `harness.cpp`, and
`verify_serializer.cpp` to use the fully-qualified type:

```cpp
pdf_proto::PdfDocument
pdf_proto::Page
```

### Issue 3 -- clang++-14 auto-selects GCC 12 but only GCC 11 headers are installed

clang-14 detects the newest GCC installation on the system and uses its C++ standard
library headers. GCC 12 is installed but `libstdc++-12-dev` is not, so headers like
`<atomic>` are missing.

Verify which GCC clang-14 selected:
```bash
clang++-14 -v 2>&1 | grep "Selected GCC"
# Selected GCC installation: /usr/bin/../lib/gcc/x86_64-linux-gnu/12  <-- wrong
```

**Fix:** explicitly add GCC 11 C++ include paths in `CMakeLists.txt`:

```cmake
add_compile_options(
  -I/usr/include/c++/11
  -I/usr/include/x86_64-linux-gnu/c++/11)
```

### Issue 4 -- abseil (from miniconda protobuf) cannot find `sanitizer/common_interface_defs.h`

Protobuf v22+ depends on abseil. Abseil's `dynamic_annotations.h` includes
`<sanitizer/common_interface_defs.h>` when compiled with sanitizers. This header
lives in clang's resource directory but is not on the default include path.

Verify the header exists:
```bash
find $(clang-14 -print-resource-dir) -name "common_interface_defs.h"
# /usr/lib/llvm-14/lib/clang/14.0.0/include/sanitizer/common_interface_defs.h
```

**Fix:** add the clang-14 resource include directory explicitly (as `-I`, not
`-resource-dir` -- the latter replaces all runtime headers and breaks SSE
intrinsics):

```cmake
add_compile_options(-I/usr/lib/llvm-14/lib/clang/14.0.0/include)
```

### Issue 5 -- linker cannot find libstdc++

clang++-14 on this machine does not add `/usr/lib/gcc/x86_64-linux-gnu/11` to the
linker search path automatically. CMake's compiler test fails at configure time
before any `add_link_options` in CMakeLists.txt takes effect.

**Fix:** pass the linker path on the cmake command line so it applies during the
compiler test phase:

```bash
cmake .. -DCMAKE_EXE_LINKER_FLAGS="-L/usr/lib/gcc/x86_64-linux-gnu/11"
```

And mirror it in `CMakeLists.txt`:

```cmake
add_link_options(-L/usr/lib/gcc/x86_64-linux-gnu/11)
```

### Issue 6 -- `port/protobuf.h` not found when compiling harness.cpp

`libfuzzer_macro.h` (from libprotobuf-mutator) includes `port/protobuf.h` using a
path relative to the libprotobuf-mutator root. That root directory must be on the
compiler include path.

**Fix:** add the libprotobuf-mutator root to `include_directories` in
`CMakeLists.txt`:

```cmake
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/libprotobuf-mutator
  ...
)
```

### Issue 7 -- undefined reference to abseil symbols at link time

`${Protobuf_LIBRARIES}` from CMake's module-mode `find_package(Protobuf)` does not
carry transitive abseil dependencies. Linking fails with missing symbols like
`absl::log_internal::MakeCheckOpString`.

**Fix:** switch to CMake config mode and use the modern target:

```cmake
find_package(Protobuf REQUIRED CONFIG)  # CONFIG mode carries transitive deps

target_link_libraries(pdf_fuzzer
  ...
  protobuf::libprotobuf   # instead of ${Protobuf_LIBRARIES}
  ...
)
```

---

## Build verify_serializer (serializer validation only)

Build from the CMake tree so protobuf sources are generated into `build/`.

```bash
cd research/schema/pdf-proto
cmake -S . -B build \
  -DCMAKE_CXX_COMPILER=clang++-14 \
  -DCMAKE_C_COMPILER=clang-14 \
  -DCMAKE_EXE_LINKER_FLAGS="-L/usr/lib/gcc/x86_64-linux-gnu/11"
cmake --build build --target verify_serializer verify_cff

./build/verify_serializer > /tmp/test.pdf
pdfinfo /tmp/test.pdf
pdftotext /tmp/test.pdf -
```

Useful diagnostic targets:

```bash
cmake --build build --target \
  gen_corpus proto2pdf check_serializer measure_valid_rate \
  gen_cve_2020_35376_seed inspect_cff_seed

./build/gen_cve_2020_35376_seed /tmp/cve-2020-35376.txtpb
./build/inspect_cff_seed /tmp/cve-2020-35376.txtpb
./build/proto2pdf /tmp/cve-2020-35376.txtpb /tmp/cve-2020-35376.pdf
```

**Expected result:**

```
Custom Metadata: no
Metadata Stream: no
Tagged:          no
...
Pages:           1
Encrypted:       no
Page size:       612 x 792 pts (letter)
Page rot:        0
File size:       330 bytes
PDF version:     1.4
```

`pdftotext` produces empty output -- correct, because the page has no content stream.

---

## Build the fuzzer (pdf_fuzzer)

Requires clang-14, an xpdf source tree passed via `-DXPDF_SRC=...`, and protobuf from miniconda.

```bash
cd research/schema/pdf-proto
cmake -S . -B build \
  -DCMAKE_CXX_COMPILER=clang++-14 \
  -DCMAKE_C_COMPILER=clang-14 \
  -DXPDF_SRC=/path/to/xpdf-4.06 \
  -DCMAKE_EXE_LINKER_FLAGS="-L/usr/lib/gcc/x86_64-linux-gnu/11"

cmake --build build -j$(nproc)
```

## Run the fuzzer

```bash
cd research/schema/pdf-proto/build

# smoke test: 100 runs to confirm the pipeline works
./pdf_fuzzer -runs=100 -max_len=1024

# real fuzzing session: runs indefinitely, saves corpus
mkdir -p corpus
./pdf_fuzzer -max_len=65536 corpus/
```

**Expected result for smoke test (`-runs=100`):**

```
INFO: found LLVMFuzzerCustomMutator (...). Disabling -len_control by default.
INFO: Running with entropic power schedule (0xFF, 100).
INFO: Seed: ...
INFO: A corpus is not provided, starting from an empty corpus
#2    INITED cov: 15 ft: 16 corp: 1/1b ...
#3    NEW    cov: 628 ft: 636 ...
...
#100  DONE   cov: 664 ft: 1124 corp: 24/1539b lim: 1024 exec/s: 0 rss: 52Mb
Done 100 runs in 0 second(s)
```

Key things to verify in the output:
- `found LLVMFuzzerCustomMutator` -- confirms libprotobuf-mutator is active, not raw byte mutation
- `INITED` appears -- the fuzzer initialized successfully and fed at least one input to xpdf
- `cov` grows over runs -- xpdf is being exercised and new code paths are discovered
- No `CRASH` or `AddressSanitizer` lines -- the baseline PDF structure is valid

## Crash-triage caveat: single-input replay is unreliable

Running the binary in single-input replay mode (`./build/pdf_fuzzer <one_input>`) exits
**non-deterministically** with SIGSEGV (139) on the *same* input -- it returns 0, 139, 0, 0
across repeated runs. This is **not** a target crash: under gdb the input reports
`Executed ... in N ms` cleanly, and the failure is `LeakSanitizer has encountered a fatal
error` during sanitizer **shutdown** (a known LSan teardown race), independent of input
content. Continuous fuzzing mode is unaffected -- 24h runs 1 and 2 both ended with a clean
`Done ... runs` line, and a 20s empty-corpus smoke run exits 0.

Consequences for triage:
- Do **not** judge whether an input crashes xpdf by its single-replay exit code.
- To triage a real finding, re-run under gdb (`gdb -ex run --args ./build/pdf_fuzzer <input>`)
  and look for an actual ASan report / stack -- ignore the LSan-at-exit fatal error.
- `ASAN_OPTIONS=detect_leaks=0` reduces but does not fully remove the at-exit flakiness, so
  gdb is the reliable path.

## Required ASAN_OPTIONS for fuzzing runs (P1.5+)

Run the fuzzer with:

```bash
ASAN_OPTIONS=detect_container_overflow=0:detect_leaks=0 ./build/pdf_fuzzer ... corpus/
```

- **`detect_container_overflow=0` (mandatory from P1.5 on).** The schema's first
  `repeated int32` field (`FontDescriptor.font_bbox`, a protobuf `RepeatedField<int>`) trips a
  **false-positive** ASan `container-overflow` inside `RepeatedField<int>::MergeFrom` during
  libprotobuf-mutator's CrossOver. Cause: `pdf.pb.cc` is ASan-instrumented and annotates the
  RepeatedField buffer, but miniconda's prebuilt `libprotobuf.so` is **not** instrumented, so
  the annotation desyncs. It is in the fuzzer's *mutation* layer, not in xpdf or the
  serializer (confirmed by stack trace + clean `pdffonts`/`pdfinfo` on generated PDFs).
  Without this flag the run aborts on the first cross-over that copies `font_bbox`.
  Trade-off: disables genuine container-overflow detection globally, but xpdf uses raw
  arrays / GString / GList (almost no `std::vector`), so the lost coverage is negligible.
- **`detect_leaks=0`** silences the unrelated LSan teardown crash described above.

Real memory-safety bugs (heap-overflow, UAF, etc.) are still caught -- only the
container-overflow check and the at-exit leak check are disabled.
