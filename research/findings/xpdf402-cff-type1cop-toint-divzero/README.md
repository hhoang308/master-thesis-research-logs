# xpdf 4.02 — CFF/Type1C `Type1COp::toInt` division-by-zero → SIGFPE

**Target:** xpdf 4.02 · `pdftops` (parser CFF/Type1C: `fofi/FoFiType1C.cc`)
**Lớp lỗi:** Division by zero (CWE-369) trên font `/FontFile3` (Type1C/CFF) → `SIGFPE` → DoS
**Trạng thái:** Reproduce trên 4.02 · **KHÔNG** reproduce trên 4.06 — nhóm "known/fixed".

## Differential 4.02 ↔ 4.06 (đã kiểm chứng)

| | xpdf 4.02 (ASan) | xpdf 4.06 (ASan) |
|---|---|---|
| `crash.pdf` | **CRASH** — `FPE` | ok (exit 0) |

## Mô tả

Khi diễn giải Type 2 charstring, `FoFiType1C::cvtGlyph` gọi `Type1COp::toInt()` để ép một
operand về số nguyên. Với operand kiểu **rational**, hàm chia trực tiếp mà **không kiểm mẫu số**:

```c
// fofi/FoFiType1C.cc:53
case type1COpRational: return rat.num / rat.den;   // rat.den == 0  -> SIGFPE
```

Một font CFF nhúng tạo được operand rational có `den = 0` (ví dụ qua toán tử `div` 0x0c 0x0c
trong charstring) → chia cho 0 → SIGFPE. Số nguyên chia cho 0 trên x86 là fault phần cứng,
crash **không cần ASan** (ASan chỉ báo lại là `FPE`).

```
ERROR: AddressSanitizer: FPE
  #0 Type1COp::toInt()        fofi/FoFiType1C.cc:53   <- rat.num / rat.den, den==0
  #1 FoFiType1C::cvtGlyph()   fofi/FoFiType1C.cc:1835
  #2 FoFiType1C::cvtGlyph()   fofi/FoFiType1C.cc:1507 (đệ quy callsubr)
```

## Tái hiện

```sh
RESEARCH=/home/hoangnh8/master-thesis-research-logs/research
ASAN=$RESEARCH/thesis/xpdf-4.02/build-asan/xpdf/pdftops
ASAN_OPTIONS=detect_leaks=0 "$ASAN" crash.pdf /dev/null
# stderr: "AddressSanitizer: FPE ... in Type1COp::toInt"
```

## Nguồn gốc

- Campaign: `experiments/afl-xpdf402-cff-run1` (AFL++ structure-aware, mutator CFF → `pdftops`).
- **6 / 489** crash của campaign gom về bug này; 483 crash còn lại là stack-overflow đệ quy
  (finding `xpdf402-cff-cvtglyph-stackoverflow`). Đây là bug **thứ hai, phân biệt**, do
  fuzzing structure-aware sinh operand rational `den=0`.
- `crash.pdf` = một trong 6 input, đã serialize sẵn ra PDF (tự crash `pdftops` 4.02).

## TODO

- [ ] **Kiểm tra CVE:** chia-cho-0 trong bộ diễn giải charstring CFF của xpdf 4.02
  (`Type1COp::toInt`) — tra xem đã có CVE chưa (đối chiếu changelog/commit 4.02→4.06 tìm bản vá),
  rồi cập nhật mục **Trạng thái** (known-CVE vs chỉ known-fixed).

## Ghi chú

SIGFPE do chia-cho-0 là DoS (không phải corruption khai thác được), **ưu tiên trung bình**.
Cùng file/đường đi với `xpdf402-cff-cvtglyph-stackoverflow` (đều trong `cvtGlyph`), nhưng
**gốc khác** (chia-cho-0 ở `toInt` vs đệ quy không giới hạn) ⇒ finding riêng.
