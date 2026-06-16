## Mục tiêu luận văn

Xây dựng một pipeline structure-aware fuzzing cho định dạng PDF sử dụng libFuzzer +
libprotobuf-mutator, chứng minh rằng phương pháp này đạt coverage cao hơn và tìm được
nhiều lỗi hơn so với AFL++ thuần tuý trên các PDF reader ít được fuzzing liên tục.

**Hai tiêu chí đo lường:**
1. Tỉ lệ input hợp lệ đạt 60-80% (so với AFL++ baseline).
2. Coverage cao hơn AFL++ baseline sau cùng thời gian chạy.

**Đủ để bảo vệ luận văn thạc sĩ.** Nếu tìm được CVE mới trong xpdf/PoDoFo/qpdf thì
có thể nâng lên thành workshop paper hoặc short paper.

---

## Phương pháp

```
pdf.proto  ->  libprotobuf-mutator mutates PdfDocument  ->  SerializePdf()  ->  raw PDF bytes  ->  xpdf / PoDoFo / qpdf
```

- Định nghĩa `.proto` schema mô tả cấu trúc PDF.
- libprotobuf-mutator tự động sinh và đột biến protobuf message theo schema -- không cần tự viết mutation logic.
- Serializer chuyển protobuf message sang raw PDF bytes, **tự tính xref offset và `/Length`** (điểm khác biệt so với AFLSmart và các chunk-level tool).
- Harness feed bytes vào target qua `DEFINE_PROTO_FUZZER`.

**Lý do bỏ phương án ban đầu (AFL++ + lopdf IR):**
- lopdf round-trip chỉ đạt 60% -- không đủ tin cậy.
- Tự viết L1/L2/L3 mutation logic đồng thời với parser là quá lớn về kỹ thuật.
- Phương án protobuf tách bạch rõ ràng: schema + serializer + libprotobuf-mutator. Chỉ một bài toán khó cần giải là serializer.

---

## Target

- **Ưu tiên** (ít được fuzzing liên tục): xpdf, PoDoFo, qpdf
- **Tránh** (đã có đội ngũ chạy fuzzer liên tục): pdfium, poppler, mupdf, pdf.js

---

## Tiến độ công việc

### 1. Baseline AFL++ [DONE]

- Seed corpus thu thập từ pdf.js và poppler test, lọc bằng `afl-cmin`.
- AFL++ chạy trên xpdf 4.06 (`pdftotext`), đang chạy liên tục:

| Thông số | Giá trị |
|---|---|
| Thời gian chạy | ~86 giờ |
| Tổng executions | 101M |
| Coverage | 47.69% (15,006 / 31,464 edges) |
| Crashes | 0 |
| Hangs | 297 |

- Đây là số liệu baseline cho thực nghiệm so sánh.

---

### 2. Proto schema [DONE giai đoạn 1 / TODO giai đoạn 2]

**[DONE] Giai đoạn 1 -- Document skeleton tối giản**
- `PdfDocument` chứa danh sách `Page`, mỗi `Page` có `width` / `height` (MediaBox).
- Thêm `package pdf_proto` để tránh name collision với xpdf's `Catalog` class.
- File: `research/pdf-proto-schema/pdf.proto`

**[TODO] Giai đoạn 2 -- Stream object model** ← việc tiếp theo quan trọng nhất
- Thêm `StreamObject` với `filter` (NONE / FLATE / JPX) và `payload` (bytes).
- Thêm `content_stream` vào `Page`.
- Mục tiêu: cho phép fuzzer chạm đến các stream decoder trong xpdf (FlateDecode, JPXDecode, JBIG2,...).

**[TODO -- nếu còn thời gian] Giai đoạn 3**
- Font objects, JavaScript actions, name trees, arrays.

---

### 3. Serializer [DONE giai đoạn 1 / TODO giai đoạn 2]

**[DONE] Giai đoạn 1 -- PDF hợp lệ từ protobuf message**
- Tự tính xref offset và ghi đúng trailer.
- Kết quả xác nhận:
  - `pdfinfo`: Pages: 1, Page size: 612x792, PDF 1.4, 330 bytes -- không có lỗi.
  - `pdftotext`: không có lỗi parse.
- File: `research/pdf-proto-schema/serializer.cpp`

**[TODO] Giai đoạn 2 -- Stream object**
- Nén payload bằng zlib khi `filter = FLATE`.
- Ghi đúng `/Length` dựa trên kích thước thực của dữ liệu đã nén.
- Thêm `/Contents` reference vào Page dictionary.

---

### 4. Harness + libFuzzer [DONE giai đoạn 1 / TODO giai đoạn 2]

**[DONE] Giai đoạn 1 -- xpdf 4.06**
- `DEFINE_PROTO_FUZZER` nhận `pdf_proto::PdfDocument`, gọi `SerializePdf`, feed vào `PDFDoc` của xpdf.
- Build thành công với clang-14 + ASan + libprotobuf-mutator.
- Smoke test 100 runs: coverage tăng từ 15 lên 664 edges, `LLVMFuzzerCustomMutator` active.
- File: `research/pdf-proto-schema/harness.cpp`

**[TODO] Giai đoạn 2 -- Mở rộng target**
- Thêm harness cho PoDoFo.
- Thêm harness cho qpdf.

---

### 5. Thực nghiệm và so sánh [TODO]

- [ ] Chạy `pdf_fuzzer` 24h sau khi có stream objects, lặp 3 lần.
- [ ] Thu thập coverage curve theo thời gian (edges found vs time).
- [ ] So sánh trực tiếp: AFL++ baseline vs libFuzzer+protobuf-mutator.
- [ ] Nếu còn thời gian: so sánh thêm AFLSmart.
- [ ] Triage crash nếu có: stacktrace, root cause, reproduce.

---

## Điều kiện bảo vệ

1. **Kỹ thuật:** Schema + serializer + harness chạy được trên ít nhất một target.
2. **Thực nghiệm:** Chạy đủ 24h x 3 lần, có trung bình và độ lệch chuẩn.
3. **Kết quả:** Coverage hoặc valid input rate cao hơn AFL++ baseline.
4. **Crash:** Reproduce được nếu có. Nếu không: phân tích coverage chứng minh pipeline chạm đến code path sâu hơn AFL++.
5. **Phân tích:** Giải thích vì sao kết quả tốt hơn (hoặc không), nhận biết giới hạn, đề xuất hướng mở rộng.

---

## Cấu trúc luận văn

| Chương | Nội dung | Số trang |
|---|---|---|
| 1 -- Giới thiệu | Bài toán, đóng góp cụ thể, cấu trúc luận văn | 5-8 |
| 2 -- Kiến thức nền tảng | Coverage-guided fuzzing, libFuzzer, định dạng PDF (object model / xref / stream / filter), libprotobuf-mutator, structure-aware fuzzing | 15-20 |
| 3 -- Related work | AFLSmart, Superion, FormatFuzzer, pFuzzer -- bảng so sánh theo semantic awareness / serializer validity / xref handling -- khoảng trống đề tài lấp | 8-12 |
| 4 -- Thiết kế và triển khai | Schema design, serializer (tại sao xref+/Length quan trọng), harness, mở rộng schema với stream | 25-35 |
| 5 -- Thực nghiệm | Setup, coverage curve AFL++ vs libFuzzer+protobuf, case study crash hoặc coverage, thảo luận | 20-25 |
| 6 -- Kết luận | Tóm tắt đóng góp, hướng mở rộng | 3-5 |

---

## Khái niệm liên quan

1. **Structure-aware fuzzing:** thay vì đột biến raw bytes, fuzzer hiểu cấu trúc input qua schema và chỉ sinh ra input hợp lệ về mặt cấu trúc. Giúp vượt qua các bước kiểm tra format ở đầu parser và chạm đến logic sâu hơn.

2. **libprotobuf-mutator (LPM):** thư viện của Google nhận một protobuf message và thực hiện mutation trên cây object theo schema, đảm bảo output vẫn là protobuf message hợp lệ. Tích hợp trực tiếp với libFuzzer qua `DEFINE_PROTO_FUZZER`.

3. **Serializer:** bộ chuyển đổi từ protobuf message sang raw bytes của định dạng đích. Phần phức tạp nhất vì phải tự tính xref offset và `/Length` -- các giá trị phụ thuộc vào vị trí byte thực tế của từng object trong file.

4. **xref table:** bảng tra cứu trong file PDF, ánh xạ object number sang byte offset trong file. Phải tính lại từ đầu sau mỗi lần mutation vì offset thay đổi.

5. **RNG seed:** trong context libFuzzer, seed điều khiển toàn bộ quá trình mutation. Cùng seed cho cùng kết quả, giúp reproduce lỗi.
