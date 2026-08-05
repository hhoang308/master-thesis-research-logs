# ObjStm `/Length` Deadlock Proto

This module models the Xpdf CVE-2023-3436 pattern as a protobuf grammar for
`/ObjStm` `/Length` dependency graphs.

The default serializer output is still the historical 2-stream trigger:

- a compressed catalog object stored in object stream `4 0`
- `/Length 6 0 R` on object stream `4 0`
- compressed integer object `6 0` stored in object stream `5 0`
- an xref stream that marks objects `1 0` and `6 0` as compressed

That is the exact shape needed to exercise the `XRef::getObjectStreamObject()`
re-entry path that deadlocks in Xpdf `4.04`.

The grammar is no longer limited to that single shape. `objstm_entries` can now
describe:

- self-referential `/Length` objects hosted inside the same `/ObjStm`;
- mutual or cyclic `/ObjStm` dependency graphs;
- chains longer than two parent object streams;
- multiple compressed objects per `/ObjStm`;
- auto-assigned dependency object numbers before or after the host `/ObjStm`
  xref slot via `resolve_length_before_register`.

For each `ObjectStreamEntry`:

- `objstm_number` is the direct parent `/ObjStm` object number;
- `objects` stores raw compressed objects in the form `"<obj> 0 <body>"`;
- `length_dependencies` declares compressed integer objects hosted by that
  stream and used as `/Length` references by other parent `/ObjStm`s.

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

- canonicalization and serializer clamps;
- object-stream payload structure, dependency integer bodies, and xref-stream
  compressed-object entries for the expanded graph model;
- default-shape stability for the original CVE trigger;
- parser sanity: `qpdf --check` should accept the serialized PDF, with warnings
  allowed for intentionally cyclic/self-referential malformed cases;
- regression cases for self-reference, 2-node cycles, longer chains, multiple
  objects per stream, and before/after host-registration dependency numbering;
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
