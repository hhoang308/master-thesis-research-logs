# AFL++ structure-aware — xpdf 4.02 — Type 3 font-cache (CVE-2020-25725)

Structure-aware AFL++ campaign nhắm nhánh **Type 3 font-cache** của xpdf 4.02 (grammar mới
`modules/type3cache/`, mô phỏng CVE-2020-25725). libprotobuf-mutator mutate một `PdfDocument`
proto có `type3_cache_programs`; `SerializePdf()` rẽ sang `SerializeType3CachePdf()` → PDF chứa
font Type 3 lồng nhau; xpdf 4.02 parse nó.

```
seed .pb (PdfDocument{type3_cache_programs}) ─► afl_custom_fuzz (LPM mutate)
        ─► afl_custom_post_process ─ SerializeType3CachePdf() ─► PDF
        ─► afl_harness_xpdf ─► xpdf 4.02 (instrumented)
```

## Trạng thái build (tính đến khi tạo folder này)

| Bước | Trạng thái |
|---|---|
| A. Resolve conflict git (stash-pop) | ✅ done — giữ upstream harness, bỏ `build_mutator.sh` top-level |
| B. Sinh protobuf C++ (pdf+cff+type3) → `build-afl/` | ✅ done |
| C. Build `build-afl/afl_harness_xpdf` (afl-clang-fast, xpdf 4.02) | ✅ done (5.1M, 64 `__afl` syms) |
| D. Build `afl/afl_pdf_mutator.so` | ✅ done (4 hook, `__asan`=0, ldd sạch, 2.7M) |
| E. Sinh seed Type 3 (binary) | ⏳ **chạy các lệnh bên dưới** |
| F. `run.sh` / `.gitignore` / README | ✅ done |
| G. Smoke test + launch | ⏳ sau khi có seed |

## Toolchain (host này)
- AFL++ `~/.local/bin`; conda protobuf 6.33.5 + protoc 33.5 (`$HOME/miniconda3` by default); clang-14.
- afl-fuzz cần `LD_LIBRARY_PATH=~/miniconda3/lib` (libpython3.13) — `run.sh` tự set.

---

## Cách build lại từ đầu

```bash
REPO_ROOT=$(git rev-parse --show-toplevel)
ROOT=$REPO_ROOT/research
PP=$ROOT/schema/pdf-proto ; XPDF402=$ROOT/thesis/xpdf-4.02 ; PB=${PB_PREFIX:-$HOME/miniconda3}
AFL_CC=${AFL_CC:-$HOME/.local/bin/afl-clang-fast}
AFL_CXX=${AFL_CXX:-$HOME/.local/bin/afl-clang-fast++}
export LD_LIBRARY_PATH=$PB/lib:$LD_LIBRARY_PATH
```

### C. Build AFL harness (xpdf 4.02, không fontconfig)
```bash
cmake -S "$PP" -B "$PP/build-afl" \
  -DCMAKE_C_COMPILER="$AFL_CC" \
  -DCMAKE_CXX_COMPILER="$AFL_CXX" \
  -DCMAKE_PREFIX_PATH="$PB" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DAFL_BUILD=ON -DXPDF_LEGACY_DISPLAYPAGES=ON -DXPDF_SRC="$XPDF402"
cmake --build "$PP/build-afl" -j"$(nproc)" --target afl_harness_xpdf
```
- `CMAKE_POLICY_VERSION_MINIMUM=3.5`: CMake conda 4.x không nhận `cmake_minimum_required` cũ của xpdf 4.02.
- Host thiếu `libfontconfig-dev` → `CMakeLists.txt` đã sửa để **link fontconfig có điều kiện**
  (`if(FONTCONFIG_LIBRARY)`), xpdf build với `HAVE_FONTCONFIG=0`. **Không** truyền `-DFONTCONFIG_LIBRARY`.

### B. Sinh protobuf C++ vào build-afl (build_mutator.sh cần sẵn)
```bash
"$PB/bin/protoc" -I "$PP" --cpp_out="$PP/build-afl" \
  pdf.proto modules/cff/cff.proto modules/type3cache/type3_cache.proto
```
Đồng thời **xoá file `.pb.*` cũ ở top-level** (`$PP/pdf.pb.* $PP/cff.pb.*`, sót từ build tháng 7) —
nếu không, `-I$ROOT_DIR` (đứng trước `-I$BUILD_DIR`) chọn trúng bản cũ → lỗi `redefinition of CffFont`.

### D. Build `.so` mutator (serializer + cff + type3 + LPM, non-ASan)
`afl/build_mutator.sh` (bản mới) đã được thêm 3 hook env host-specific (mặc định rỗng):
```bash
CXX=clang++-14 PB="$PB" BUILD_DIR="$PP/build-afl" \
  EXTRA_CXXFLAGS="-I/usr/include/c++/11 -I/usr/include/x86_64-linux-gnu/c++/11" \
  EXTRA_LDFLAGS="-L/usr/lib/gcc/x86_64-linux-gnu/11" \
  EXTRA_LIBS="-lutf8_range -lutf8_validity" \
  bash "$PP/afl/build_mutator.sh"
```
- `EXTRA_CXXFLAGS`: clang-14 tự chọn GCC không có `-dev` header → chỉ rõ header GCC-11.
- `EXTRA_LIBS`: protobuf 6.x tách `libutf8_range`/`libutf8_validity` thành lib riêng.
- Verify: `nm -D afl/afl_pdf_mutator.so | grep afl_custom_` (đủ 4); `__asan`=0.

### E. Sinh seed Type 3 (binary PdfDocument)  ← BƯỚC CẦN BẠN CHẠY
Mutator đọc **binary** `PdfDocument`. Convert seed CVE gốc + tạo biến thể:
```bash
EXP=$ROOT/experiments/afl-xpdf402-type3cache-run1
mkdir -p "$EXP/seeds"

# 1) seed CVE-2020-25725 gốc
"$PB/bin/protoc" -I "$PP" --encode=pdf_proto.PdfDocument pdf.proto \
  < "$PP/modules/type3cache/seeds/cve-2020-25725-pdfdoc.txtpb" > "$EXP/seeds/cve-2020-25725.pb"

# 2) biến thể phủ grammar (matrix_mode / nested_depth / glyph_style / d1 / filler_fonts)
i=0
gen(){ i=$((i+1)); printf 'type3_cache_programs {\n%s\n}\n' "$1" \
  | "$PB/bin/protoc" -I "$PP" --encode=pdf_proto.PdfDocument pdf.proto \
  > "$EXP/seeds/$(printf 't3_%03d.pb' $i)"; }
gen "matrix_mode: EXACT_REUSE nested_depth: 1 filler_fonts: 8"
gen "matrix_mode: NEAR_REUSE  nested_depth: 1 filler_fonts: 8"
gen "matrix_mode: DIFFERENT   nested_depth: 1 filler_fonts: 8"
gen "matrix_mode: EXACT_REUSE nested_depth: 0 filler_fonts: 8"
gen "matrix_mode: EXACT_REUSE nested_depth: 2 filler_fonts: 8"
gen "matrix_mode: EXACT_REUSE nested_depth: 4 filler_fonts: 16"
gen "matrix_mode: EXACT_REUSE nested_depth: 8 filler_fonts: 32"
gen "matrix_mode: EXACT_REUSE nested_depth: 1 glyph_program_style: EMPTY_GLYPH"
gen "matrix_mode: EXACT_REUSE nested_depth: 1 glyph_program_style: REPEATED_TEXT"
gen "matrix_mode: EXACT_REUSE nested_depth: 2 outer_uses_d1: false"
gen "matrix_mode: EXACT_REUSE nested_depth: 2 inner_uses_d1: false"
gen "matrix_mode: EXACT_REUSE nested_depth: 2 render_fillers_after_nested: false"
gen "matrix_mode: EXACT_REUSE nested_depth: 1 filler_fonts: 0"
gen "matrix_mode: EXACT_REUSE nested_depth: 8 filler_fonts: 64 glyph_program_style: REPEATED_TEXT page_font_size: 144 glyph_size: 2000"
ls "$EXP/seeds" | wc -l    # ~15 seeds; LPM tự mở rộng phần còn lại
```
(Tùy chọn nhiều seed hơn: build `gen_type3cache_corpus` trong `modules/type3cache/tools/`, sinh
`.txtpb` `Type3CacheDocument`, rồi bọc `type3_cache_programs { … }` + `protoc --encode` như trên.)

### G. Chạy
```bash
cd "$EXP"
./run.sh 60        # smoke test 60s: xác nhận .so nạp, seed load, KHÔNG corruption, coverage > 0
./run.sh 86400     # campaign 24h (detached)
# monitor:
LD_LIBRARY_PATH=$PB/lib ${AFL_WHATSUP:-$HOME/.local/bin/afl-whatsup} "$EXP/out"
tail -f "$EXP/afl.log"
# stop:  kill $(cat "$EXP/afl.pid")
```

## Lưu ý vận hành
- **`AFL_POST_PROCESS_KEEP_ORIGINAL=1`** (đã set trong `run.sh`) — BẮT BUỘC. Bỏ đi → afl-fuzz hỏng
  heap trong vài giây (queue lưu PDF thay vì proto → mutator parse PDF-as-proto → corrupt).
- Crash lưu dạng **proto** (`out/default/crashes/*`). Triage: `modules/type3cache/tools/type3cache2pdf`
  hoặc `tools/proto2pdf` để dựng lại PDF thật rồi chạy qua bản xpdf 4.02 ASan (`thesis/xpdf-4.02/build-asan`).
- ASan OFF trên tuyến này → crash hiện dưới dạng SIGSEGV/SIGABRT, AFL bắt trực tiếp.

## Review serializer/proto mới (tóm tắt)
An toàn bộ nhớ SẠCH (output chặn bởi LPM `max_size`), xác định, không đệ quy (Type3 clamp
`nested_depth`∈[0,8], `filler_fonts`∈[0,64]). Nit nhỏ không chặn: 2 signed-overflow ở
`modules/cff/cff_serializer.cc` (chỉ tạo số wrap).
