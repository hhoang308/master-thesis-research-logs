#!/bin/bash
# Build the PoDoFo 0.9.7 libFuzzer target -- COVERAGE-GUIDED INTO PODOFO SOURCE.
#
# Runs 1-4 linked the harness against the SYSTEM libpodofo (/lib/libpodofo.so.0.9.7),
# which is NOT instrumented -- so libFuzzer got ZERO coverage feedback from inside
# podofo's own parser and ASan never poisoned podofo's heap. This build instead links
# the FROM-SOURCE, instrumented static archive
#   thesis/podofo-0.9.7/build-asan/src/podofo/libpodofo.a
# built with clang -fsanitize=address,fuzzer-no-link (ASan + SanitizerCoverage). The
# fuzzer is now coverage-guided into base/*.cpp, exactly like the CFF campaign builds
# xpdf from source so instrumentation reaches the target (afl-xpdf402-cff-run1).
#
# The from-source podofo build (build-asan) already exists (it triaged the alloc-size
# finding). If it is ever removed, rebuild it -- see thesis/podofo-0.9.7 / the
# findings/podofo-alloc-size README.
set -euo pipefail

RESEARCH=/home/hoangnh8/master-thesis-research-logs/research
SCHEMA="$RESEARCH/schema/pdf-proto"
PODOFO_BUILD="$RESEARCH/thesis/podofo-0.9.7/build-asan"     # instrumented from-source podofo
BUILD_DIR="$SCHEMA/build-podofo-fuzz"
CLANG=/home/hoangnh8/miniconda3/bin/clang
CLANGXX=/home/hoangnh8/miniconda3/bin/clang++

# conda toolchain: libpodofo.a was built with conda clang 18; the harness MUST match it
# (sanitizer runtime + libstdc++ ABI). conda libs (protobuf/ssl/...) resolve via this path.
export LD_LIBRARY_PATH=/home/hoangnh8/miniconda3/lib:${LD_LIBRARY_PATH:-}
export PATH=/home/hoangnh8/miniconda3/bin:$PATH

[ -f "$PODOFO_BUILD/src/podofo/libpodofo.a" ] || {
  echo "ERROR: instrumented podofo archive missing: $PODOFO_BUILD/src/podofo/libpodofo.a"
  echo "  -> rebuild podofo 0.9.7 from source with clang -fsanitize=address,fuzzer-no-link"
  echo "     (see findings/podofo-alloc-size/README.md)."; exit 1; }

cmake -S "$SCHEMA" -B "$BUILD_DIR" \
  -DCMAKE_C_COMPILER="$CLANG" \
  -DCMAKE_CXX_COMPILER="$CLANGXX" \
  -DXPDF_SRC="$RESEARCH/thesis/xpdf-4.02" \
  -DXPDF_LEGACY_DISPLAYPAGES=ON \
  -DPODOFO_SOURCE_BUILD="$PODOFO_BUILD" \
  -DFONTCONFIG_LIBRARY=/usr/lib/x86_64-linux-gnu/libfontconfig.so.1 \
  -DCMAKE_PREFIX_PATH=/home/hoangnh8/miniconda3 \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5

cmake --build "$BUILD_DIR" --target pdf_fuzzer_podofo -j"$(nproc)"

BIN="$BUILD_DIR/pdf_fuzzer_podofo"
echo ""
echo "=== verify instrumentation reaches podofo source ==="
echo -n "  static (no dynamic libpodofo): "; ldd "$BIN" | grep -qi podofo && { echo "FAIL -- dynamic libpodofo linked"; exit 1; } || echo "OK"
echo -n "  podofo symbols present:        "; nm "$BIN" | grep -qc 'PdfMemDocument' && echo "OK" || { echo "FAIL"; exit 1; }
echo -n "  SanitizerCoverage present:     "; [ "$(nm "$BIN" | grep -c '__sanitizer_cov')" -gt 0 ] && echo "OK" || { echo "FAIL"; exit 1; }
echo -n "  ASan present:                  "; [ "$(nm "$BIN" | grep -c '__asan_')" -gt 0 ] && echo "OK" || { echo "FAIL"; exit 1; }
echo ""
echo "Built: $BIN"
