# Text Large-Y Proto

This module models the Xpdf `CVE-2022-30524` trigger as a small protobuf
grammar.

The serializer emits a valid one-page PDF with:

- `/MediaBox [0 0 612 200000500]`
- one normal glyph at `(72, 72)`
- one large-`y` glyph at `(72, 200000000)`
- a simple `/Type1` Helvetica resource for `pdftotext`

That is the minimal shape needed to exercise the `TextOutputDev.cc` path that
crashes in Xpdf `4.04`.

## Build

```sh
cd /home/parkle/master-thesis-research-logs/research/schema/pdf-proto
cmake -S . -B build-env-check-clang18 \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++-18 \
  -DCMAKE_C_COMPILER=/usr/bin/clang-18 \
  -DXPDF_SRC=/home/parkle/master-thesis-research-logs/research/targets/xpdf/4.04/source
cmake --build build-env-check-clang18 --target \
  verify_text_large_y textlargey2pdf verify_serializer proto2pdf
```

## Verify

```sh
./build-env-check-clang18/verify_text_large_y \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.04/build-release/xpdf/pdftotext \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.04/build-asan/xpdf/pdftotext \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.05/build-asan/xpdf/pdftotext
```

The verifier checks:

- semantic round-trip: serialized PDF parses back into a canonical
  `TextLargeYDocument`;
- structural invariants after deserialization: valid PDF header, sane font
  settings, very large page height, normal/large glyph coordinates, and the
  `large_y < page_height` relation that keeps the glyph inside the page;
- parser sanity: `qpdf --check`, `mutool info`, and `pdfinfo` should accept the
  serialized PDF when those tools are installed;
- content assertions: emitted bytes must include the required `/MediaBox`,
  `/BaseFont`, normal `Tm`, large-`y` `Tm`, and text-show tokens;
- optional Xpdf smoke behavior: Xpdf `4.04` release and ASan should crash on
  the default trigger, while Xpdf `4.05` ASan should extract text cleanly.

## Deterministic CVE-2022-30524 Seed

```sh
./build-env-check-clang18/textlargey2pdf \
  modules/textlargey/seeds/cve-2022-30524.txtpb \
  /tmp/cve-2022-30524-textlargey.pdf
```

Expected differential result:

```sh
RESEARCH=/home/parkle/master-thesis-research-logs/research

"$RESEARCH/targets/xpdf/4.04/build-release/xpdf/pdftotext" \
  /tmp/cve-2022-30524-textlargey.pdf /tmp/cve-2022-30524-4.04-release.txt

env ASAN_OPTIONS=detect_leaks=0 \
  "$RESEARCH/targets/xpdf/4.04/build-asan/xpdf/pdftotext" \
  /tmp/cve-2022-30524-textlargey.pdf /tmp/cve-2022-30524-4.04-asan.txt

env ASAN_OPTIONS=detect_leaks=0 \
  "$RESEARCH/targets/xpdf/4.05/build-asan/xpdf/pdftotext" \
  /tmp/cve-2022-30524-textlargey.pdf /tmp/cve-2022-30524-4.05-asan.txt
```

Xpdf `4.04` release should segfault. Xpdf `4.04` ASan should report the
`TextLine::TextLine` invalid-access path in `TextOutputDev.cc`. Xpdf `4.05`
ASan should exit `0` and write the large-`y` glyph text.
