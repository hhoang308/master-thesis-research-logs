# xpdf 4.02 — Uncaught `GMemException` → `std::terminate`/abort

**Target:** xpdf 4.02 · `pdftops`
**Lớp lỗi:** Uncaught C++ exception (CWE-248) trên input hỏng → `abort()` → DoS
**Trạng thái:** ✅ **ĐÃ VÁ trong xpdf 4.06** — chỉ có giá trị *tái hiện* trên 4.02, **KHÔNG phải bug mới**.

## Differential 4.02 ↔ 4.06 (đã kiểm chứng)

| | xpdf 4.02 | xpdf 4.06 |
|---|---|---|
| plain build | **CRASH** (SIGABRT) | ok (exit 0) |
| ASan build | `terminate … GMemException` | **CLEAN** (không lỗi) |

4.06 phục hồi xref và không còn ném `GMemException` không bắt với input này.
⇒ nhóm "known/fixed".

## Mô tả

Khi parse PDF hỏng, xpdf gọi cấp phát bộ nhớ trong `goo/gmem` với kích thước
lấy từ dữ liệu file (thường là length/count bị bơm lớn). Cấp phát thất bại/không
hợp lệ → ném `GMemException`. `pdftops` **không bắt** exception này ở tầng trên
cùng → `terminate called after throwing an instance of 'GMemException'` → `abort()`.

```
terminate called after throwing an instance of 'GMemException'
AddressSanitizer: ABRT
  #2 abort            libc
  #3 (libstdc++ __cxa_throw/terminate handler)
```

## Tái hiện

```sh
REPO_ROOT=$(git rev-parse --show-toplevel)
RESEARCH=$REPO_ROOT/research
ASAN=$RESEARCH/thesis/xpdf-4.02/build-asan/xpdf/pdftops
ASAN_OPTIONS=detect_leaks=0 "$ASAN" crash.pdf /dev/null
# stderr: "terminate called after throwing an instance of 'GMemException'"
```

## Nguồn gốc

- Tìm bởi **AFL++ byte-level** fuzz `pdftops` (build `build-afl`).
- Chiến dịch: `experiments/afl-xpdf402-cve35376-seed`, input `id:000007` (sig:06 / SIGABRT).
- Seed cha: `acroform_calculation_order.pdf` — **KHÔNG** phải poc.pdf.

## Minimization

Giữ nguyên `crash.pdf` (1040 B), **không** kèm bản tối giản: `afl-tmin` theo tín
hiệu crash co input về ~21 byte và rơi vào `Catalog::~Catalog` (NULL-deref bắn
trên mọi PDF hỏng), làm mất chữ ký `GMemException`. `crash.pdf` vẫn cho đúng
`terminate ... 'GMemException'`.

## Ghi chú / cảnh báo phân loại

Đây có thể là **hành vi phòng vệ có chủ đích** của xpdf (ném `GMemException` khi
cấp phát bất thường) chứ không phải lỗi bộ nhớ. Với `USE_EXCEPTIONS=ON`, việc
exception không được bắt ở `main` → abort vẫn là một dạng crash/DoS trên input
hỏng, nhưng **ưu tiên thấp** hơn NULL-deref và đệ quy. Cần kiểm tra xem đây có
phải OOM-guard hợp lệ (kích thước cấp phát khổng lồ) hay một allocation thật sự
sai. Đối chiếu với `findings/podofo-alloc-size` (cùng mô-típ alloc-size).
