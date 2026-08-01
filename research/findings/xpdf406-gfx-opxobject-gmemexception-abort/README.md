# xpdf 4.06 — Uncaught `GMemException` → `std::terminate`/abort trong `Gfx::opXObject`

**Target:** xpdf 4.06 · harness `pdf_fuzzer` (proto libFuzzer + ASan, link từ source `thesis/xpdf-4.06`)
**Lớp lỗi:** Uncaught C++ exception (CWE-248) trên input hỏng → `abort()` → DoS
**Trạng thái:** ⚠️ **Harness-only** — là **lỗ hổng của HARNESS**, KHÔNG phải bug của tool xpdf 4.06.
Cùng lớp với [`xpdf402-uncaught-gmemexception-abort`](../xpdf402-uncaught-gmemexception-abort/),
nhưng chạm tới qua đường **Gfx / XObject** (không phải đường xref/parse của finding 4.02).

## Tóm tắt (điểm mấu chốt)

xpdf **4.06** cố tình bắt `GMemException` trong `Gfx::opXObject` (toán tử `Do`),
dọn dẹp rồi **rethrow** ngược lên (`Gfx.cc:4241`). Ai gọi `displayPages` phải bắt.
Các tool chính thức 4.06 (`pdftotext`/`pdftops`) **không abort** trên input này; harness
`pdf_fuzzer` (`DEFINE_PROTO_FUZZER`) gọi `displayPages` **không có try/catch** ⇒ exception
lọt ra ⇒ `terminate` ⇒ `abort`. libFuzzer coi đó là "deadly signal" và **campaign chết**.

## Differential trên cùng một `crash.pdf` (đã kiểm chứng)

| Chạy | Kết quả |
|---|---|
| **Harness 4.06** `build-406/pdf_fuzzer` (không try/catch) | **ABORT** — `terminate … 'GMemException'`, `Gfx::opXObject` `Gfx.cc:4241` (exit 77) |
| Tool 4.06 chính thức `build/xpdf/pdftotext` | exit 0 — chỉ `Syntax Error`, **không abort** |
| Tool 4.06 chính thức `build/xpdf/pdftops` | exit 98 — lỗi có kiểm soát, **không abort** |
| Tool 4.06 ASan `build-asan/xpdf/pdftops` | exit 98 — **CLEAN**, không GMemException |
| Tool **4.02** ASan `build-asan/xpdf/pdftops` | **ABORT** — `terminate … 'GMemException'` (exit 134) |

⇒ 4.06 tool đã "chịu được" input này; chỉ **harness** (thiếu try/catch) mới abort.
4.02 tool thì genuine crash (khớp finding 4.02). ⇒ nhóm **known-class / harness-gap, ưu tiên thấp**.

## Cơ chế

Input (xem `crash.txtpb`): mỗi page có một content stream với `length_delta:
16777216` (bơm trường `Length` thêm 16 MB) kèm ảnh `RAW` DeviceRGB / `JPX`. Khi render
XObject qua `Do`, xpdf cấp phát trong `goo/gmem` theo kích thước lấy từ dữ liệu file
(bị bơm khổng lồ) → `gmem` ném `GMemException` (guard chống cấp phát bất thường).

`Gfx::opXObject` (4.06) — `thesis/xpdf-4.06/xpdf/Gfx.cc:4241`:

```cpp
#if USE_EXCEPTIONS
  } catch (GMemException e) {
    xObj.free();
    refObj.free();
    throw;            // ← rethrow (frame __cxa_rethrow); phải được bắt ở tầng trên
  }
#endif
```

Stack lúc abort (harness 4.06):

```
terminate called after throwing an instance of 'GMemException'
  #8  abort
  #11 std::terminate()
  #12 __cxa_rethrow
  #13 Gfx::opXObject(Object*, int)          xpdf-4.06/xpdf/Gfx.cc:4241
  #14 Gfx::execOp                           Gfx.cc:862
  #15 Gfx::go                               Gfx.cc:747
  #16 Gfx::display                          Gfx.cc:669
  #17 Page::displaySlice                    Page.cc:440
  #18 Page::display                         Page.cc:386
  #19 PDFDoc::displayPage                   PDFDoc.cc:446
  #20 PDFDoc::displayPages                  PDFDoc.cc:465
  #21 TestOneProtoInput                     harness.cpp:76   ← gọi displayPages, KHÔNG try/catch
  #22 LLVMFuzzerTestOneInput               harness.cpp:48
```

## Tái hiện

```sh
cd research/schema/pdf-proto
FIND=../../findings/xpdf406-gfx-opxobject-gmemexception-abort

# 1) Trực tiếp trên harness (proto txt) — ABORT, deterministic
ASAN_OPTIONS=detect_container_overflow=0:detect_leaks=0 \
  ./build-406/pdf_fuzzer "$FIND/crash.txtpb"
# stderr: terminate called after throwing an instance of 'GMemException'  (exit 77)

# 2) Chuyển proto -> pdf (đã kèm sẵn crash.pdf; tái tạo bằng:)
#    cmake --build build-406 --target proto2pdf
#    ./build-406/proto2pdf "$FIND/crash.txtpb" "$FIND/crash.pdf"

# 3) Tool 4.06 chính thức KHÔNG abort (đối chứng)
../../thesis/xpdf-4.06/build/xpdf/pdftotext "$FIND/crash.pdf" /dev/null ; echo "exit=$?"   # 0
ASAN_OPTIONS=detect_leaks=0 ../../thesis/xpdf-4.06/build-asan/xpdf/pdftops "$FIND/crash.pdf" /dev/null ; echo "exit=$?"  # 98, clean

# 4) Cross-version: tool 4.02 thì ABORT
ASAN_OPTIONS=detect_leaks=0 ../../thesis/xpdf-4.02/build-asan/xpdf/pdftops "$FIND/crash.pdf" /dev/null ; echo "exit=$?"  # 134 (GMemException)
```

## Nguồn gốc

- Chiến dịch: **long fuzz xpdf-4.06** (libFuzzer fork-mode, proto custom mutator).
  Run dir: `research/schema/pdf-proto/fuzz-runs/20260727T175021Z-3227057-long/`.
- Commit: `3227057`. Harness: `build-406/pdf_fuzzer` (xpdf-4.06, `XPDF_LEGACY_DISPLAYPAGES=OFF`).
- Seed: `fuzz-corpus/main`. Reproducer libFuzzer lưu tại `crash.txtpb` (base unit `12e92cdd…`,
  chuỗi mutation `CustomCrossOver`).
- Run dừng sớm (~80') vì abort này thoát ra khỏi harness (artifact ghi ra `./` với prefix mặc
  định thay vì `-artifact_prefix`), cộng thêm host cạn RAM (worker ~1.1 GB, swap đầy, exec/s tụt còn 5).

## Phân loại & khắc phục

- **Không phải bug mới của xpdf 4.06.** Tool chính thức 4.06 không crash; đây là **guard OOM có
  chủ đích** (`gmem` ném `GMemException` khi kích thước cấp phát phi lý — do `length_delta` 16 MB
  bị fuzz), và 4.06 đã rethrow đúng để tầng trên xử lý. Không phải lỗi memory-safety (0 ASan
  heap-overflow/UAF).
- **Đây là lỗ hổng của HARNESS:** `DEFINE_PROTO_FUZZER` không bắt exception. Hệ quả trực tiếp:
  campaign long **chết/kẹt** ở lớp abort đã-biết-này thay vì đi săn bug thật — đúng thứ mà việc
  đổi sang 4.06 muốn tránh nhưng không tránh được, vì gốc rễ nằm ở harness chứ không ở phiên bản.
- **Khắc phục đề xuất** (để campaign lướt qua & không tính là crash):

  ```cpp
  // harness.cpp — bọc displayPages
  try {
      pdf->displayPages(&dev, NULL, 1, n, 72, 72, 0, gFalse, gTrue, gFalse);
  } catch (GMemException &e) {   // guard OOM đã biết của xpdf → bỏ qua
  } catch (...) { }
  ```

  Kèm `-rss_limit_mb`/`-malloc_limit_mb` và giảm `-max_len` để libFuzzer coi cấp-phát-khổng-lồ là
  OOM (bỏ qua được trong fork mode) thay vì để tiến trình abort.

## Liên quan

- [`xpdf402-uncaught-gmemexception-abort`](../xpdf402-uncaught-gmemexception-abort/) — cùng lớp,
  đường xref/parse qua `pdftops`; finding đó ghi 4.06 "clean" cho **input của nó**. Finding này
  bổ sung: lớp `GMemException`-abort **vẫn chạm tới được trên 4.06** qua đường Gfx/XObject, nhưng
  chỉ khi harness không bắt exception.
- [`podofo-alloc-size`](../podofo-alloc-size/) — cùng mô-típ alloc-size do trường độ dài bị bơm.
