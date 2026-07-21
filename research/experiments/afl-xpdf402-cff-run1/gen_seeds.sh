#!/bin/bash
# Emit a VALID (non-crashing) structured CFF proto seed into ./seeds/ for the mutator.
#
# The seed is a PdfDocument with a Type1C /FontFile3 font that carries a benign local
# subr (RETURN) plus a charstring that calls it -- so AFL starts from real CFF structure
# (charstrings + subr + callsubr machinery) and mutates toward bugs.
#
# NB: AFL needs a seed that does NOT crash. The recursive-subr shape (subr 0 calls subr 0)
# that triggers the known stack-overflow is therefore NOT used as a seed -- it is recorded
# in findings/xpdf402-cff-cvtglyph-stackoverflow. The mutator rediscovers recursion quickly.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PP=/home/hoangnh8/master-thesis-research-logs/research/schema/pdf-proto
PROTOC=/home/hoangnh8/miniconda3/bin/protoc

[ -f "$PP/pdf.proto" ] || { echo "ERROR: schema not found ($PP/pdf.proto)"; exit 1; }
mkdir -p "$SCRIPT_DIR/seeds"

"$PROTOC" --encode=pdf_proto.PdfDocument -I"$PP" "$PP/pdf.proto" \
    > "$SCRIPT_DIR/seeds/cff_valid.pb" <<'EOF'
pages {
  width: 612
  height: 792
  content_streams { raw_content: "BT /F0 24 Tf 72 720 Td (AB) Tj ET" }
  fonts {
    subtype: TYPE1
    base_font: "CffFuzzSeed"
    font_descriptor {
      flags: 4
      ascent: 700
      descent: -200
      stem_v: 80
      font_file {
        key: FONTFILE3
        subtype: "Type1C"
        cff {
          font_name: "CffFuzzSeed"
          charstrings { ops { op: ENDCHAR } }
          charstrings { ops { call_local: 0 } ops { op: ENDCHAR } }
          local_subrs { ops { op: RETURN } }
        }
      }
    }
    base_encoding: WINANSI
    differences { code: 32 name: "space" }
    first_char: 32
    widths: 500
  }
}
EOF

echo "wrote:"; ls -l "$SCRIPT_DIR/seeds"
echo ""
echo "Add more CFF shapes later via: $PP/build-402/gen_corpus, or hand-written .txtpb"
echo "(malformed INDEX / Private DICT overflow / charset-encoding OOB / deep callgsubr / CID)."
