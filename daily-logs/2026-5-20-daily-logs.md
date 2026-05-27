## Concrete Recommendation: A Decision Fork
You are at an architecture decision point. Here are the two coherent paths:

Path A — AFL++ Custom Mutator (Stay on current trajectory)

Target: xpdf with AFL++ persistent mode harness
Mutation engine: Python custom mutator using pikepdf AOT manipulation
Grammar source: CVE-derived mutation functions (your Nautilus grammar work informs these)
Nautilus role: offline seed corpus generation only
Effort: medium — persistent mode harness for xpdf requires ~100 lines of C modification
Path B — LPM + libFuzzer (Architecture pivot)

Target: MuPDF or Poppler (switch from xpdf — they are library targets with clean APIs)
Mutation engine: LPM with CVE-informed proto schema
Converter: proto-to-pikepdf (or direct PDF byte construction) function
libFuzzer harness: 30-50 line C wrapper calling mupdf_load_document(bytes, len) or equivalent
Effort: medium — proto schema design is the intellectual core, harness is boilerplate
My recommendation for your stated goals (prototype + CVE + defensible thesis): Take Path B. The reasons are:

The speed problem you identified is structurally solved by in-process fuzzing, not worked around.
MuPDF has active CVEs (2023-2024) and a clean C API — probability of CVE discovery is comparable to xpdf.
LPM + libFuzzer is the architecture Google uses for PDFium — your work is directly comparable to and positioned against production-grade tooling, which strengthens your evaluation section.
The proto schema design IS the thesis contribution — CVE analysis → structural risk patterns → proto schema encoding is a clear, traceable, and examinable methodology.
In the next session, if you choose Path B, we will design the protobuf schema for PDF objects and define the proto-to-PDF converter architecture. If you choose Path A, we will specify the AFL++ custom mutator interface and the persistent mode harness modification for xpdf. State your choice and we proceed.