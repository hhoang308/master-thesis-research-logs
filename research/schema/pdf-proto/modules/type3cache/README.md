# Type 3 Cache Lifecycle Proto

This module models the Type 3 font-cache lifetime pattern behind
CVE-2020-25725 as a small protobuf grammar.  It is intentionally separate from
the main `pdf.proto` while the grammar is being validated.

The generated PDFs are local research inputs for denial-of-service/UAF
reproduction under sanitizers.  They do not contain shellcode, ROP, heap
grooming, or a control-flow hijacking payload.

## Build

```sh
cd /home/parkle/master-thesis-research-logs/research/schema/pdf-proto
cmake -S . -B build-env-check-clang18 \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++-18 \
  -DCMAKE_C_COMPILER=/usr/bin/clang-18 \
  -DXPDF_SRC=/home/parkle/master-thesis-research-logs/research/targets/xpdf/4.02/source \
  -DXPDF_LEGACY_DISPLAYPAGES=ON
cmake --build build-env-check-clang18 --target \
  type3cache2pdf gen_type3cache_corpus check_type3cache_corpus
```

## Deterministic CVE-2020-25725 Seed

```sh
env ASAN_OPTIONS=detect_leaks=0 \
  ./build-env-check-clang18/type3cache2pdf \
  modules/type3cache/seeds/cve-2020-25725.txtpb \
  /tmp/type3cache-seed.pdf
```

Expected differential result:

```sh
RESEARCH=/home/parkle/master-thesis-research-logs/research

env ASAN_OPTIONS=detect_leaks=0 timeout 10s \
  "$RESEARCH/targets/xpdf/4.02/build-asan/xpdf/pdftoppm" \
  /tmp/type3cache-seed.pdf /tmp/type3cache-4.02

env ASAN_OPTIONS=detect_leaks=0 timeout 10s \
  "$RESEARCH/targets/xpdf/4.03/build-asan/xpdf/pdftoppm" \
  /tmp/type3cache-seed.pdf /tmp/type3cache-4.03
```

xpdf 4.02 should report an ASan `heap-use-after-free` in
`SplashOutputDev::endType3Char`.  xpdf 4.03 should not report the Type 3 cache
UAF.

## Corpus Smoke Test

```sh
env ASAN_OPTIONS=detect_leaks=0 \
  ./build-env-check-clang18/gen_type3cache_corpus /tmp/type3cache-corpus 1000

env ASAN_OPTIONS=detect_leaks=0 \
  ./build-env-check-clang18/check_type3cache_corpus /tmp/type3cache-corpus 1000
```

The corpus test validates serializer invariants and shape diversity.  It is not
expected that every generated sample crashes.  A good corpus should keep a useful
fraction of samples on the `d1` + nested CharProc + cache-eviction path.
