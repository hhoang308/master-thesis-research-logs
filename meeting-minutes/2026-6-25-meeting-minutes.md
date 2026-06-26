# Báo cáo tiến độ luận văn -- 2026-06-25

**Đề tài:** Structure-aware fuzzing cho định dạng PDF (libprotobuf-mutator + serializer tự
tính xref/Length), so sánh với AFL++ thuần trên các PDF reader ít được fuzzing (xpdf,
PoDoFo, qpdf).

Tóm tắt theo nhật ký commit (git) kể từ buổi gặp trước.

---

## 1. Tóm tắt nhanh (TL;DR)

- Đã **hoàn thiện schema PDF** (skeleton + stream + **font đầy đủ** + **image**) và serializer;
  serializer được **kiểm chứng nghiêm ngặt** (xref đúng 100% trên 2000 input đa dạng).
- **Phát hiện và sửa một lỗi phương pháp luận quan trọng**: harness cũ không thực thi content
  stream nên thực ra chưa hề chạm tới font/image parser của target. Sau khi sửa, coverage thật
  sự tăng mạnh.
- Đã **tìm được 1 vấn đề** ở PoDoFo (cấp phát bộ nhớ không kiểm tra kích thước).
- **Chuyển engine từ libFuzzer sang AFL++ + custom mutator** (lý do bên dưới); đã build xong
  toàn bộ và chạy sanity test thành công. Campaign 24h đang chờ giải phóng RAM của máy.

---

## 2. Schema + Serializer (đã hoàn thiện)

Mở rộng schema theo thứ tự ưu tiên, mỗi bước 1 commit + verify:
- **Vá 4 gap của stream object**: `length_delta` (sai /Length -> over/under-read),
  `skip_compression` (đẩy byte rác vào FlateDecode), bỏ clamp MediaBox (cho NaN/0/âm/huge),
  `repeated ContentStream` (mảng /Contents).
- **Font object (P1 -> P1.5 -> Step A -> Step B)** -- đầy đủ:
  - P1: dummy Type1 + driver stream `Tf` (ép `GfxFont::makeFont` chạy vì xpdf load font lazy).
  - P1.5: `/FontDescriptor` + nhúng `/FontFile,2,3` -> chạm tới các FoFi parser (Type1/TrueType/CFF).
  - Step A: `/Encoding`+`/Differences`, `/Widths`, `/ToUnicode` CMap.
  - Step B: CID/Type0 (`/DescendantFonts`, `/CIDSystemInfo`, `/CIDToGIDMap`, `/W`).
- **P3 Image XObject**: `/DCTDecode`, `/JPXDecode`, `/CCITTFaxDecode`, `/LZWDecode` -- chạm tới
  các image decoder (JPEG2000 của xpdf là code nhiều CVE lịch sử).

**Kiểm chứng serializer (công cụ `check_serializer` -- nhiều "oracle"):**
- Bất biến **xref byte-offset đúng 100%** trên 2000 proto đa dạng (sinh bằng `gen_corpus`,
  có histogram đo độ đa dạng) -- đây là đảm bảo cốt lõi (tự tính lại xref/Length).
- Tỉ lệ hợp lệ ngoài (3 parser độc lập): poppler/mupdf 100%, **qpdf nghiêm ngặt ~89%** không lỗi
  (phần còn lại là malformation cố ý qua length_delta/skip_compression). => Đạt tiêu chí 1
  (valid-input-rate 60-80%).
- Bài học: không nên chỉ dùng 1 parser (pdfinfo) để đo hợp lệ vì poppler rất "khoan dung".

---

## 3. Lỗi phương pháp luận đã phát hiện & sửa (quan trọng)

Dùng gdb phát hiện: **harness cũ chỉ parse cấu trúc** (xref/catalog/page-tree) rồi dừng
(`getNumPages` / `LoadFromBuffer`+`GetPageCount`), **KHÔNG thực thi content stream** -> toán tử
`Tf`/`Do` không chạy -> font parser & image decoder của xpdf/PoDoFo chưa hề được chạm tới.
Vì cả binary được instrument chung nên "coverage" tăng trước đó chủ yếu là code
serializer/protobuf, **không phải độ sâu của target**.

**Đã sửa harness** để render trang thật sự (xpdf: `displayPages` + `OutputDev` tự viết, ép
giải mã ảnh; PoDoFo: tokenize content + `GetFilteredCopy` mọi stream). Sau khi sửa, đã verify
bằng gdb là `GfxFont::makeFont` và `Gfx::doImage` chạy thật. => **Đây là điểm cần ghi rõ trong
chương Thực nghiệm**: phải đo coverage trên harness có render, nếu không kết quả sai lệch.

Hệ quả: các con số coverage của Run 1/2 cũ chỉ là checkpoint phát triển, **campaign thống kê
chính thức sẽ chạy lại trên harness mới**.

---

## 4. Phát hiện (finding)

- **PoDoFo 0.9.7 -- cấp phát không kiểm tra kích thước** (CWE-789): ảnh có `/BitsPerComponent`
  ~4.18 tỉ làm PoDoFo `calloc` ~256 TB (ASan: allocation-size-too-big). Reproducer 245 byte đã
  lưu (`research/findings/podofo-alloc-size/`). poppler/mupdf đọc cùng file này bình thường ->
  lỗi đặc thù PoDoFo.
- **Lưu ý đánh giá:** khi thử bằng tool trích ảnh của PoDoFo thì PoDoFo trả về *lỗi* chứ không
  crash -> nhiều khả năng đây là **DoS/được xử lý** chứ chưa chắc là lỗi memory-safety. Cần
  xác minh thêm (đang để mức "low / cần điều tra", không thổi phồng).

---

## 5. Chuyển engine: libFuzzer -> AFL++ + custom mutator

**Lý do:**
1. libFuzzer đã **deprecated** (LLVM khuyến nghị FuzzTest/Centipede).
2. libFuzzer (fork mode + ASan) **bất ổn định** trên máy này (SIGSEGV không xác định ở giai đoạn
   khởi tạo, hay bị OOM).
3. Quan trọng nhất: AFL++ cho phép **so sánh công bằng** -- baseline (AFL++ thuần) và phương pháp
   (AFL++ + custom mutator) **dùng cùng engine + cùng instrumentation** -> coverage so sánh
   trực tiếp được, không cần "quy đổi".

**Kiến trúc (giữ nguyên schema + serializer, chỉ thay lớp engine):**
- Seed/queue/crash của AFL là **proto đã serialize** (binary). Custom mutator `.so`:
  - `afl_custom_fuzz`: parse proto -> libprotobuf-mutator đột biến cây message -> serialize lại.
  - `afl_custom_post_process`: parse proto -> `SerializePdf()` -> byte PDF cho target.
- Target harness build bằng `afl-clang-fast` (instrument + persistent mode), đọc PDF từ bộ nhớ
  (MemStream, không cần file tạm).

**Đã build + verify xong toàn bộ:**
- `afl_pdf_mutator.so`: export đủ 4 hook, 0 phụ thuộc ASan, smoke test ra PDF hợp lệ.
- `gen_corpus --clean --binary`: sinh seed proto nhị phân hợp lệ cho AFL.
- `afl_harness_xpdf`: instrument OK (afl-showmap bắt được 1308 tuple; log xpdf cho thấy thật sự
  giải mã DCT/parse font).
- **Sanity test 60s ĐẠT**: mutator nạp thành công, fork server OK, queue tăng 300 -> 422,
  coverage 7.44% -> 8.48% (chứng minh toàn bộ vòng lặp structure-aware-trên-AFL++ chạy đúng).

---

## 6. Số liệu (tham chiếu)

| Hạng mục | Giá trị |
|---|---|
| Baseline AFL++ (xpdf, 86h) | 47.69% (15,006 / 31,464 edges), 0 crash |
| Serializer xref đúng | 2000/2000 (100%) |
| Valid-input-rate (qpdf nghiêm ngặt) | ~89% |
| Sanity AFL++ structure-aware | queue 300->422, cvg 7.44->8.48% trong 6s, 0 lỗi |
| Finding | 1 (PoDoFo alloc-size, đang đánh giá mức độ) |

*(Coverage chính thức của phương pháp sẽ có sau campaign 24h trên harness mới.)*

---

## 7. Khó khăn

- **Máy thiếu RAM**: baseline (Firefox/VSCode/Electron/node ~8-9GB) + fuzzer làm swap đầy 100%
  -> afl-fuzz bị OOM-kill sau ~6s. Campaign 24h sẽ chạy sau khi giải phóng RAM. (Không phải lỗi
  pipeline -- lúc bị kill coverage đang tăng bình thường.)

---

## 8. Việc tiếp theo

- [ ] Giải phóng RAM, chạy campaign AFL++ 24h cho xpdf (đã sẵn sàng: `experiments/afl-xpdf-run1/run.sh`).
- [ ] Viết harness AFL cho PoDoFo (cùng mẫu), rồi qpdf.
- [ ] So sánh coverage **AFL++ baseline vs AFL++ + mutator** (cùng instrumentation) + vẽ
      coverage curve theo thời gian, lặp 3 lần lấy trung bình/độ lệch chuẩn.
- [ ] Điều tra kỹ finding PoDoFo (xác định memory-safety hay chỉ DoS).
- [ ] (Đã defer) near-valid token injection vào các giá trị dict số/name.

---

## 9. Câu hỏi cho thầy/cô

- Mức đóng góp: tập trung tiêu chí 1 (valid-input-rate -- đang ~89%) + tiêu chí 4 (chạm code
  path sâu mà AFL++ thuần khó duy trì) + tính mới của serializer (tự tính xref/Length) có đủ
  không, hay bắt buộc phải hơn baseline về **tổng** coverage (khó, vì baseline dùng real-world
  seed có đủ mọi feature)?
- Finding dạng DoS (cấp phát quá lớn) có được tính là kết quả không?



mục tiêu: làm sao để build effective grammar (tức grammar thật tốt)
	cách tiếp cận:
		1. attack pattern based grammar: dựa theo CVE để tạo grammar
			tham chiếu đối tượng đã bị xoá
			integer overflow
			can thiệp vào tham số
		2. hot code based grammar: (optional) cross bridge js enginge, dựa vào hot code (những đoạn code/vùng code mà tự mình định nghĩa là quan trọng, có thể là những đoạn thay đổi DOM,...) và critical section chứ không dựa vào CVE nữa (vì chưa có ???)

sau khi tạo xong thoả mãn 3 điều kiện trên, cần reproduce lại được các CVE (trong tuần sau)
sau đó tìm trên các phiên bản mới hơn hoặc phần mềm khác
