# xpdf 4.02 — Đệ quy không kiểm soát trong `AcroForm::scanField`

**Target:** xpdf 4.02 · `pdftops` (và mọi tool nạp AcroForm)
**Lớp lỗi:** CWE-674 Uncontrolled Recursion → stack exhaustion → SIGSEGV → DoS
**Trạng thái:** ✅ **ĐÃ VÁ trong xpdf 4.06** — chỉ có giá trị *tái hiện* trên 4.02, **KHÔNG phải bug mới**.

## Differential 4.02 ↔ 4.06 (đã kiểm chứng)

| | xpdf 4.02 | xpdf 4.06 |
|---|---|---|
| plain build | **CRASH** (SIGSEGV / stack-overflow) | ok (exit 0) |
| ASan build | `stack-overflow … AcroForm::scanField` | **CLEAN** (không lỗi) |

4.06 không còn tràn stack ở `AcroForm::scanField` với input này (nhiều khả năng
đã thêm giới hạn độ sâu / xử lý field vòng). ⇒ nhóm "known/fixed".

## Mô tả

Cây field của AcroForm cho phép field trỏ tới nhau (qua `/Kids` hoặc tham chiếu
lồng nhau). Với một cây có **vòng lặp / độ sâu không giới hạn**, `AcroForm::scanField()`
tự gọi chính nó lặp vô hạn (AcroForm.cc:271) → cạn stack. Frame lá rơi vào
`Dict::find` → `strcmp` chỉ là nơi stack hết, **gốc lỗi là đệ quy `scanField`**.

Cùng lớp lỗi (đệ quy vô hạn) với CVE-2020-35376 nhưng ở **hệ con hoàn toàn khác**
(AcroForm, không phải bộ giải font CFF). Không có giới hạn độ sâu trong 4.02.

## ASan trace (`crash.pdf`)

```
AddressSanitizer: stack-overflow
  #0 __interceptor_strcmp
  #1 Dict::find(char const*)          xpdf/Dict.cc:98
  #2 Dict::lookup(...)                xpdf/Dict.cc:125
  #3 Object::dictLookup(...)          xpdf/Object.h:267
  #4 AcroForm::scanField(Object*)     xpdf/AcroForm.cc:256
  #5 AcroForm::scanField(Object*)     xpdf/AcroForm.cc:271   <-- đệ quy
  #6 AcroForm::scanField(Object*)     xpdf/AcroForm.cc:271
  ...  (lặp lại tới ~251 frame)
```

## Tái hiện

```sh
REPO_ROOT=$(git rev-parse --show-toplevel)
RESEARCH=$REPO_ROOT/research
ASAN=$RESEARCH/thesis/xpdf-4.02/build-asan/xpdf/pdftops
ASAN_OPTIONS=detect_leaks=0 "$ASAN" crash.pdf /dev/null
```

## Nguồn gốc

- Tìm bởi **AFL++ byte-level** fuzz `pdftops` (build `build-afl`).
- Chiến dịch: `experiments/afl-xpdf402-cve35376-seed` (run thử 4 phút), input `id:000048`.
- Seed cha: `acroform_calculation_order.pdf` — hợp lý vì đây là PDF có AcroForm.
- **KHÔNG** liên quan poc.pdf — bug độc lập, tự tìm ra từ seed hợp lệ.

## Minimization

Giữ nguyên `crash.pdf` (1126 B) — **không** kèm bản tối giản. Lý do: `afl-tmin`
theo tín hiệu crash luôn co input về ~20 byte và khi đó rơi vào bug
`Catalog::~Catalog` (NULL-deref bắn trên mọi PDF hỏng — "crash attractor"), làm
mất chữ ký `AcroForm::scanField`. Bug này cần một PDF đủ hợp lệ để có AcroForm
với field tự tham chiếu, nên ~1.1 KB gần như đã là tối giản. (Kiểm chứng: `crash.pdf`
vẫn cho đúng frame `AcroForm::scanField`.)

## Ghi chú

Đây là một điểm CWE-674 *mới* (khác vị trí CVE). Nên kiểm tra xpdf 4.06/mới nhất
xem đã có `recursionLimit` cho AcroForm chưa để phân loại known/new.
