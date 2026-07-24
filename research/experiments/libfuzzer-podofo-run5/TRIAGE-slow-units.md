# Triage — 3 `slow-unit` artifacts của `libfuzzer-podofo-run5`

**Target:** PoDoFo 0.9.7 (ASan, from-source) qua harness `schema/pdf-proto/harness_podofo.cpp`.
**Ngày triage:** 2026-07-24 (run kết thúc 2026-07-23 23:28 +07).
**Artifacts:** `artifacts/slow-unit-{958f…, d007…, 0eec…}` — libFuzzer lưu khi 1 unit chạy quá
`-report_slow_units` (mặc định **10 s**; run không override, `timeout` mặc định 1200 s).

---

## TL;DR (kết luận)

1. **Không unit nào chậm khi replay standalone** trên host rảnh: **958f ≈ 10 ms · d007 ≈ 13 ms ·
   0eec ≈ 105 ms**, ổn định qua nhiều lần, **0 crash**. Cách ngưỡng 10 s tới 100–1000×.
2. **Nhãn `slow-unit` là artifact môi trường** của run 24 h (memory pressure + fork mode), **không
   phải algorithmic-complexity / DoS thật**. `report_slow_units` đo **wall-time**, không phải CPU-time.
3. **0eec = TRÙNG `findings/podofo-alloc-size`** — cùng root cause & stack: `PdfPredictorDecoder`
   ctor integer-overflow, `base/PdfFiltersPrivate.cpp:112`, chỉ khác đi qua **FLATE** predictor
   (`:605`) thay vì LZW (`:767`). Với `allocator_may_return_null=1`, alloc quá lớn trả `null` ⇒
   PoDoFo bắt (`OutOfMemory`) ⇒ **không crash** ⇒ bị ghi nhận là *slow* thay vì *crash*.
4. **d007, 958f = breadth lành tính** (nhiều stream nhỏ), **không** chạm alloc path (clean với
   `allocator_may_return_null=0`).
5. Hai "đòn bẩy" nghi ngờ khác trong 0eec — font `/Length` 808 MB (`length_delta: 808464433`) và
   CFF `call_local: 65533` recursion — đều **inert** trên PoDoFo (ablation: 0 tác động).
6. **Không có bug mới.** Không cần tạo finding dir mới. Cập nhật nhỏ cho TODO của `podofo-alloc-size`
   (0eec vẫn ở nhánh *too-big*, chưa phải nhánh *small-wrap → heap-overflow* nguy hiểm).

---

## Reproduce recipe

```sh
export LD_LIBRARY_PATH=/home/hoangnh8/miniconda3/lib
export ASAN_OPTIONS=detect_container_overflow=0:detect_leaks=0:allocator_may_return_null=1:symbolize=1
BIN=research/schema/pdf-proto/build-podofo-fuzz/pdf_fuzzer_podofo
ART=research/experiments/libfuzzer-podofo-run5/artifacts

/usr/bin/time -v "$BIN" -rss_limit_mb=2048 "$ART/slow-unit-0eec12cec4beae2950830e32f938d92dd36461d2"
# libFuzzer: "Executed ... in 105 ms"  (không crash, không slow)
```

Artifacts là **text protobuf** (harness dùng `DEFINE_PROTO_FUZZER` → `DEFINE_TEXT_PROTO_FUZZER`,
`use_binary=false`), nên đọc/sửa tay được để làm ablation.

## Phương pháp

- **A. Reproduce + đo:** `/usr/bin/time -v` → wall-ms (libFuzzer tự in) + peak RSS + user/sys, ×3 lần.
- **B. Localize:** `perf` bị chặn (`perf_event_paranoid=4`); dùng **gdb** breakpoint tại
  `PdfPredictorDecoder` ctor + ASan report. (Không cần sampling vì unit chỉ ~10–100 ms.)
- **C. Ablation:** sửa bản copy text-proto trong scratchpad, đổi từng field → đo lại (linear /
  đâu là driver).

---

## Đo lường

| unit | shape (pages / stream objects) | exec | peak RSS | user/sys | crash |
|---|---|---|---|---|---|
| **958f** | 4 pages · **18 fonts** (FONTFILE3 CFF) · 0 cs · 0 img | ~10 ms | 56 MB | 0.01 / 0.01 s | không |
| **d007** | 16 pages · **120 content_streams** · 1 img · 0 fonts | ~13 ms | 62 MB | 0.01 / 0.02 s | không |
| **0eec** | 2 pages · 4 cs · 6 fonts · **8 images** (bpc≈3.9e9) | ~105 ms | 410 MB | 0.02 / 0.11 s | không |

Harness ép **mọi** object có stream qua `GetFilteredCopy` (`harness_podofo.cpp:48-58`, **không
cap** — cap `≤10 pages` chỉ áp cho tokenizer loop `:33`). Số stream object = cột "shape".

---

## Per-unit

### 0eec — image `BitsPerComponent` overflow → alloc-size (⇒ DUP `podofo-alloc-size`)

**Ablation (chỉ `bits_per_component` là driver):**

| biến thể | exec | peak RSS |
|---|---|---|
| orig | 105 ms | 410 MB |
| `length_delta: 808464433 → 0` | 107 ms | 409 MB | ← **inert** |
| xóa `call_local: 65533` | 106 ms | 409 MB | ← **inert** |
| `bits_per_component → 8` | **9 ms** | **55 MB** | ← **driver** |
| `height → 8` (giữ bpc lớn) | 106 ms | 410 MB | ← không phải height |

**gdb + ASan (allocator_may_return_null=0 ⇒ abort):**

```
==ERROR: AddressSanitizer: requested allocation size 0xfffffffffd1fffff
         exceeds maximum supported size of 0x10000000000
  #0 calloc
  #1 PoDoFo::PdfPredictorDecoder::PdfPredictorDecoder   base/PdfFiltersPrivate.cpp:112
  #2 PoDoFo::PdfFlateFilter::BeginDecodeImpl            base/PdfFiltersPrivate.cpp:605
  #4 PoDoFo::PdfFilteredDecodeStream::PdfFilteredDecodeStream  base/PdfFilter.cpp:170
  #5 PoDoFo::PdfFilterFactory::CreateDecodeStream       base/PdfFilter.cpp:366
  #6 PoDoFo::PdfStream::GetFilteredCopy                 base/PdfStream.cpp:96
  #7 TestOneProtoInput                                  harness_podofo.cpp:54
SUMMARY: allocation-size-too-big ... PdfFiltersPrivate.cpp:112 in PdfPredictorDecoder
```

- **Cùng dòng `:112`, cùng predictor ctor** như `findings/podofo-alloc-size` — chỉ khác nhánh
  **FLATE** (`:605`) thay vì LZW (`:767`). `m_nRows = (m_nColumns*m_nColors*m_nBPC) >> 3` với
  `BitsPerComponent` (m_nBPC) không giới hạn ⇒ request `0xfffffffffd1fffff` (~18 EB, giá trị **đã
  wrap** — đúng nhánh integer-overflow CWE-190 mà finding đã cảnh báo).
- **Hai regime ASan:**
  - `allocator_may_return_null=0` (mặc định): **abort** `allocation-size-too-big`, RSS ~51 MB (chết
    trước khi cấp phát lớn).
  - `allocator_may_return_null=1` (config của run5): in `WARNING: failed to allocate
    0xfffffffffd1fffff bytes` **×8** (đúng bằng 8 image streams) rồi trả `null` ⇒ PoDoFo bắt ⇒
    **không crash**. RSS ~410 MB là churn bookkeeping/các stream còn lại.
- **Phân loại:** CWE-789 (unvalidated allocation size) + CWE-190 (integer overflow) → OOM/DoS.
  **Verdict: DUP `findings/podofo-alloc-size`.** Vẫn ở nhánh *too-big* (giá trị lớn → null/abort),
  **chưa** phải nhánh *small-wrap* (giá trị nhỏ → buffer thiếu → heap-overflow) mà finding liệt kê
  là TODO nguy hiểm. Không phải bug mới.

### d007 — nhiều `content_streams` (breadth, lành tính)

- 16 pages × ~7 `content_streams` = 120 stream object nhỏ (nhiều rỗng / FLATE ngắn), harness
  decode hết qua loop không-cap. Tổng ~13 ms / 62 MB.
- `length_delta: 7424 / 28672` **inert**: zero hết → 12 ms → 11 ms, RSS 61 → 63 MB. PoDoFo bound
  read theo dữ liệu thật nên `/Length` phồng không tốn gì.
- Clean với `allocator_may_return_null=0` (không chạm alloc path). **Không phải finding.**

### 958f — nhiều `FONTFILE3` CFF font-file (breadth, lành tính)

- 4 pages × ~4–5 `fonts`, mỗi font 1 `FONTFILE3` stream = 18 font-program stream nhỏ. ~10 ms / 56 MB.
- Serializer nén CFF (`SerializeCff`) là pass tuyến tính; PoDoFo (FontManager **tắt** trong build)
  không diễn giải charstring ⇒ chỉ decode filter của stream. Clean với `null=0`. **Không phải finding.**

---

## Vì sao 3 unit bị flag "slow" (giải thích môi trường)

`report_slow_units=10` đo **wall-time** của 1 lần `LLVMFuzzerTestOneInput`, không phải CPU-time.
Bằng chứng slow là do môi trường, **không** do input:

1. **Replay standalone (host rảnh) đều nhanh** (10 / 13 / 105 ms) và ổn định — chính các byte đã
   lưu; nếu input thực sự O(n²) thì replay cũng phải chậm.
2. **0eec là memory/paging-bound**, không CPU-bound: `user=0.02 s` nhưng `sys=0.11 s`, RSS 410 MB,
   và ASan phải xử lý **8×** request 18 EB (fail). Dưới áp lực RAM, phần này thành swap-thrash → giây.
3. **Run 24 h ở sát trần RAM:** README ghi host chỉ ~4 GB trống, `-fork=3`, `-rss_limit_mb=2048`.
   `coverage.log` cho RSS tổng 3 fork: **median 1684 MB · p90 1980 MB · max 2465 MB**. Một spike của
   worker khi gặp unit image-overflow đẩy hệ thống vào swap.
4. **Fork mode giữ RSS xuyên suốt 1 job** (một worker chạy tuần tự nhiều unit trong 1 process). Khi
   worker đã phình vì unit 0eec-class, **cả unit tầm thường phía sau** (d007/958f, ~10 ms việc thật)
   cũng đo được >10 s wall-time → bị lưu thành `slow-unit`.

⇒ Nguồn nhiễu chung: allocation image-overflow (0eec-class) làm worker thrash dưới memory pressure;
d007/958f chỉ là "nạn nhân đi kèm" trong cùng worker.

---

## Dedup & phân loại (tổng hợp)

| unit | verdict | liên hệ |
|---|---|---|
| 0eec | **DUP** | `findings/podofo-alloc-size` (predictor overflow, `PdfFiltersPrivate.cpp:112`, nhánh FLATE) |
| d007 | không finding | breadth lành tính; `length_delta` inert |
| 958f | không finding | breadth lành tính; PoDoFo không parse CFF charstring |

- CFF `call_local: 65533` recursion **KHÁC** `findings/xpdf402-cff-cvtglyph-stackoverflow` (đó là
  **xpdf** `FoFiType1C::cvtGlyph`). Trên PoDoFo 0.9.7 (harness này) path đó **inert** — không tạo cost.

---

## Khuyến nghị (cho các run PoDoFo sau)

1. **Harness:** thêm cap cho loop `GetFilteredCopy` (`harness_podofo.cpp:48-58`) — giới hạn số
   stream object và/hoặc decoded size mỗi input; hoặc clamp/validate image dimensions
   (`BitsPerComponent`, `Height`) & `/Predictor` DecodeParms trước khi decode. Hiện chỉ tokenizer
   loop bị cap `≤10 pages`; loop decode thì không.
2. **Giảm memory pressure:** giảm `PODOFO_FORKS` hoặc `-rss_limit_mb` mỗi fork, hoặc chạy host rảnh.
   Vì slow-unit đo wall-time nên rất nhạy swap → sinh nhiễu như run này.
3. **`allocator_may_return_null=1`** đúng để de-dup crash alloc-size đã biết, nhưng biến nó thành
   `slow-unit` dưới pressure. Cân nhắc **sửa gốc**: bound `BitsPerComponent` (≤16) trong
   `PdfPredictorDecoder` — vừa hết crash `podofo-alloc-size` vừa hết nhiễu slow-unit.
4. **Triage replay slow-unit trên host rảnh** (như report này) để tách slow thật vs môi trường
   trước khi kết luận.

## Cập nhật đề xuất cho `findings/podofo-alloc-size`

- Ghi nhận reproduce **thứ hai** qua nhánh **FLATE** (`PdfFiltersPrivate.cpp:605`), bổ sung cho
  nhánh LZW (`:767`) đã có — cùng ctor `:112`.
- 0eec (`bits_per_component: 3909091320`) request `0xfffffffffd1fffff`: vẫn *too-big*, **chưa** kích
  hoạt nhánh small-wrap → heap-overflow (TODO nguy hiểm vẫn để ngỏ).

## Vật liệu

- Artifacts gốc: `artifacts/slow-unit-{0eec…, 958f…, d007…}` (đã có trong repo).
- Biến thể ablation + log đo: **scratchpad** (`…/scratchpad/podofo-slow-triage/`, không commit) —
  `0eec-{orig,len0,imgsmall,nocall,bpc8,h8}.txtpb`, `d007-len0.txtpb`, `run-*.err`.
