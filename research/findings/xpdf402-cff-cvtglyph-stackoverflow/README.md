# xpdf 4.02 — CFF/Type1C charstring recursion → stack-overflow

**Target:** xpdf 4.02 · `pdftops` (parser CFF/Type1C: `fofi/FoFiType1C.cc`)
**Lớp lỗi:** Uncontrolled recursion / stack exhaustion (CWE-674) trên font `/FontFile3` (Type1C/CFF) → `SIGSEGV` → DoS
**Trạng thái:** Reproduce trên 4.02 · **KHÔNG** reproduce trên 4.06 (xem differential) — nhóm "known/fixed".

## Differential 4.02 ↔ 4.06 (đã kiểm chứng)

| | xpdf 4.02 (ASan) | xpdf 4.06 (ASan) |
|---|---|---|
| `crash.pdf` | **CRASH** — `stack-overflow` | ok (exit 0) |

## Mô tả

`FoFiType1C::cvtGlyph()` diễn giải Type 2 charstring của glyph. Khi gặp toán tử
`callsubr`/`callgsubr`, nó **gọi đệ quy chính nó** để nhảy vào subroutine:

```c
// fofi/FoFiType1C.cc:1507
cvtGlyph(val.pos, val.len, charBuf, subrIdx, pDict, gFalse);
```

Không có giới hạn độ sâu đệ quy. Một font CFF nhúng với subroutine tự tham chiếu
(hoặc lồng nhau rất sâu) khiến `cvtGlyph` đệ quy không kiểm soát → tràn stack.
ASan bắt được ở `FoFiType1C::getOp` (fofi/FoFiType1C.cc:3161), ngay dưới chuỗi
~248 khung `cvtGlyph` (fofi/FoFiType1C.cc:1313 ↔ 1507).

```
ERROR: AddressSanitizer: stack-overflow
  #0 FoFiType1C::getOp()                fofi/FoFiType1C.cc:3161
  #1 FoFiType1C::cvtGlyph()             fofi/FoFiType1C.cc:1313
  #2 FoFiType1C::cvtGlyph()             fofi/FoFiType1C.cc:1507   <- callsubr đệ quy
  ... (~248 khung cvtGlyph) ...
```

## Tái hiện

```sh
RESEARCH=/home/hoangnh8/master-thesis-research-logs/research
ASAN=$RESEARCH/thesis/xpdf-4.02/build-asan/xpdf/pdftops
ASAN_OPTIONS=detect_leaks=0 "$ASAN" crash.pdf /dev/null
# stderr: "AddressSanitizer: stack-overflow ... in FoFiType1C::getOp"
```

## Nguồn gốc

- Campaign: `experiments/afl-xpdf402-cve35376-seed` (AFL++ byte-level fuzz `pdftops`).
- Input: `id:000000,sig:11,...,op:dry_run,orig:poc.pdf` (1022 B, SIGSEGV).
- **Bắt ở bước dry-run/calibrate seed** — tức **chính seed `poc.pdf`** (PoC của
  CVE-2020-35376) đã kích hoạt, KHÔNG phải mutation do fuzzer khám phá. Đây là một
  crash *khác* với CVE mà seed đó nhắm.
- Không có campaign nào nhắm CFF riêng; `FoFiType1C` mới chỉ được cover ngẫu nhiên.

## TODO

- [ ] **Kiểm tra CVE:** stack-overflow do đệ quy charstring CFF (`callsubr`/`callgsubr`
  không giới hạn độ sâu) là lớp lỗi quen thuộc ở nhiều PDF/font parser. Cần tra xem
  bug `FoFiType1C::cvtGlyph` này của xpdf 4.02 **đã có CVE chưa** (đối chiếu
  changelog/commit 4.02→4.06 để xác định bản vá và mã CVE nếu có), rồi cập nhật mục
  **Trạng thái** ở trên cho chính xác (known-CVE vs chỉ known-fixed).

## Ghi chú

Tràn stack do đệ quy là DoS (không phải corruption khai thác được), **ưu tiên trung
bình**. Đối chiếu với các finding cùng nhóm robustness: `xpdf402-uncaught-gmemexception-abort`,
`xpdf402-acroform-scanfield-recursion` (cũng là đệ quy không giới hạn, nhưng ở AcroForm).
