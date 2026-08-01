# ICCBased `/N 0` Module

Structured ICCBased image grammar for the Xpdf CVE-2023-2662 path.

The serializer emits a one-page PDF with one image XObject whose `/ColorSpace`
is `[/ICCBased 4 0 R]`. The referenced ICC stream dictionary carries `/N` and
`/Alternate`; the default message reproduces the `/N 0` + `/Alternate
/DeviceGray` shape that reaches `ImageStream::ImageStream()` in xpdf 4.04.

## Build

```sh
cd /home/parkle/master-thesis-research-logs/research/schema/pdf-proto
cmake -S . -B build-env-check-clang18 \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++-18 \
  -DCMAKE_C_COMPILER=/usr/bin/clang-18
cmake --build build-env-check-clang18 --target \
  verify_icc_based iccbased2pdf proto2pdf
```

## Verify

```sh
env ASAN_OPTIONS=detect_leaks=0 \
  ./build-env-check-clang18/verify_icc_based \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.04/build-asan/xpdf/pdftoppm \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.05/build-asan/xpdf/pdftoppm
```

The verifier checks:

- semantic round-trip: serialized PDF is parsed back into a canonical
  `IccBasedDocument`;
- structural invariants after deserialization: dimensions, `BitsPerComponent`,
  `N`, `Range`, and stream payload presence;
- parser sanity: the internal parser must not return a meaningless error, and
  `qpdf --check` is used when available;
- content assertions: `/ICCBased`, `/Alternate`, `/N`, `/Subtype /Image`,
  `/Width`, `/Height`, and `/Im0 Do` must appear;
- optional xpdf smoke behavior: xpdf 4.04 ASan should hit the divide-by-zero
  path, while 4.05 should report the fixed ICCBased mismatch error.

## Deterministic CVE-2023-2662 Seed

```sh
env ASAN_OPTIONS=detect_leaks=0 \
  ./build-env-check-clang18/iccbased2pdf \
  modules/iccbased/seeds/cve-2023-2662.txtpb \
  /tmp/cve-2023-2662-iccbased.pdf

env ASAN_OPTIONS=detect_leaks=0 \
  ./build-env-check-clang18/proto2pdf \
  modules/iccbased/seeds/cve-2023-2662-pdfdoc.txtpb \
  /tmp/cve-2023-2662-iccbased-pdfdoc.pdf
```

Expected differential result:

```sh
RESEARCH=/home/parkle/master-thesis-research-logs/research

env ASAN_OPTIONS=detect_leaks=0 timeout 10s \
  "$RESEARCH/targets/xpdf/4.04/build-asan/xpdf/pdftoppm" \
  /tmp/cve-2023-2662-iccbased.pdf /tmp/cve-2023-2662-4.04

env ASAN_OPTIONS=detect_leaks=0 timeout 10s \
  "$RESEARCH/targets/xpdf/4.05/build-asan/xpdf/pdftoppm" \
  /tmp/cve-2023-2662-iccbased.pdf /tmp/cve-2023-2662-4.05
```

xpdf 4.04 should report a divide-by-zero in `ImageStream::ImageStream()`.
xpdf 4.05 should not crash and should instead report the ICCBased component
mismatch error.
