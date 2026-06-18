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

### 2. Proto schema [DONE giai đoạn 1+2 / TODO giai đoạn 3]

**[DONE] Giai đoạn 1 -- Document skeleton tối giản**
- `PdfDocument` chứa danh sách `Page`, mỗi `Page` có `width` / `height` (MediaBox).
- Thêm `package pdf_proto` để tránh name collision với xpdf's `Catalog` class.
- File: `research/pdf-proto-schema/pdf.proto`

**[DONE] Giai đoạn 2 -- Stream object model**
- Thêm `ContentStream` với `FilterType` enum (NONE / FLATE) và `raw_content` (bytes).
- Thêm `content_stream` vào `Page`.
- Kết quả smoke test 100 runs:
  - Giai đoạn 1 (skeleton): 664 edges
  - Sau Task 2a (raw stream, NONE filter): 705 edges (+41, +6.2%)
  - Sau Task 2b (FlateDecode): **716 edges (+52 tổng, +7.8%)**
- xpdf's FlateDecode/zlib code path đã được chạm đến.

**[TODO] Giai đoạn 3 -- Mở rộng schema để tăng coverage**

Lý do phải làm: schema hiện tại chỉ sinh ra PDF với MediaBox + ContentStream. libFuzzer đang
cover 729/27,712 edges (~2.6%) trong khi AFL++ với real-world seeds đạt 47.74%. Phần lớn code
của xpdf/PoDoFo nằm ở font parser, image decoder, color space, annotation -- không có object
nào trong số này được model trong schema hiện tại. Để tiêu chí 2 (coverage cao hơn AFL++
baseline sau cùng thời gian chạy) có thể đạt được, phải mở rộng schema để libFuzzer sinh ra
input chạm đến những code path đó.

Ngoài ra cần test các edge case quan trọng:
- **Multi-page document**: test page tree traversal, cross-page reference -- đã có `repeated Page`
  trong proto nhưng cần verify serializer và smoke test.
- **Multiple objects per page**: một Page có nhiều Font, nhiều Image -- test resource dictionary
  lookup, reference counting.
- **Dangling reference**: `/Contents` hoặc `/Font` trỏ đến object number không tồn tại trong
  xref -- test error handling path của parser.
- **Free object reference**: xref entry đánh dấu `f` (free/deleted) nhưng vẫn bị reference --
  test behavior khi PDF library đọc "deleted" object.
- **Recursive reference**: Type3 font chứa content stream gọi lại chính nó, hoặc Page -> Parent
  -> Kids -> Page tạo vòng lặp -- test stack overflow / infinite loop protection.

Danh sách object cần implement theo thứ tự ưu tiên (cao -> thấp):

**[P1] Font object (Type1 dummy)**
- Lý do: font parser là phần lớn nhất của xpdf/PoDoFo. Type1 dummy (chỉ cần `/Type /Font
  /Subtype /Type1 /BaseFont /Helvetica`) đủ để kích hoạt font lookup code mà không cần
  nhúng font program thật.
- Proto: `message Font { enum Subtype { TYPE1=0; TRUETYPE=1; TYPE3=2; } ... }`
- Serializer: thêm font object, reference từ `/Resources << /Font << /F1 N 0 R >> >>` trong Page.
- Coverage gain dự kiến: cao nhất trong tất cả các object.

**[P2] Dangling và free object references**
- Lý do: implementation cost thấp (chỉ thêm field vào proto), nhưng test được error handling
  path quan trọng -- đây là nơi có nhiều CVE (null deref, UAF khi parser không check object
  validity trước khi dereference).
- Proto: thêm `optional uint32 extra_contents_ref` trong Page (reference đến object number
  tùy ý, kể cả số không tồn tại).
- Serializer: ghi `/Contents [N 0 R M 0 R]` với M có thể là object không có trong xref.

**[P3] ImageXObject (với DCTDecode / JPXDecode)**
- Lý do: image decoder (JPEG, JPEG2000) có nhiều CVE lịch sử, và là code path AFL++ với
  real seeds dễ hit nhưng libFuzzer với schema đơn giản không thể chạm tới.
- Proto: `message ImageXObject { uint32 width=1; uint32 height=2; FilterType filter=3;
  bytes data=4; }` -- thêm vào Page resources.
- Serializer: ghi image stream object với `/Type /XObject /Subtype /Image /Width /Height
  /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length N`.

**[P4] ColorSpace (ICCBased, Indexed)**
- Lý do: color processing code trong xpdf/PoDoFo xử lý nhiều loại color space khác nhau.
  ICCBased references một stream (ICC profile) -- tạo thêm một layer object reference.
- Proto: `message ColorSpace { enum Type { DEVICE_RGB=0; DEVICE_CMYK=1; INDEXED=2;
  ICC_BASED=3; } ... }`

**[P5] Recursive reference (Type3 font)**
- Lý do: Type3 font có content stream riêng (CharProcs), và content stream đó có thể
  reference lại font khác -- tạo dependency graph phức tạp. Test stack overflow protection
  và infinite loop guard trong parser.
- Proto: `message Type3Font { map<string, ContentStream> char_procs = 1; }`
- Serializer: viết CharProcs stream, reference từ /Resources của font.

**[P6] Annotation (Link, FreeText)**
- Lý do: annotation parser trong xpdf có nhiều nhánh (GoTo, URI, JavaScript action). Link
  annotation với /Dest reference đến page khác test cross-object reference.
- Proto: `message Annotation { enum Subtype { LINK=0; FREE_TEXT=1; } ... }`

**[P7] JavaScript action (OpenAction, AA)**
- Lý do: xpdf có JavaScript engine (xpdf-js). JS actions trong /OpenAction hoặc /AA
  dictionary được thực thi khi mở file -- high-value target cho bug finding.
- Proto: `message Action { enum Type { JAVASCRIPT=0; GOTO=1; } optional string js_code=2; }`
- Chú ý: chỉ implement nếu còn thời gian -- JS engine trong xpdf ít được test nhất.

---

### 3. Serializer [DONE giai đoạn 1+2]

**[DONE] Giai đoạn 1 -- PDF hợp lệ từ protobuf message**
- Tự tính xref offset và ghi đúng trailer.
- Kết quả xác nhận:
  - `pdfinfo`: Pages: 1, Page size: 612x792, PDF 1.4, 330 bytes -- không có lỗi.
  - `pdftotext`: không có lỗi parse.
- File: `research/pdf-proto-schema/serializer.cpp`

**[DONE] Giai đoạn 2 -- Stream object**
- Nén payload bằng zlib khi `filter = FLATE` (dùng `compress2()` từ zlib).
- Ghi đúng `/Length` dựa trên kích thước thực của dữ liệu đã nén.
- Thêm `/Contents N 0 R` reference vào Page dictionary.
- Fallback về NONE nếu zlib compress thất bại (đảm bảo output luôn là PDF hợp lệ).
- 4/4 test cases trong `verify_serializer` pass.

**[TODO] Giai đoạn 2b -- Vá các gap trong stream object (cần làm trước run 2 và 3)**

Serializer hiện tại over-sanitize một số trường hợp, khiến libFuzzer không thể chạm đến
các code path lỗi quan trọng trong xpdf/PoDoFo. Cần fix 4 gap sau:

- [ ] **[HIGH] `/Length` sai (length_delta)**: serializer luôn ghi `/Length` chính xác.
  Thêm `sint32 length_delta = 3` vào `ContentStream` proto. Serializer cộng delta vào
  `/Length` trước khi ghi ra -- cho phép LPM sinh `/Length` nhỏ hơn (under-read) hoặc
  lớn hơn (over-read) thực tế. Đây là nguồn gốc của nhiều CVE trong PDF parser
  (parser đọc sai số byte, bỏ qua `endstream`, hoặc đọc tràn sang object kế tiếp).

- [ ] **[HIGH] FlateDecode data không hợp lệ (skip_compression)**: khi `filter = FLATE`,
  serializer nén `raw_content` bằng zlib nên LPM không bao giờ sinh được byte stream
  corrupt vào FlateStream decoder. Thêm `bool skip_compression = 4` vào `ContentStream`.
  Khi true: ghi `raw_content` trực tiếp vào stream payload nhưng vẫn giữ header
  `/Filter /FlateDecode` -- xpdf/PoDoFo sẽ thấy dữ liệu không phải zlib và chạy qua
  toàn bộ error-handling path của FlateStream (invalid CMF byte, corrupt Huffman table,
  truncated stream, wrong Adler-32 checksum).

- [ ] **[MEDIUM] Bỏ clamp width/height**: serializer đang clamp `width/height` về [1, 14400]
  (serializer.cpp line 73-74). Xóa clamp này để xpdf nhận NaN, 0, giá trị âm, giá trị
  rất lớn. Các giá trị này có thể gây integer overflow trong tính toán page geometry
  (width × height × bpp), đây là dạng lỗi phổ biến trong PDF renderer.

- [ ] **[MEDIUM] Multiple streams per page (`repeated ContentStream`)**: PDF cho phép
  `/Contents` là array nhiều stream: `/Contents [3 0 R 4 0 R]`, xpdf ghép chúng trước
  khi parse. Đổi `optional ContentStream content_stream = 4` thành
  `repeated ContentStream content_streams = 4`. Serializer ghi array reference khi có
  nhiều hơn 1 stream. Test được stream concatenation logic.

---

### 4. Harness + libFuzzer [DONE giai đoạn 1+2 / TODO giai đoạn 3]

**[DONE] Giai đoạn 1 -- xpdf 4.06**
- `DEFINE_PROTO_FUZZER` nhận `pdf_proto::PdfDocument`, gọi `SerializePdf`, feed vào `PDFDoc` của xpdf.
- Build thành công với clang-14 + ASan + libprotobuf-mutator.
- Smoke test 100 runs: coverage tăng từ 15 lên 664 edges, `LLVMFuzzerCustomMutator` active.
- File: `research/pdf-proto-schema/harness.cpp`

**[DONE] Giai đoạn 2 -- PoDoFo 0.9.7**
- Install: `sudo apt install libpodofo-dev libpodofo0.9.7`
- Harness dùng `PdfMemDocument::LoadFromBuffer()` -- feed bytes trực tiếp vào bộ nhớ, không cần temp file.
- Kết quả smoke test:
  - Coverage: 161 edges (PoDoFo và xpdf instrumented độc lập, không so sánh trực tiếp)
  - Exec/s: ~6,300 (so với ~1,800 của xpdf -- nhanh hơn 2.6x vì không I/O)
- File: `research/pdf-proto-schema/harness_podofo.cpp`

**[TODO] Giai đoạn 3 -- qpdf**
- Thêm harness cho qpdf.

---

### 5. Thực nghiệm và so sánh [RUN 1 DONE / TODO run 2+3]

**[DONE] Run 1 -- xpdf 4.06** (stopped early ~17h, schema exhausted)

| Metric | Value |
|---|---|
| Runtime | ~17h (dừng sớm) |
| Executions | 36.8M |
| Exec/s | 602 |
| Final coverage | 729 edges (~2.6% of 27,712 counters) |
| Crashes | 0 |
| Log | `research/experiments/libfuzzer-xpdf-run1/` |

**[DONE] Run 1 -- PoDoFo 0.9.7** (stopped early ~15h, schema exhausted)

| Metric | Value |
|---|---|
| Runtime | ~15h (dừng sớm) |
| Executions | 256.9M |
| Exec/s | 4,595 |
| Final coverage | 161 edges |
| Crashes | 0 |
| Log | `research/experiments/libfuzzer-podofo-run1/` |

**Kết luận Run 1:** Coverage plateau ngay trong 30 giây đầu tiên và không tăng thêm
suốt 17h chạy. Schema Stage 1+2 chỉ sinh PDF với MediaBox + ContentStream, không có
Font/Image/ColorSpace/Annotation nên libFuzzer không thể chạm đến phần lớn code của
xpdf/PoDoFo. Cần fix schema trước khi chạy run 2.

**[TODO] Trước khi chạy Run 2**
- [ ] Fix Giai đoạn 2b: length_delta + skip_compression + bỏ clamp width/height.
- [ ] Implement P1 Font object (Type1 dummy) vào schema + serializer.
- [ ] Rebuild binary, smoke test coverage tăng so với 729 edges.

**[TODO] Run 2 và Run 3**
- [ ] Chạy run 2 với schema mới (24h), so sánh coverage vs run 1.
- [ ] Chạy run 3 (24h), tính trung bình và độ lệch chuẩn qua 3 lần.
- [ ] Vẽ coverage curve: edges vs time cho cả 2 target, so với AFL++ baseline.
- [ ] So sánh trực tiếp: AFL++ baseline vs libFuzzer+protobuf-mutator trên xpdf.
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
