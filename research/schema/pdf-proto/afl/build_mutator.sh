#!/bin/bash
# Build the AFL++ custom-mutator shared object (afl_pdf_mutator.so).
# Compiled NON-ASan / -fPIC -shared: it is dlopen'd by afl-fuzz, not the target.
# LPM core sources are compiled fresh (the checked-in libprotobuf-mutator.a in build/
# was built WITH ASan and would drag __asan_* deps into the .so).
set -e
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SELF_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
CXX_BIN="${CXX:-clang++-14}"
PB="${PB:-/usr}"
PB_INCLUDE="${PB_INCLUDE:-$PB/include}"
if [ -z "${PB_LIB:-}" ]; then
  if [ "$PB" = "/usr" ] && [ -d /usr/lib/x86_64-linux-gnu ]; then
    PB_LIB=/usr/lib/x86_64-linux-gnu
  else
    PB_LIB=$PB/lib
  fi
fi
CLANG_RESOURCE_DIR="$($CXX_BIN -print-resource-dir)"
ABSL=$(ls "$PB_LIB"/libabsl_*.so 2>/dev/null | sed 's|.*/lib\(absl_[a-z0-9_]*\)\.so|-l\1|' | tr '\n' ' ')

"$CXX_BIN" -std=c++17 -fPIC -shared -O1 -g \
    -I"$CLANG_RESOURCE_DIR/include" \
    -I"$ROOT_DIR" -I"$BUILD_DIR" -I"$ROOT_DIR/libprotobuf-mutator" -I"$PB_INCLUDE" \
    "$SELF_DIR/afl_mutator.cpp" "$ROOT_DIR/serializer.cpp" \
    "$ROOT_DIR/modules/cff/cff_serializer.cc" \
    "$ROOT_DIR/modules/type3cache/type3_cache_serializer.cc" \
    "$BUILD_DIR/pdf.pb.cc" "$BUILD_DIR/modules/cff/cff.pb.cc" \
    "$BUILD_DIR/modules/type3cache/type3_cache.pb.cc" \
    "$ROOT_DIR/libprotobuf-mutator/src/mutator.cc" \
    "$ROOT_DIR/libprotobuf-mutator/src/binary_format.cc" \
    "$ROOT_DIR/libprotobuf-mutator/src/text_format.cc" \
    "$ROOT_DIR/libprotobuf-mutator/src/utf8_fix.cc" \
    -L"$PB_LIB" \
    -Wl,--start-group -lprotobuf $ABSL -Wl,--end-group -lz -lpthread \
    -Wl,-rpath,"$PB_LIB" \
    -o "$SELF_DIR/afl_pdf_mutator.so"

echo "built afl_pdf_mutator.so"
nm -D "$SELF_DIR/afl_pdf_mutator.so" | grep -E "afl_custom_(init|fuzz|post_process|deinit)"
echo "__asan deps: $(nm -D "$SELF_DIR/afl_pdf_mutator.so" | grep -c __asan) (must be 0)"
