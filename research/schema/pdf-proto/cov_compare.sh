#!/bin/bash
# Fair AFL++-vs-libFuzzer coverage comparison (thesis criterion 2).
#
# Measures BOTH corpora on ONE coverage-instrumented xpdf build (cov_driver, LLVM
# source coverage) so the yardstick is identical (lines/functions/regions on xpdf
# source). This replaces the bogus "edge bitmap %" comparison between two differently
# instrumented binaries.
#
# Inputs:
#   $1 = label              (e.g. "afl" or "libfuzzer")
#   $2 = dir of PDF files   (AFL queue, or libFuzzer corpus already serialized to PDFs)
# Output: <label>.profdata + an llvm-cov report over the xpdf source.
#
# For the libFuzzer corpus (protos), first convert to PDFs with serialize_corpus
# (see README) before running this.
set -u
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
COV_DRIVER="$SELF_DIR/build-cov/cov_driver"
XPDF_SRC="$SELF_DIR/../../targets/xpdf/4.06/source/xpdf"
PROFDATA_TOOL=llvm-profdata-14
COV_TOOL=llvm-cov-14

LABEL="${1:?usage: cov_compare.sh <label> <pdf_dir> [max]}"
PDF_DIR="${2:?usage: cov_compare.sh <label> <pdf_dir> [max]}"
MAX="${3:-0}"   # 0 = all

WORK="$SELF_DIR/cov-work/$LABEL"
rm -rf "$WORK"; mkdir -p "$WORK/raw"

echo "[$LABEL] running cov_driver over PDFs in $PDF_DIR ..."
i=0
for f in "$PDF_DIR"/*; do
    [ -f "$f" ] || continue
    # one profraw per input keeps a crash on input N from losing prior coverage
    LLVM_PROFILE_FILE="$WORK/raw/p_%p_%m.profraw" timeout 20 "$COV_DRIVER" "$f" >/dev/null 2>&1
    i=$((i+1))
    if [ "$MAX" -gt 0 ] && [ "$i" -ge "$MAX" ]; then break; fi
done
echo "[$LABEL] ran $i inputs"

echo "[$LABEL] merging $(ls "$WORK/raw" | wc -l) profraw files ..."
"$PROFDATA_TOOL" merge -sparse "$WORK"/raw/*.profraw -o "$WORK/$LABEL.profdata"

echo "[$LABEL] === llvm-cov report (xpdf source) ==="
"$COV_TOOL" report "$COV_DRIVER" -instr-profile="$WORK/$LABEL.profdata" \
    "$XPDF_SRC" 2>/dev/null | tail -40

echo ""
echo "[$LABEL] profdata: $WORK/$LABEL.profdata"
echo "[$LABEL] full per-file report: $COV_TOOL report $COV_DRIVER -instr-profile=$WORK/$LABEL.profdata $XPDF_SRC"
