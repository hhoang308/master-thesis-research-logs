## Pipeline 
- Tạo một `.proto` schema cho định dạng PDF
- `libprotobuf-mutator` đột biến `protobuf message` (the structured object tree)
- Viết một bộ serializer chịu trách nhiệm cho việc truyển đổi `protobuf message` sang dạng raw bytes PDF
- Lớp harness sẽ dùng các bytes này để làm input cho target

## Công cụ sử dụng
- libFuzzer và libprotobuf-mutator trước, sau này nếu muốn tăng hiệu suất thì có thể sử dụng AFL++
- đã có hướng dẫn sử dụng libFuzzer trên pdfium và poppler
## Thách thức
- Viết grammar sẽ dễ vì có thể tách nó thành các công việc bé hơn và kiểm soát được.
- Phân khó nhất là phần serializer để chuyển từ protobuf messsage sang dạng byte, đặc biệt là xref và stream length fields.
- Hơi khó để tìm được CVE vì PDFium đã có sẵn 19 fuzzer chạy trên nó, Poppler cũng có 16 fuzzer đang chạy.
- Do đó nếu muốn tìm CVE thì tìm ở các thành phần ít được chú trọng hơn trong PDF là JBIG2, JPXDecode, JPEG2000,...hoặc các target ít được hướng đến là MuPDF, Ghostscript, Sumatra, mobile/embeded readers,...
- Tuy nhiên trình bày được phương pháp để tăng coverage là đã đủ rồi.
## Các phần việc cần phải làm thêm để tăng hàm lượng học thuật
- Sử dụng cùng một file PDF cho nhiều phần mềm khác nhau để tìm các lỗi liên quan đến thay đổi logic xử lý giữa các phần mềm
- Mô hình hóa bản thân stream content dưới dạng decoded và fuzzing trong đó, rồi sau đó re-encode lại 
- Tập trung tạo file `.proto` vào các object/filter type mà chưa được phát hiện (hoặc ít được chạm tới)
- Coi việc tạo văn phạm là baseline, rồi sau đó làm thêm các công việc khác, cụ thể:
	- các phương pháp mutate tại chunk-level như AFLSmart không thể xử lý được xref-offset và `/Length` nhưng phương pháp này có thể
	- tập trung vào mutate ở các thành phần decoder
	- sử dụng cùng input nhưng thực hiện fuzzing trên nhiều đối tượng khác nhau để kiểm tra lỗi logic
	- so sánh giữa các phương pháp: LPM-grammar, AFLSmart, AFL++...
## Sắp xếp thứ tự các target từ dễ đến khó tìm CVE
- Target dễ tìm được CVE vì cùng attack surface nhưng không có đội ngũ chuyên chạy fuzzing liên tục:
	- PoDoFo
	- xpdf
	- qpdf
	- pdfbox (java)
	- pdfcpu/unipdf/pdf-rs/lopdf (go/rust)
- Các decoder trong pdf nên tập trung:
	- openjpeg
	- jbig2dec
	- freetype
	- libjpeg-turbo, CCITTFax/JBIG2 paths
- nên tránh
	- pdfium
	- poppler
	- mupdf/ghostscript
	- pdf.js
## Milestone
1. Giới hạn công việc
- Xây dựng `.proto` schema cho PDF với các tiêu chí sau: chỉ xây dựng đủ để chạm đến các stream decoder và sau đó model decoded stream payload để thực hiện mutation trên đó. Cụ thể:
	- document skeleton (header, catalog, pages, one page) để mở được file
	- stream object model (filter chain + decoded payload) - phần này là phần chính
	- xref (tại bước thực hiện serializer) 
	- các object khác nếu còn thời gian như arrays, name trees, font, javascript actions,...
- Xử lý được `xref` và `/Length` và các giá trị liên quan.
- Thực hiện mutate bên trong object stream Flate và decoder JPXDecode (phần mới).
2. Sử dụng libFuzzer để fuzzing đối tượng trước để tạo baseline
3. Xây dựng schema cho stream và decoder ở trên
- có vẻ là sẽ khó để xây dựng
4. Xây dựng serializer
## Các công việc cần làm
1. Xây dựng Pdfdocument .proto schema trong đó hardcode 1 catalog, 1 pages, 1 stream và 1 flatedecode.
2. Xây dựng serializer.
Mục tiêu: Tạo được file PDF hợp lệ và mở được, tạo proof of concept.
3. Tạo schema cho stream và đảm bảo target có thể đọc được stream đó (qua coverage)
4. Tạo ImagePayload với codec là JPX, sử dụng payload là một file .jp2 thật và đảm bảo target có thể đọc được.
## Thông tin
1. PDF Specs sử dụng sẽ là Adobe PDF Reference 1.7 (the pre-ISO version) dài 1300 trang vì đây là định dạng cơ bản và nguyên thủy nhất, tập trung vào các phần:
- Syntax
- Filter
- File Structure
- Graphics Model
- Stream Objects
2. Sau đó đọc tiếp ISO 32000-2:2020 (PDF 2.0) để xác định những phần thay đổi hoặc những đính chính mà bản cũ chưa rõ ràng, tập trung vào những phần này vì có thể các PDF viewer sẽ có các cách xử lý khác nhau.
3. 
