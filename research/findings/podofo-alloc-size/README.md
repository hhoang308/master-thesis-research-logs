# PoDoFo 0.9.7 — Predictor decoder unvalidated allocation size

**Target:** PoDoFo 0.9.7 · `PdfMemDocument.LoadFromBuffer` + `GetFilteredCopy` (filter chain).
**Lớp lỗi:** Excessive / unvalidated allocation size (CWE-789, kèm nguy cơ integer overflow CWE-190) → DoS.
**Trạng thái:** ✅ **Đã triage** trên ASan build từ source (podofo 0.9.7). (Trước đây chỉ là "candidate" chờ ASan build.)

## Root cause (đã định vị bằng ASan source build)

`base/PdfFiltersPrivate.cpp` — constructor `PdfPredictorDecoder` (chuỗi `/Predictor` của LZW/Flate):

```c
// PdfFiltersPrivate.cpp:110-112
m_nRows = (m_nColumns * m_nColors * m_nBPC) >> 3;   // Columns/Colors/BitsPerComponent: attacker-controlled
m_pPrev = static_cast<char*>(podofo_calloc( m_nRows, sizeof(char) ));   // <- alloc theo m_nRows
```

`BitsPerComponent` (`m_nBPC`) **không được giới hạn** (đáng lẽ ≤ 16). Input đặt `bits_per_component ≈ 4.17e9`
→ `m_nRows` khổng lồ → `calloc` vượt ngưỡng ASan (`allocation-size-too-big`). Phép nhân
`m_nColumns * m_nColors * m_nBPC` (int) còn có thể **tràn** → nếu wrap về giá trị nhỏ, `calloc` thành công
với buffer nhỏ nhưng predictor ghi theo `m_nBpp` → **nguy cơ heap-overflow** (chưa quan sát, cần khảo sát thêm).

## Stack (ASan)

```
ERROR: AddressSanitizer: allocation-size-too-big
  #1 PoDoFo::PdfPredictorDecoder::PdfPredictorDecoder   base/PdfFiltersPrivate.cpp:112
  #2 PoDoFo::PdfLZWFilter::BeginDecodeImpl              base/PdfFiltersPrivate.cpp:767
  #4 PoDoFo::PdfFilteredDecodeStream::PdfFilteredDecodeStream  base/PdfFilter.cpp:170
  #5 PoDoFo::PdfFilterFactory::CreateDecodeStream       base/PdfFilter.cpp:366
  #6 PoDoFo::PdfStream::GetFilteredCopy                 base/PdfStream.cpp:96
```

## Hành vi theo ASAN_OPTIONS
- `allocator_may_return_null=0` (mặc định): ASan **abort** `allocation-size-too-big`.
- `allocator_may_return_null=1`: ASan trả `null` → podofo bắt được (`PODOFO_RAISE_ERROR(ePdfError_OutOfMemory)`,
  dòng 113) → **không crash**. ⇒ đây là **OOM/DoS** (không phải corruption) *trong nhánh alloc-quá-lớn*; nhánh
  integer-overflow (nếu có) mới nguy hiểm hơn.

## Material
- `crash.pdf` / `crash-input.pdf` (893 B) — PDF đã serialize, replay trực tiếp.
- `crash-input.txtpb` — proto text (thấy rõ `images { ... bits_per_component: 4177526792, filter: LZW }`).
- `crash-input.txt` — artifact libFuzzer gốc (1 byte).

## Tái hiện (ASan source build)

Cần podofo 0.9.7 build từ source có ASan (local, gitignored):
`thesis/podofo-0.9.7/build-asan/` (clang + `-fsanitize=address`, `-DPODOFO_HAVE_OPENSSL_1_1`,
`-DPODOFO_NO_FONTMANAGER=TRUE`, hint `FREETYPE_LIBRARY_RELEASE`). Chạy `crash.pdf` qua một loader
`LoadFromBuffer + GetFilteredCopy` (mirror `schema/pdf-proto/harness_podofo.cpp`):

```sh
ASAN_OPTIONS=detect_leaks=0:detect_container_overflow=0 ./podofo_load crash.pdf
# stderr: "AddressSanitizer: allocation-size-too-big ... PdfPredictorDecoder ... PdfFiltersPrivate.cpp:112"
```

## TODO
- [ ] **Khảo sát nhánh integer-overflow**: dựng input làm `m_nColumns*m_nColors*m_nBPC` tràn về nhỏ,
  xem predictor có ghi OOB (`m_pPrev`) → nâng từ DoS thành memory-corruption không.
- [ ] **Kiểm tra CVE**: alloc-size/predictor của podofo 0.9.7 — tra xem đã có CVE / bản vá ở 0.9.8+/0.10 chưa.
