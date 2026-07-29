# JPXStream Module

Structured JPEG 2000 `/JPXDecode` grammar for Xpdf JPXStream allocation and
tile-part behavior, with defaults matching the CVE-2022-24107 PoC.

The serializer emits:

- a naked JPEG 2000 codestream with `SOC/SIZ/COD/QCD/SOT/SOD/EOC`
- a one-page PDF wrapper with one `/JPXDecode` image XObject

The default message produces one 65536x65536 tile.  Xpdf 4.03 computes
`tileComp->w * tileComp->h` before checking overflow; 4.04 rejects it with
`Invalid tile size or sample separation in JPX stream`.

## Verify

```sh
cd /home/parkle/master-thesis-research-logs/research/schema/pdf-proto
cmake --build build-env-check-clang18 --target verify_jpx_stream

./build-env-check-clang18/verify_jpx_stream \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.04/build-asan/xpdf/pdftoppm \
  /home/parkle/master-thesis-research-logs/research/targets/xpdf/4.03/build-asan/xpdf/pdftoppm
```

The verifier checks semantic round-trip, deserialized structural invariants,
content assertions, PDF xref sanity, and optional xpdf smoke behavior.
