# AcroForm Field-Loop Module

Structured AcroForm field-cycle grammar for the Xpdf CVE-2018-7453 path.

The serializer emits a one-page PDF whose catalog references an `/AcroForm`
object. The default message reproduces the minimal mutual `/Kids` loop
documented in `research/pocs/cve-2018-7453`: `6 0 R -> 7 0 R -> 6 0 R`, with
non-null `/Parent` entries so Xpdf 4.04 keeps recursing in
`AcroForm::scanField()`.

## Build

```sh
cd /home/parkle/master-thesis-research-logs/research/schema/pdf-proto
cmake -S . -B build-env-check-clang18 \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++-18 \
  -DCMAKE_C_COMPILER=/usr/bin/clang-18
cmake --build build-env-check-clang18 --target \
  verify_acroform_loop acroformloop2pdf proto2pdf verify_serializer
```

## Verify

```sh
env ASAN_OPTIONS=detect_leaks=0 \
  ./build-env-check-clang18/verify_acroform_loop \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.04/build-release/xpdf/pdftotext \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.04/build-asan/xpdf/pdftotext \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.05/build-asan/xpdf/pdftotext
```

The verifier checks:

- semantic round-trip: serialized PDF is parsed back into a canonical
  `AcroFormLoopDocument`;
- structural invariants after deserialization: page dimensions, loop shape,
  loop length, parent mode, and non-empty `/Fields` entry;
- parser sanity: the internal parser must not return a meaningless error, and
  `qpdf --check` is used when available;
- content assertions: `/AcroForm`, `/Fields [6 0 R]`, `/Kids`, `/Parent`,
  `/Type /Page`, and `/MediaBox` must appear;
- optional xpdf smoke behavior: xpdf 4.04 release should crash, xpdf 4.04 ASan
  should report `AcroForm::scanField` stack overflow, and xpdf 4.05 ASan should
  exit cleanly on the same input.

## Deterministic CVE-2018-7453 Seed

```sh
env ASAN_OPTIONS=detect_leaks=0 \
  ./build-env-check-clang18/acroformloop2pdf \
  modules/acroformloop/seeds/cve-2018-7453.txtpb \
  /tmp/cve-2018-7453-acroform-loop.pdf

env ASAN_OPTIONS=detect_leaks=0 \
  ./build-env-check-clang18/proto2pdf \
  modules/acroformloop/seeds/cve-2018-7453-pdfdoc.txtpb \
  /tmp/cve-2018-7453-acroform-loop-pdfdoc.pdf
```

Expected differential result:

```sh
RESEARCH=/home/parkle/master-thesis-research-logs/research

bash -lc 'ulimit -s 256; \
  "$RESEARCH/targets/xpdf/4.04/build-release/xpdf/pdftotext" \
  /tmp/cve-2018-7453-acroform-loop.pdf /tmp/cve-2018-7453-release.txt'

bash -lc 'ulimit -s 256; \
  ASAN_OPTIONS=detect_leaks=0 \
  "$RESEARCH/targets/xpdf/4.04/build-asan/xpdf/pdftotext" \
  /tmp/cve-2018-7453-acroform-loop.pdf /tmp/cve-2018-7453-asan.txt'

bash -lc 'ulimit -s 256; \
  ASAN_OPTIONS=detect_leaks=0 \
  "$RESEARCH/targets/xpdf/4.05/build-asan/xpdf/pdftotext" \
  /tmp/cve-2018-7453-acroform-loop.pdf /tmp/cve-2018-7453-fixed.txt'
```

xpdf 4.04 release should segfault, xpdf 4.04 ASan should report a
`stack-overflow` involving `AcroForm::scanField`, and xpdf 4.05 should stop the
loop without crashing.
