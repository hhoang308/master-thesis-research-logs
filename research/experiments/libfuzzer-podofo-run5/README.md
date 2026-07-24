# libFuzzer campaign — PoDoFo 0.9.7 — Run 5 — **coverage-guided vào source**

Run podofo đầu tiên mà **thư viện đích được instrument từ source**. Harness link
`thesis/podofo-0.9.7/build-asan/src/podofo/libpodofo.a` (clang
`-fsanitize=address,fuzzer-no-link` → ASan + SanitizerCoverage) ⇒ libFuzzer nhận coverage
feedback **bên trong parser của podofo** (`base/*.cpp`) và ASan poison heap của podofo.
Phỏng theo campaign CFF (`afl-xpdf402-cff-run1`) — build xpdf từ source để instrumentation
chạm tới target.

## Vì sao Run 5 khác Run 1–4 (điểm mấu chốt)

| | Runs 1–4 | **Run 5** |
|---|---|---|
| Link podofo | `/lib/libpodofo.so.0.9.7` (**system, KHÔNG instrument**) | `build-asan/.../libpodofo.a` (**from-source, instrumented**) |
| Coverage trong podofo | **0** — fuzzer mù trong parser | edge của `base/*.cpp` được feed vào libFuzzer |
| ASan trong podofo | không (chỉ bắt được qua interposer malloc) | có — poison heap trong code podofo |
| `cov:` sau khi nạp corpus run3 (567) | ~761 (đỉnh cả run) | **2334 ngay lúc INITED** (đo được) |

Runs 1–4 chỉ thấy coverage của harness + serializer + `pdf.pb.cc` + LPM. Bug tràn heap
đọc/ghi **bên trong** podofo trước đây sẽ lọt lưới (không có redzone trong code podofo).

## Build

```bash
./build.sh      # cmake configure + build pdf_fuzzer_podofo, rồi verify instrumentation
```

`build.sh` bật cờ CMake mới `PODOFO_SOURCE_BUILD=thesis/podofo-0.9.7/build-asan` (thêm ở
`schema/pdf-proto/CMakeLists.txt`) để link static archive instrumented thay cho system
libpodofo. Dùng **conda clang 18** (phải khớp compiler đã build `libpodofo.a`).

Binary: `schema/pdf-proto/build-podofo-fuzz/pdf_fuzzer_podofo` (36 MB, static podofo).
Verify tự động: không có dynamic libpodofo, có `PdfMemDocument`, có `__sanitizer_cov` + `__asan_`.

## Chạy

```bash
./run.sh 86400        # 24h; hoặc số giây nhỏ để sanity
# tùy chọn: PODOFO_FORKS=6 ./run.sh 86400
```

- Engine: libFuzzer + libprotobuf-mutator (structure-aware qua `pdf.proto`), `-fork=3`
  (`-ignore_crashes=1` để không dừng ở crash đã biết), `-rss_limit_mb=2048`.
- Host chỉ có ~4 GB RAM trống → `-fork=3` (mỗi worker ASan ~0.4 GB). Chỉnh qua `PODOFO_FORKS`.
- Corpus: seed từ run3 (567) + run4 (font/image, 10) = **572 file**.
- Log podofo verbose đã **tắt** trong harness (`PdfError::EnableLogging/EnableDebug(false)`,
  `LLVMFuzzerInitialize`) — trước đây làm `fuzzer.log` của run1 phình **6 GB**.

`ASAN_OPTIONS`: `detect_container_overflow=0` (chặn false-positive cross-over protobuf) ·
`detect_leaks=0` · `allocator_may_return_null=1` (alloc-size-too-big đã biết → null → podofo
bắt → không re-crash; heap-overflow/UAF **thật** vẫn được bắt).

## Bề mặt tấn công harness chạm tới (`harness_podofo.cpp`)

1. `PdfMemDocument::LoadFromBuffer` — xref / trailer / catalog / page-tree.
2. Với mỗi page (≤10): `PdfContentsTokenizer` → decode content stream + duyệt operator.
3. Với **mọi** object có stream: `GetStream()->GetFilteredCopy()` → chạy toàn bộ filter chain
   (Flate/LZW + `/Predictor`, ASCIIHex/85, RunLength, DCT…). Đây là nơi có finding alloc-size.

## Triage crash

> **Kết quả run:** 0 crash. 3 `slow-unit` đã triage → **`TRIAGE-slow-units.md`**: cả 3 nhanh khi
> replay standalone (10/13/105 ms), nhãn slow là artifact môi trường (memory pressure + fork);
> `slow-unit-0eec` = DUP `findings/podofo-alloc-size`. Không có bug mới.

Crash lưu ở `artifacts/crash-*` (ASan abort trong podofo). Mỗi crash:

```bash
export LD_LIBRARY_PATH=/home/hoangnh8/miniconda3/lib
ASAN_OPTIONS=detect_container_overflow=0:detect_leaks=0:symbolize=1 \
  ../../schema/pdf-proto/build-podofo-fuzz/pdf_fuzzer_podofo artifacts/crash-<hash>
```

Phân loại theo frame podofo (`PdfFiltersPrivate` / `PdfParser` / `PdfTokenizer` …). So với
finding đã có (`findings/podofo-alloc-size`) để loại trùng; ưu tiên nhánh
integer-overflow → heap-overflow còn bỏ ngỏ trong finding đó.

## Ghi chú
- Thay đổi hạ tầng (dùng lại cho các run podofo sau): cờ CMake `PODOFO_SOURCE_BUILD` +
  `LLVMFuzzerInitialize` tắt log, trong `schema/pdf-proto/`.
- `build-asan` của podofo build sẵn từ trước (đã dùng triage alloc-size). Nếu bị xóa: build
  lại 0.9.7 với clang `-fsanitize=address,fuzzer-no-link` (xem `findings/podofo-alloc-size`).
