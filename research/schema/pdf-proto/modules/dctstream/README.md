# DCTStream Scan-Mode Proto

This module models the JPEG scan-mode behavior behind CVE-2022-24106 in Xpdf's
internal `DCTStream` decoder.  The serializer emits a minimal PDF with one
`/DCTDecode` image XObject and assembles the embedded JPEG markers from the
protobuf fields.

The generated PDFs are local research inputs for sanitizer-backed
denial-of-service reproduction.  They do not contain shellcode, ROP, heap
shaping, or a control-flow hijacking payload.

## Build

```sh
cd /home/parkle/master-thesis-research-logs/research/schema/pdf-proto
cmake -S . -B build-env-check-clang18 \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++-18 \
  -DCMAKE_C_COMPILER=/usr/bin/clang-18
cmake --build build-env-check-clang18 --target \
  verify_dct_stream dctstream2pdf proto2pdf
```

## Verify

```sh
./build-env-check-clang18/verify_dct_stream
```

The verifier checks:

- semantic round-trip: serialized PDF/JPEG is parsed back into a canonical
  `DctStreamDocument`;
- field invariants after deserialization: dimensions, frame component count,
  scan component IDs, duplicate IDs;
- parser sanity: an internal JPEG/PDF parser must return a meaningful result,
  and `qpdf --check` is used when available;
- content assertions: `/DCTDecode`, `/Subtype /Image`, SOF0/SOS/EOI JPEG
  markers, `/Width`, and `/Height` must appear.

## Deterministic CVE-2022-24106 Seed

```sh
./build-env-check-clang18/dctstream2pdf \
  modules/dctstream/seeds/cve-2022-24106.txtpb \
  /tmp/cve-2022-24106-dctstream.pdf

./build-env-check-clang18/proto2pdf \
  modules/dctstream/seeds/cve-2022-24106-pdfdoc.txtpb \
  /tmp/cve-2022-24106-dctstream-pdfdoc.pdf
```

Expected differential result:

```sh
RESEARCH=/home/parkle/master-thesis-research-logs/research

env ASAN_OPTIONS=detect_leaks=0 timeout 10s \
  "$RESEARCH/targets/xpdf/4.03/build-asan/xpdf/pdftoppm" \
  /tmp/cve-2022-24106-dctstream.pdf /tmp/cve-2022-24106-4.03

env ASAN_OPTIONS=detect_leaks=0 timeout 10s \
  "$RESEARCH/targets/xpdf/4.04/build-asan/xpdf/pdftoppm" \
  /tmp/cve-2022-24106-dctstream.pdf /tmp/cve-2022-24106-4.04
```

xpdf 4.03 should report an ASan null write in `DCTStream::readMCURow`.  xpdf
4.04 should not report the DCTStream crash on the same input.
