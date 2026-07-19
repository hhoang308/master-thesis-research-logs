# xpdf 4.02 — NULL-pointer deref trong `Catalog::~Catalog`

**Target:** xpdf 4.02 · `pdftops` (và mọi tool dùng `PDFDoc`)
**Lớp lỗi:** CWE-476 NULL Pointer Dereference (con trỏ `GList` chưa khởi tạo) → SIGSEGV → DoS
**Trạng thái:** ✅ **ĐÃ VÁ trong xpdf 4.06** — chỉ có giá trị *tái hiện* trên 4.02, **KHÔNG phải bug mới**.

## Differential 4.02 ↔ 4.06 (đã kiểm chứng)

| | xpdf 4.02 | xpdf 4.06 |
|---|---|---|
| plain build | **CRASH** (SIGSEGV) | ok (exit 1) |
| ASan build | `SEGV … Catalog::~Catalog` | **CLEAN** (không lỗi) |

4.06 thêm kiểm tra null tường minh: in `Catalog object is wrong type (null)` →
`Couldn't read page catalog` rồi thoát an toàn thay vì deref trong destructor.
Đây là **fix thật**. ⇒ Xếp vào nhóm "known/fixed", dùng để validate pipeline
trên 4.02, không tính là phát hiện mới.

## Mô tả

Trên một PDF hỏng, quá trình dựng `Catalog` thất bại/dừng giữa chừng khiến một
thành viên kiểu `GList*` chưa được khởi tạo. Khi `Catalog::~Catalog()` chạy để
dọn dẹp, nó gọi `getLength()` trên con trỏ `GList` đó (NULL/rác) → SEGV. Stack
rất nông (9 frame) — **không phải đệ quy**, khác hẳn CVE-2020-35376.

## ASan trace (đại diện — `crash.pdf`)

```
AddressSanitizer: SEGV on unknown address
  #0 GList::getLength()        goo/GList.h:39
  #1 Catalog::~Catalog()       xpdf/Catalog.cc:295
  #2 PDFDoc::setup2(...)       xpdf/PDFDoc.cc:312
  #3 PDFDoc::setup(...)        xpdf/PDFDoc.cc:266
  #4 PDFDoc::PDFDoc(...)       xpdf/PDFDoc.cc:208
  #5 main                      xpdf/pdftops.cc:309
```

## Tái hiện

```sh
REPO_ROOT=$(git rev-parse --show-toplevel)
RESEARCH=$REPO_ROOT/research
ASAN=$RESEARCH/thesis/xpdf-4.02/build-asan/xpdf/pdftops   # gcc-11 + -fsanitize=address
ASAN_OPTIONS=detect_leaks=0 "$ASAN" crash.pdf /dev/null
```

## Nguồn gốc (provenance)

- Tìm bởi **AFL++ byte-level** fuzz `pdftops` (build `build-afl`, afl-clang-fast).
- Chiến dịch: `experiments/afl-xpdf402-cve35376-seed` (run thử 4 phút).
- Seed cha: `acroform_calculation_order.pdf` (từ `seeds/min`) — **KHÔNG phải poc.pdf**.
- 62 input cùng chữ ký này (1 bug, nhiều biến thể) được giữ trong `raw-crashes/`.
- `crash-min.pdf` (21 B) — bản `afl-tmin` tối giản, vẫn tái hiện `Catalog::~Catalog`.
  Chỉ 21 byte đủ để crash ⇒ bug này bắn trên **gần như mọi PDF hỏng** (một
  "crash attractor" — xem ghi chú ở 2 finding kia).

## Ghi chú

Đây là bug **độc lập** với CVE-2020-35376 (CVE nằm ở `FoFiType1C::cvtGlyph`, đệ
quy sâu). Cần: minimize (`afl-tmin`), xác nhận còn crash trên xpdf 4.06/mới nhất
để phân loại known/new trước khi báo upstream.
