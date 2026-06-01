## Danh sách các API
- Bắt buộc phải implement. (refer to https://aflplus.plus/docs/custom_mutators/)
	- afl_custom_init: được gọi khi AFL++ khởi động, dùng để khởi tạo RNG seed và allocate bộ nhớ tái sử dụng
	- afl_custom_fuzz (quan trọng): thực hiện mutation trên input; `add_buf` là nội dung của một seed khác trong queue, có thể dùng để splicing. Phần này sẽ cần implement toàn bộ logic parser, bao gồm: bytes -> IR -> mutate -> serialize.
	- afl_custom_deinit: 
- Nên implement sau:
	- Implement sau khi Layer 1 chạy tốt: 
		- afl_custom_havoc_mutation: thực hiện mutation đơn lẻ được stack với các mutation khác trong havoc stage
		- afl_custom_havoc_mutation_probability: trả về xác suất hàm afl_custom_havoc_mutation được gọi, mặc định là 6%

## Tóm tắt
Mục tiêu:
1. Tăng tỉ lệ input hợp lệ lên khoảng 60% đến 80%.
2. Tăng time-to-first-crash và unique crash
Phương pháp:
- Phương pháp 1 (dễ hơn, làm trước): Sử dụng AFL++ và lopdf 
	1. Tái sử dụng lopdf parser (viết bằng Rust) để chuyển từ bytes sang IR.
	2. (Đóng góp chính) Thực hiện mutate dựa trên IR bằng 3 Layer đã đề cập.
		- custom mutator sẽ viết bằng Rust rồi build ra dưới dạng .so sao cho khớp với C API là được, sau đó include vào custom mutator thuộc AFL++.
		- 3 phương pháp mutator L1, L2, L3.
	3. Tái sử dụng lopdf parser (viết bằng Rust) để chuyển từ IR sang bytes, tự tính xref, startref và Length.
- Phương pháp 2 (khó hơn, làm khi cách 1 gặp vấn đề không khắc phục được): Sử dụng LibAFL + lopdf để tự build 1 fuzzer chuyên dụng cho PDF.
## Công việc
1. Thu thập seed corpus + chạy afl++ thuần tuý cho các pdf reader/parser/library.
Thu thập các seed corpus
  https://github.com/mozilla/pdf.js/tree/master/test/pdfs
  https://gitlab.freedesktop.org/poppler/test.git
    Sử dụng --depth để chỉ lấy các commit ở gần thay vì lấy full lịch sử $\to$ nặng
    Các file .link là những file chứa URL dẫn đến 1 file PDF mà ví lí do nào đó (nặng, bản quyền,...) nên không có, tạm bỏ qua các file này đã đủ đa dạng rồi.
Sau khi tìm xong phải duyệt bằng afl-cmin để lọc bớt seed.
  $\leftarrow$ Sử dụng afl-cmin với các seed tìm được ở bước trên
Từ các CVE tìm được, tự tạo các seed đặc biệt. 
  $\leftarrow$ Kiểm tra pipeline có thực sự hoạt động, nếu hoạt động thì phải tái hiện lại được CVE. 
  $\leftarrow$ Tạo seed gần vùng code đáng khai thác, giúp fuzzer có khởi đầu tốt hơn.
  $\leftarrow$ Tạo baseline để so sánh, cụ thể, so sánh khoảng thời gian tìm lại được CVE
    1. Sử dụng AFL++ và seed thông thường
    2. Sử dụng AFL++ và seed đã được ép theo tương tự CVE
    3. Sử dụng AFL++ và IR mutator và seed đã được sửa đổi dựa trên CVE.
2. Round-trip test lopdf:
Mục tiêu: Sử dụng lopdf (hiện tại chưa liên quan gì đến mutate) load ~ 40 files pdf dạng bytes, sau đó parse thành IR, rồi từ IR parse lại thành bytes. Đảm bảo tất cả các file đều phải parse được, nếu không được phải xử lý.

3. Phát triển mutator tối giản bằng Rush cdylib
Mục tiêu: AFL++ có thể nhận và sử dụng được mutator này.

4. Phát triển L1 mutator và so sánh với afl++ baseline

5. Phát triển L2 mutator, đo đạc các thông số quan trọng: valid input,...

6. Phát triển L3 mutator, 

7. Thực nghiệm 3 lần 24-48h, triage crash (nếu có)

## Điều kiện bảo vệ
1. Hoàn chỉnh về mặt kỹ thuật
- Hoàn thành custom mutator bằng Rust cho AFL++, build được và chạy được.
- Tích hợp được custom mutator vào AFL++ và chạy trên 2 target (xpdf, popler,...)
2. Kết quả thực nghiệm
- Chạy đủ 24h, lặp 3 lần, có giá trị trung bình và độ lệch chuẩn
- Tỷ lệ valid input cao hơn AFL++
- Số liệu của L1, L2, L3 riêng biệt và tổ hợp của 2 trong 3
3. Phát hiện được crash
- Reproduce được crash, có stacktrace, mô tả được nguyên nhân.
4. Phân tích và thảo lua
- Giải thích vì sao kết quả tốt hơn, nhận biết giới hạn và đề xuất hướng mở rộng cụ thể

## Cấu trúc luận văn
Chương 1 — Giới thiệu (5-8 trang)
  - Bài toán: tại sao AFL++ vanilla không đủ với PDF
  - Đóng góp cụ thể (liệt kê 3-4 bullet, không mơ hồ)
  - Cấu trúc luận văn

Chương 2 — Kiến thức nền tảng (15-20 trang)
  - Coverage-guided fuzzing và AFL++
  - Định dạng PDF: chỉ phần cần thiết (object model, xref, stream)
  - Custom mutator API

Chương 3 — Related work (8-12 trang)  ← QUAN TRỌNG
  - AFLSmart, Superion, FormatFuzzer, pFuzzer
  - Bảng so sánh theo các chiều: semantic awareness, serializer validity...
  - Khoảng trống mà đề tài lấp

Chương 4 — Thiết kế và triển khai (25-35 trang)  ← DÀY NHẤT
  - PDF-IR design: các invariant, lý do chọn lopdf
  - L1/L2/L3 mutator: mô tả thuật toán, pseudocode
  - Serializer: tại sao tự tính lại xref quan trọng
  - Tích hợp AFL++: API nào dùng, tại sao

Chương 5 — Thực nghiệm (20-25 trang)
  - Setup: target, seed corpus, môi trường
  - Kết quả baseline vs custom mutator (biểu đồ coverage theo thời gian)
  - Ablation study: đóng góp từng layer
  - Case study: 1-2 crash tiêu biểu
  - Thảo luận: kết quả có ý nghĩa gì, giới hạn là gì

Chương 6 — Kết luận (3-5 trang)
  - Tóm tắt đóng góp
  - Hướng mở rộng


## Khái niệm liên quan
1. RNG (Random Number Generator) là bộ sinh số ngẫu nhiên. Trong bối cảnh fuzzing thì RNG được dùng để quyết định xem chọn mutator nào (L1, L2 hay L3) và chọn object nào để mutate và thay nó bằng giá trị gì. Nếu cần reproduce lại một mutation cụ thể, có thể chạy lại với cùng seed và ra đúng kết quả đó. "Seed" ở đây là một số nguyen truyền vào trước khi fuzzing, nếu sử dụng cùng số seed thì mọi quyết định ngẫu nhiên của fuzzing đều reproduce được, từ đó quá trình fuzzing có thể được tái hiện lại.
2. IR (Intermediate Representation) của cấu trúc PDF trông như thế nào?