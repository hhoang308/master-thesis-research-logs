### Xây dựng Grammar trên Nautilus để generate các pattern trong PDF CVE
- Giải pháp 1. Tìm Grammar cho PDF sau đó sửa Grammar để tạo pattern tương tự như PDF CVE
    $\rightarrow$ Về mặt logic thì việc tạo PDF Grammar rules hoàn chỉnh là hoàn toàn khả thi nhưng chưa có ai làm (và khả năng là cũng không có ai làm) vì định dạng của PDF rất phức tạp (dài ~ 1300 trang) do đó cần **phải khoanh vùng và viết một bộ grammar rule thu gọn cho riêng một cấu trúc nhỏ muốn nhắm tới**.
### Tạo grammar rule cho 2 CVE: 
1. https://www.sentinelone.com/vulnerability-database/cve-2024-49576/
2. https://codeanlabs.com/2024/05/cve-2024-4367-arbitrary-js-execution-in-pdf-js/ $\to$ nhưng mà các pdf viewer opensource thì không có js engine thì không biết xử lý phần này như thế nào???
Refer đến grammar tại [font-matrix-type-mismatch-grammar](..\logs\2026-05-16-logs\font-matrix-type-mismatch-grammar.py)
### Nhúng grammar rule của một cấu trúc nhỏ vào file PDF và thu thập feedback như thế nào?
- Sử dụng template thông qua tính năng `ctx.script` (hỗ trợ viết kịch bản python bằng lambda function)
    - chuẩn bị seed là một file pdf cơ bản và hợp lệ, trong đó có chứa object đã có grammar (ví dụ như FontMatrix).
    - tạo script rule: trong nautilus định nghĩa một root rule dạng script, trong script này chứa hardcode toàn bộ chuỗi byte của tệp PDF có sẵn kia, ngoại trừ vị trí của font matrix.
    - inject payload: script này nhận tham số đầu vào là kết quả từ font matrix grammar rule, nó sẽ khoét phần font matrix cũ ra và chèn payload mới vào.
### Nautilus đã 2 năm rồi chưa có update? liệu có ai làm cái tương tự nhưng update mới hơn chưa?
- Nautilus là dự án tiên phong với vai trò làm proof of concept nên hiện tại ít được cập nhật, hiện tại đã chuyển sang các giải pháp tốt hơn, kế thừa triết lí của nautilus, bao gồm:
    - Grammatron: Cũ hơn cả nautilus, update lần cuối là 5 năm trước???
    - Grammarinator với AFL++/LibFuzzer: Grammarinator là công cụ sinh ngữ pháp mạnh mẽ sử dụng chuẩn ANTLR
    - FormatFuzzer: đây là một công cụ mới chuyên trị định dạng nhị phân, sử dụng để cắm thẳng vào những phần nhị phân (zlib, jpeg, offset,...) trong pdf.
    -libprotobuf-mutator (LPM) + libFuzzer: phương pháp được google/pdfium sử dụng. định nghĩa cấu trúc của một phần PDF bằng protocol buffer (protobuf), sau đó lpm sẽ làm nhiệm vụ mutate trên protobuf này và biên dịch ngược thành dữ liệu đầu vào
### Phân tích sự phù hợp của các hướng đối với luận văn tốt nghiệp
Mục tiêu: Cải tiến cấu trúc file PDF để tìm bug
Phương pháp: (DGF - Directed grey-box fuzzing, fuzzing hướng đích) kết hợp với vulnerability-guided/seed-optimization fuzzing, fuzzing tối ưu hoá hạt giống từ lỗ hổng.
    - Phương pháp 1: bug-type specific DGF, fuzzer được thiết kế riêng chỉ để nhắm vào một bug cụ thể. 
    - Phương pháp 2: tối ưu seed từ các CVE cũ (ISC4DGF, CVE-GENIE)
    - Phương pháp 3: sử dụng format template của format fuzzer để tái tạo lỗi
Phương pháp sử dụng: 
    - Khảo sát và phân nhóm CVE, ví dụ: lỗ hổng DOM, lỗ hổng về type mismatch, lỗ hổng dữ liệu,...
    - Xây dựng bộ sinh tệp PDF (PDF generator và seed corpus):
        - Sử dụng libprotobuf-mutator + libFuzzer: định nghĩa các object của PDF (xref, font, stream) bằng protobuf, sau đó viết code C++ để serial các protobuf này thành file pdf thực.
        - Dùng FormatFuzzer: tạo tempate của PDF -> sinh ra các đoạn code -> đoạn code này lại sinh ra file PDF -> corpus chất lượng cao.
    - Directed Fuzzing: 
        - Sử dụng DAFL có khả năng tính toán luồng dữ liệu và đo khoảng cách từ điểm thực thi hiện đại đến đoạn code mục tiêu.
        - Lấy opensource về và xác định hàm xử lý phông chữ hoặc xử lý JS
        - Chỉ định DAFL xáo trộn sao cho chạy đúng hàm xử lý phông chữ này.
    - Benchmark:
        - chạy AFL++ và DAFL với tập PDF có hạt giống cấu trúc CVE ??? và đo TTE
### Phân tích phương pháp libprotobuf-mutator + libFuzzer
- Cho phép định nghĩa PDF format dưới dạng các đối tượng dữ liệu, sau đó chỉ định fuzzer mutate ngay trên đối tượng này.
- Quy trình 4 bước:
    - Sử dụng protocol buffer (một ngôn ngữ lập trình?) định nghĩa cấu trúc của tệp PDF, tập trung vào các object muốn khảo sát.
    - Sử dụng C++ để viết một bộ converter chuyển đổi cấu trúc từ protocol buffer thành định dạng pdf. Bộ chuyển đổi này sẽ duyệt qua PdfDocument, thêm các thẻ obj, endobj,...để nối chúng lại thành một bộ đệm duy nhất
    - Sử dụng tính năng PostProcessRegistration của libprotocol-mutator để cho phép đăng ký các hàm callback. Các hàm callback này sẽ được thực hiện ngay sau khi PDF thực hiện xáo trộn, để đàm bảo các object khác được đồng bộ.

#### Khái niệm
1. Protocol Buffer (hay protobuf) là một định dạng dữ liệu (như kiểu JSON, XML nhưng ở đây là kiểu binary) trung lập về ngôn ngữ + nền tảng do Google phát triển. Cần biên dịch, sau khi biên dịch xong sẽ thành dạng nhị phân. HDSD
    - Tạo tệp .proto định nghĩa cấu trúc của đối tượng
    - Sử dụng protobuf compiler (gọi là protoc) biên dịch .proto thành các classes trong ngôn ngữ lập trình sử dụng (phổ biến là C++).
    - Trong chương trình nguồn, khởi tạo các dối tượng từ lớp vừa được tạo, gán giá trị cho chúng rồi gọi lệnh để serialize nó thành chuỗi byte hoặc deserialize nó thành đối tượng.
Ví dụ cụ thể: Giả sử muốn lưu trữ một danh bạ người liên lạc, mỗi người sẽ bao gồm thông tin về tên, tuổi, sđt, thì có thể sử dụng nhiều cách, ví dụ như dùng JSON, XML,...để lưu dữ liệu thành một file .txt, .xml,...hay gì đó. Nếu sử dụng định dạng JSON, sau đó lưu dưới dạng .txt, thì phải có một library (hoặc tự làm) để xử lý đoạn đọc text trong file .txt, bóc tách từng thành phần nhằm tìm kiếm hoặc sửa gì đó, tương tự với .xml, và dù sử dụng cách nào thì nó vẫn cần một đoạn xử lý để parse dữ liệu, đặc biệt là đối với các định dạng như JSON và XML sẽ tốn nhiều thời gian. Do đó người ta phát triển ra định dạng dữ liệu mới gọi là Protocol Buffer, với cách encode và decode có hiệu suất cao hơn. Khi người dùng phát triển một application bằng C++ để đọc/ghi danh bạ, các bước cần làm sẽ như sau
    1. Tạo một file .proto (ví dụ là dictionary.proto) để định nghĩa danh bạ điện thoại sẽ có những thông tin và kiểu dữ liệu như thế nào.
    2. Biên dịch file .proto bằng compiler của riêng nó, kết quả sẽ tạo ra 1 file .h và 1 file .cpp (ví dụ là dictionary.pb.h và dictionary.pb.cpp), trong đó, file .h sẽ khai báo một class biểu diễn lại danh bạ đã định nghĩa ở bước 1, bao gồm attribute, method,... còn file .cpp sẽ là implement của các method đó.
    3. Tại C++ application, các file .cpp khi muốn đọc/ghi danh bạ sẽ tiến hành include file .h và file .cpp (tại quá trình linking), lúc này người lập trình viên chỉ cần gọi các hàm được liệt kê trong file .h là có thể tương tác được với danh bạ rồi.
    4. Quá trình đọc/ghi danh bạ vẫn sẽ tạo ra một file dữ liệu (tương tự như đối với trường hợp dùng JSON, XML là tạo ra file .txt, .xml,...) có thể có đuôi là .data hoặc .bin, tuỳ, nhưng dữ liệu bên trong khong thay đổi.

2. libprotobuf-mutator (LPM) là một thư viện hỗ trợ biến đổi cấu trúc thay vì chỉ biến đổi byte, luôn sinh ra cú pháp hợp lệ và có khả năng hậu xử lý (sau khi mutate xong thì gọi các callback để đồng bộ những thay đổi, không để sai cú pháp).
Một vài use case nên sử dụng libFuzzer và libprotobuf-mutator để mutate protobuf:
    - Fuzzing Target chấp nhận protocol buffer.
    - Fuzzing target chấp nhận input được định nghĩa bởi grammar. Cần viết code để convert data $\leftrightarrow$ protobuf-based format.
    - Fuzzing target chấp nhạ nhiều hơn 1 arguments.

$\Rightarrow$ Có thể phải tìm target function trực tiếp trong một libary xử lý pdf nào đó opensource, sau đó sử dụng libprotobuf-mutator để fuzzing cụ thể một thành phần. 
$\Rightarrow$ dùng Format Fuzzer?


### Phân tích phương pháp Nautilus
- Không có grammar cho toàn bộ PDF format
- Chỉ có thẻ tạo grammer cho 1 khối bộ phận thôi, nhưng format pdf lại cần sự thống nhất giữa nhiều object với nhau.

### Các OpenSource xử lý PDF
1. PDFium (Của Google)
2. Poppler
3. MuPDF
4. Xpdf (xpdfreader)

### Cải tiến structure-aware mutator cho PDF
- Phương pháp 1: Fuzzing cấp độ khối dữ liệu (Chunk-level Fuzzing - Mô hình AFLSmart)
Thay vì mô hình hóa toàn bộ cú pháp, bạn chỉ cần xây dựng một Biểu diễn trung gian (Intermediate Representation - IR) rất cơ bản mô tả các "khối" (chunk) của PDF (ví dụ: đâu là Header, đâu là Body, đâu là XRef). Mutator của bạn sẽ thực hiện xáo trộn cấp độ cao như: Xóa nguyên một khối Object, hoán đổi vị trí hai khối, hoặc nhân bản một khối lên nhiều lần. Sau đó mới dùng đột biến byte ngẫu nhiên bên trong lõi của khối đó. 
    - Sử dụng thêm kỹ thuật Hậu xử lý (Post-Processing) để tự động quét lại toàn bộ file PDF vừa sinh ra, tự động đếm lại số lượng byte, tính toán lại trường /Length của các Stream bị thay đổi, và tạo lại bảng tham chiếu XRef mới hoàn toàn. Sau khi sửa xong cấu trúc cho hợp lệ, buffer mới được bơm vào phần mềm mục tiêu.
    - Thiết kế một mutator có khả năng nhận thức cấu trúc ở cả cấp độ thấp (bit/byte/word/dword) ngay bên trong ranh giới của khối dữ liệu đó để tránh làm hỏng cú pháp cục bộ.
    - Tự định nghĩa một cấu trúc Biểu diễn trung gian (PDF-IR) siêu nhẹ bằng C/C++ (hoặc dùng FormatFuzzer/Protobuf) tích hợp thẳng vào AFL++. Việc này sẽ giúp tốc độ thực thi (executions/second) của bạn vượt trội hoàn toàn so với AFLSmart gốc.
- Phương pháp 2: Fuzzing cấp độ Token (Token-Level Fuzzing)
Đây là phương pháp xuất hiện tại hội nghị USENIX Security. Bạn viết một script đơn giản đọc file PDF hạt giống, cắt nó ra thành các "từ vựng" (Token) như /Type, /Font, obj, endobj, <<, >>. Mutator của bạn không quan tâm đến ngữ pháp cây AST mà chỉ bốc ngẫu nhiên các Token này chèn, hoán đổi hoặc xóa chúng. Việc này tạo ra các file PDF "gần đúng" cú pháp nhưng sai logic, lừa được parser đi rất sâu để tìm lỗi.