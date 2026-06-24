#!/bin/bash
# Build the AFL++ custom-mutator shared object (afl_pdf_mutator.so).
# Compiled NON-ASan / -fPIC -shared: it is dlopen'd by afl-fuzz, not the target.
# LPM core sources are compiled fresh (the checked-in libprotobuf-mutator.a in build/
# was built WITH ASan and would drag __asan_* deps into the .so).
set -e
cd "$(dirname "$0")"
PB=/home/hoangnh8/miniconda3
ABSL=$(ls $PB/lib/libabsl_*.so 2>/dev/null | sed 's|.*/lib\(absl_[a-z0-9_]*\)\.so|-l\1|' | tr '\n' ' ')

clang++-14 -std=c++17 -fPIC -shared -O1 -g \
    -I/usr/include/c++/11 -I/usr/include/x86_64-linux-gnu/c++/11 \
    -I/usr/lib/llvm-14/lib/clang/14.0.0/include \
    -I. -Ilibprotobuf-mutator -I$PB/include \
    afl_mutator.cpp serializer.cpp pdf.pb.cc \
    libprotobuf-mutator/src/mutator.cc libprotobuf-mutator/src/binary_format.cc \
    libprotobuf-mutator/src/text_format.cc libprotobuf-mutator/src/utf8_fix.cc \
    -L$PB/lib -L/usr/lib/gcc/x86_64-linux-gnu/11 \
    -Wl,--start-group -lprotobuf $ABSL -Wl,--end-group -lz -lpthread \
    -Wl,-rpath,$PB/lib \
    -o afl_pdf_mutator.so

echo "built afl_pdf_mutator.so"
nm -D afl_pdf_mutator.so | grep -E "afl_custom_(init|fuzz|post_process|deinit)"
echo "__asan deps: $(nm -D afl_pdf_mutator.so | grep -c __asan) (must be 0)"
