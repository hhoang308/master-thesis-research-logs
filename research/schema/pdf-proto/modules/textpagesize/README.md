# Text Extraction Page-Size Module

Structured damaged-PDF grammar for the Xpdf CVE-2023-3044 text-extraction
path.

This module intentionally models the exact public fuzzed trigger shape that was
verified in `research/pocs/cve-2023-3044`, not a clean well-formed PDF. The
serializer reproduces the public trigger by default and exposes the oversized
`/MediaBox` tokens plus a small amount of header/tail control to
libprotobuf-mutator.

## Build

```sh
cd /home/parkle/master-thesis-research-logs/research/schema/pdf-proto
cmake -S . -B build-env-check-clang18 \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++-18 \
  -DCMAKE_C_COMPILER=/usr/bin/clang-18
cmake --build build-env-check-clang18 --target \
  verify_text_page_size textpagesize2pdf
```

## Verify

```sh
env ASAN_OPTIONS=detect_leaks=0 \
  ./build-env-check-clang18/verify_text_page_size \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.04/build-release/xpdf/pdftotext \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.04/build-asan/xpdf/pdftotext \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.05/build-asan/xpdf/pdftotext
```

The verifier checks:

- semantic round-trip: serialized bytes are parsed back into a canonical
  `TextPageSizeDocument`;
- structural invariants after deserialization: header shape, positive decimal
  `MediaBox` tokens, and oversized height relative to width;
- parser sanity: the internal parser must succeed, and `qpdf --check` must
  either accept the file or emit meaningful damaged-PDF diagnostics;
- content assertions: `%iDF`/header token, `/Type /Page`, `/MediaBox`,
  `/Contents 7 0 R`, and the damaged `/Filter /FlateDecode` tail markers must
  appear when expected;
- smoke behavior: Xpdf 4.04 release must crash, Xpdf 4.04 ASan must reach the
  `TextLine::TextLine()` path, and Xpdf 4.05 ASan must no longer crash.

## Deterministic CVE-2023-3044 Seed

```sh
env ASAN_OPTIONS=detect_leaks=0 \
  ./build-env-check-clang18/textpagesize2pdf \
  modules/textpagesize/seeds/cve-2023-3044.txtpb \
  /tmp/cve-2023-3044-textpagesize.pdf
```

The default seed reproduces the public trigger shape with:

```pdf
%iDF
/MediaBox [0 0 612.0000 79299999999999999999.0000]
```

and the damaged `/Filter /FlateDecode` tail preserved.
