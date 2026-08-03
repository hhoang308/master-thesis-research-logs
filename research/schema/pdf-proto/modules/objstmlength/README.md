# ObjStm `/Length` Deadlock Proto

This module models the Xpdf CVE-2023-3436 pattern as a small protobuf grammar.

The serializer emits a PDF 1.5 file with:

- a compressed catalog object stored in object stream `4 0`
- `/Length 6 0 R` on object stream `4 0`
- compressed integer object `6 0` stored in object stream `5 0`
- an xref stream that marks objects `1 0` and `6 0` as compressed

That is the exact shape needed to exercise the `XRef::getObjectStreamObject()`
re-entry path that deadlocks in Xpdf `4.04`.

## Build

```sh
cd /home/parkle/master-thesis-research-logs/research/schema/pdf-proto
cmake -S . -B build-objstmlength
cmake --build build-objstmlength --target \
  verify_objstm_length objstmlength2pdf proto2pdf verify_serializer
```

## Verify

```sh
./build-objstmlength/verify_objstm_length \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.04/build-release/xpdf/pdftoppm \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.04/build-asan/xpdf/pdftoppm \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.05/build-asan/xpdf/pdftoppm
```

The verifier checks:

- semantic round-trip: serialized PDF is parsed back into a canonical
  `ObjstmLengthDocument`;
- structural invariants after deserialization: header, page dimensions,
  object-stream dictionaries, xref-stream entries, and `/Length` value
  consistency;
- parser sanity: `qpdf --check` should accept the serialized PDF;
- content assertions: the emitted bytes must include the required `/ObjStm`,
  `/Type /XRef`, `/Length 6 0 R`, `/Root 1 0 R`, and compressed-object tokens;
- optional xpdf smoke behavior: Xpdf `4.04` release and ASan should hang until
  `timeout` returns `124`, and Xpdf `4.05` ASan should exit cleanly and render
  the page.

## Deterministic CVE-2023-3436 Seed

```sh
./build-objstmlength/objstmlength2pdf \
  modules/objstmlength/seeds/cve-2023-3436.txtpb \
  /tmp/cve-2023-3436-objstm-length.pdf

./build-objstmlength/proto2pdf \
  modules/objstmlength/seeds/cve-2023-3436-pdfdoc.txtpb \
  /tmp/cve-2023-3436-objstm-length-pdfdoc.pdf
```

Expected differential result:

```sh
RESEARCH=/home/parkle/master-thesis-research-logs/research

timeout 5s \
  "$RESEARCH/targets/xpdf/4.04/build-release/xpdf/pdftoppm" \
  /tmp/cve-2023-3436-objstm-length.pdf /tmp/cve-2023-3436-4.04-release

env ASAN_OPTIONS=detect_leaks=0 timeout 5s \
  "$RESEARCH/targets/xpdf/4.04/build-asan/xpdf/pdftoppm" \
  /tmp/cve-2023-3436-objstm-length.pdf /tmp/cve-2023-3436-4.04-asan

env ASAN_OPTIONS=detect_leaks=0 timeout 5s \
  "$RESEARCH/targets/xpdf/4.05/build-asan/xpdf/pdftoppm" \
  /tmp/cve-2023-3436-objstm-length.pdf /tmp/cve-2023-3436-4.05-asan
```

Xpdf `4.04` release and ASan should hang until `timeout` kills them. Xpdf
`4.05` ASan should exit `0` and emit a PPM.
