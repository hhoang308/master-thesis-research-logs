# CFF Module

Structured CFF/Type1C schema and helpers for `/FontFile3` fuzzing.

- `cff.proto` models CFF charstrings, local subrs, global subrs, and the
  `callsubr`/`callgsubr` relations needed by xpdf's `FoFiType1C` parser.
- `cff_serializer.cc` compiles `pdf_cff.CffFont` into byte-valid CFF bytes.
- `verify_cff.cc` checks the CFF assembler against a small target-relevant parser.
- `seeds/gen_cve_2020_35376_seed.cpp` emits a deterministic recursive local-subr
  `PdfDocument` seed for the known CVE-2020-35376 trigger shape.
- `tools/inspect_cff_seed.cpp` audits corpus inputs for structured CFF and
  self-recursive local-subr patterns.
