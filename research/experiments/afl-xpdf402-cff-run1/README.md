# AFL++ CFF/Type1C campaign — xpdf 4.02 — `FoFiType1C`

Structure-aware fuzzing của parser CFF/Type1C (`fofi/FoFiType1C.cc`) qua font `/FontFile3`.
Mục tiêu: bug CFF **sâu hơn** stack-overflow `cvtGlyph` đã biết (finding
`findings/xpdf402-cff-cvtglyph-stackoverflow`, chỉ 4.02) — **ưu tiên bug còn sống trên 4.06**.

> ⚠️ **CHƯA chạy được ngay** — cần build target AFL+ASan (mục 1) và sinh seed (mục 2). `run.sh`
> có precondition check nên sẽ báo lỗi rõ ràng nếu thiếu.

## Vì sao target là `pdftops`, KHÔNG phải harness render

`cvtGlyph` (charstring interpreter, nơi có bug) chỉ chạy ở đường **Type1C → Type1** của
`PSOutputDev` (tức `pdftops`). Harness render Splash (`build-afl-asan/afl_harness_xpdf`) giao
font CFF nhúng cho **FreeType** → **KHÔNG** gọi `cvtGlyph` (đã kiểm chứng: harness render exit 0
trên input crash CFF, còn `pdftops` thì crash). ⇒ **target đúng = `pdftops`**.

## 1. Build target: `pdftops` xpdf 4.02 với afl-clang-fast + ASan

`build-asan/xpdf/pdftops` hiện có là ASan **thuần** (không instrument AFL → fuzz sẽ mù). Cần build
bản có coverage:

```bash
ROOT=/home/hoangnh8/master-thesis-research-logs/research
XPDF402=$ROOT/thesis/xpdf-4.02
export LD_LIBRARY_PATH=/home/hoangnh8/miniconda3/lib:$LD_LIBRARY_PATH
cmake -S "$XPDF402" -B "$XPDF402/build-afl-asan" \
  -DCMAKE_C_COMPILER=/home/hoangnh8/.local/bin/afl-clang-fast \
  -DCMAKE_CXX_COMPILER=/home/hoangnh8/.local/bin/afl-clang-fast++ \
  -DCMAKE_C_FLAGS="-fsanitize=address -g -O1" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -g -O1" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$XPDF402/build-afl-asan" -j"$(nproc)" --target pdftops
# xác minh có CẢ AFL lẫn ASan:
nm "$XPDF402/build-afl-asan/xpdf/pdftops" | grep -c __afl    # > 0
nm "$XPDF402/build-afl-asan/xpdf/pdftops" | grep -c __asan   # > 0
```

## 2. Seed: CFF proto có cấu trúc

Mutator ăn seed là **proto `.pb`** (không phải PDF). Chạy `./gen_seeds.sh` → sinh
`seeds/cff_valid.pb`: một font `/FontFile3` Type1C **hợp lệ** (charstring + local subr benign
`RETURN` + `callsubr`) để AFL bắt đầu từ cấu trúc CFF thật rồi mutate.

> ⚠️ AFL cần seed **KHÔNG crash**. Shape đệ quy (subr 0 gọi subr 0) gây stack-overflow đã biết
> **không** dùng làm seed (AFL sẽ abort "no valid seed") — nó nằm ở finding
> `xpdf402-cff-cvtglyph-stackoverflow`. Mutator tự tìm lại đệ quy rất nhanh (sanity 25s → 8 crashes).

Thêm seed đa dạng sau bằng `build-402/gen_corpus` hoặc hand-write `.txtpb` các hình dạng:
INDEX méo (`getIndexVal` OOB), Private DICT tràn operand, charset/encoding SID/GID OOB,
callsubr/callgsubr lồng sâu, CID (FDSelect/FDArray).

## 3. Chạy

```bash
cd afl-xpdf402-cff-run1
./run.sh 86400        # 24h; hoặc số giây nhỏ để sanity
```

Engine: `afl-fuzz` + custom mutator (`afl/afl_pdf_mutator.so`, structure-aware) ở **@@ mode**
(`pdftops @@ /dev/null`). Mutator serialize proto→PDF(`/FontFile3` CFF); `AFL_POST_PROCESS_KEEP_ORIGINAL=1`
giữ proto trong queue để triage.

## 4. Bề mặt tấn công ưu tiên (`FoFiType1C`)

| Vùng | Hàm | Bug tiềm năng |
|------|-----|---------------|
| Charstring interp | `cvtGlyph`, `getOp` | operand stack over/underflow; đệ quy callsubr/callgsubr (đã biết) |
| INDEX | `getIndex`/`getIndexVal` | offset/count OOB |
| DICT | `readTopDict`/`readPrivateDict` | operand overflow |
| Charset/Encoding | `readCharset`/`buildEncoding` | ghi SID/GID OOB |
| CID | FDSelect/FDArray | chưa cover |

## 5. Differential triage (2 chiều: 4.02 + 4.06)

Fuzz 4.02 (bug surface giàu). Mỗi crash → chạy qua **cả** `pdftops` 4.02 và 4.06 ASan:

```bash
for pdftops in $ROOT/thesis/xpdf-4.02/build-asan/xpdf/pdftops \
               $ROOT/thesis/xpdf-4.06/build-asan/xpdf/pdftops; do
  ASAN_OPTIONS=detect_leaks=0:symbolize=1 "$pdftops" <crash.pdf> /dev/null
done
```

Reproduce proto→PDF bằng `proto2pdf_t3` (phải link `cff_serializer.cc` — bản `build-402/proto2pdf`
CŨ nuốt grammar, xem finding type3cache-uaf để tránh bẫy). Phân loại: **4.02-only** (known/fixed) vs
**4.06-live** (đáng giá cho luận văn). Group theo frame `FoFiType1C`.

## Ghi chú
- Đã có finding CFF: `findings/xpdf402-cff-cvtglyph-stackoverflow` (stack-overflow `cvtGlyph`, 4.02-only).
- Chưa từng có campaign nhắm CFF; trước đây `FoFiType1C` chỉ được cover ngẫu nhiên.
