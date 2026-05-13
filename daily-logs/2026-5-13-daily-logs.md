## Các cơ chế khai thác kĩ thuật đặc thù trên định dạng PDF
- Object Graph Abuse:
    - PDF là định dạng file gồm một mạng lưới các tham chiếu chéo (XRef). Các đối tượng (Dictionaries, Arrays) có thể tham chiếu lẫn nhau. Nếu kẻ tấn công tạo ra một cấu trúc trong đó Đối tượng A tham chiếu đến Đối tượng B, và B lại tham chiếu ngược về A (ví dụ, cấu trúc vòng lặp vô hạn trong cây đánh dấu trang - readPageLabelTree2 của xpdf ), trình phân tích cú pháp sẽ rơi vào vòng đệ quy bất tận. Nếu phần mềm thiếu bộ đếm độ sâu đệ quy (recursion depth tracker), ngăn xếp thực thi (Call Stack) sẽ phình to và sập đổ (Stack Overflow). Đây là nguyên nhân của hàng loạt lỗi DoS.
- JavaScript và AcroForms:
    - Mã JavaScript nhúng có thể thao tác với DOM của PDF, thay đổi thuộc tính, xóa trường dữ liệu (Forms). Lỗi Use-After-Free (UAF)  kinh điển xảy ra khi mã JS tạo một đối tượng, gán nó cho một sự kiện giao diện (UI event), rồi giải phóng nó khỏi bộ nhớ Heap. Tuy nhiên, trình quản lý sự kiện của PDF Viewer (Foxit/Adobe) không nhận biết được đối tượng đã biến mất và vẫn giữ lại con trỏ. Khi sự kiện được kích hoạt lại, con trỏ lơ lửng này sẽ được sử dụng để đọc hoặc ghi, tạo ra điểm tựa vững chắc để kẻ tấn công chèn shellcode thực thi RCE.
- Filter / Decoder Anomalies:
    - Dữ liệu bên trong luồng PDF thường được mã hóa (Compression/Encoding) bằng các chuẩn như FlateDecode (Zlib), DCTDecode (JPEG), hay JBIG2Decode. Các thuật toán giải nén này yêu cầu cấp phát trước một mảng bộ nhớ đệm (buffer) để chứa dữ liệu đầu ra. Kẻ tấn công sửa đổi trực tiếp metadata của tệp ảnh/tệp nén, khiến phần mềm đọc ra một giá trị width và height nhỏ (cấp phát bộ nhớ ít), nhưng sau đó luồng dữ liệu thực tế bung ra lại dài hơn rất nhiều. Việc chênh lệch này dẫn đến Heap-based Buffer Overflow (như trong trường hợp CVE-2026-6306 của Chrome PDFium ), ghi đè qua ranh giới bộ đệm và thay đổi dữ liệu liền kề trên RAM
## Các hướng nghiên cứu hiện tại
Không thể sử dụng byte-level fuzzing truyền thống như AFL vì PDF parser sẽ từ chối xử lý ngay từ giai đoạn đầu.
- grammar-based and structure-aware fuzzing: Tạo grammar, kết quả đầu ra luôn luôn khớp về mặt ngữ nghĩa.
- token level fuzzing: phân mảnh cấu trúc tệp thành một chuỗi các "token" có cơ sở (ví dụ như các từ khoá obj, stream, toán tử,...), sau đó chỉ thực hiện thêm, sửa xoá, hoán vị ngẫu nhiên các token này.
- robust fuzzing & occluded vulnerabilities: sử dụng để bỏ qua một đoạn code có bug để thực hiện fuzzing tiếp các phần phía sau, phương pháp này sử dụng các kỹ thuật phân tích nhị phân động để cô lập lệnh hợp ngữ gây ra ỗi, sau đó inject các intruction đặc biệt như `nop` hoặc `jmp`,...để bỏ qua đường rẽ nhánh, ép thực hiện tiếp chương trình dù đã có bug.



### Câu hỏi
1. Trước khi được biên dịch, thì các ngôn ngữ lập trình đều phải đi bộ một thành phần na ná Abstract Syntax Tree (AST) để kiểm tra tính hợp lệ về mặt ngữ nghĩa của đoạn code?