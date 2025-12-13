- Định dạng PDF sử dụng graph-based object model và cơ chế truy cập ngẫu nhiên, do đó nếu sử dụng biểu diễn trung gian dạng cây sẽ bỏ qua attack surface quan trọng của pdf là cross-reference table, stream objects và incremental updates mechanism. Các đối tượng trong PDF có thể tham chiếu đến các đối tượng khác thông qua object number và generation number, do đó một đối tượng có thể sử dụng được nhiều lần mà không cần sao chép dữ liệu. Do đó để quá trình fuzzing thực sự hiệu quả, cần mô hình hoá các mối quan hệ phức tạp này. Ngoài ra, PDF cho phép hiển thị nhanh bất kì trang tài liệu nào thông qua cơ chế cross-reference table nằm ở cuối tệp. Bảng này lưu trữ offset (vị trí byte) của từng object do đó có thể truy cập chính xác từng object, và do có sự phụ thuộc về offset này mà một sự thay đổi nhỏ có thể ảnh hưởng đến tính toàn vẹn của file nếu cross-reference table không được cập nhật.
- PDF đóng gói dữ liệu lớn như ảnh, font vào các stream object và được nén hoặc mã hoá thông qua các filter. Đồng thời, mỗi stream sẽ đi kèm với stream dictionary, trong đó quan trọng nhất là `\Length` quy định độ dài thực tế của dữ liệu.  
- Định dạng HTML và XML được thiết kế  trên cấu trúc cây phân cấp, bắt đầu từ các thẻ gốc, rồi phân nhánh thành các thẻ con lồng nhau. Parser của HTML xử lý tài liệu theo tuần tự từ đầu đến cuối, khi gặp một thẻ mới, nó tạo thành một nút mới trong DOM (Document Object Model) và duy trì trạng thái ngữ cảnh cho đến khi gặp thẻ đóng tương ứng. Do đó cơ chế fuzzing cho HTML thường tập trung vào việc tạo các cây DOM phức tạp, lồng ghép thẻ hoặc sai lệch cặp thẻ.
### Phân vùng các lỗ hổng đặc thù
1. Xref Table
- Offset Mismatch: Sửa offset của Xref để trỏ đến đối tượng khác hoặc trỏ ra ngoài vùng nhớ được cấp phát
- Xref Cycles: Trong PDF 1.5+, bảng xref có thể được lưu trữ dưới dạng một stream nén (xref stream) và nó cũng là một object, do đó nó cần được tham chiếu, có thể sửa để xref tự tham chiếu chính nó gây tràn bộ nhớ.
- Hybrid Xref: Tồn tài cả xref và xref stream trong cùng một file
2. Stream and Filters
- Heap Overflow: Đặt `/Length` nhỏ hơn thực tế để ghi đè lên các dữ liệu liền kề
- Filter Chains: PDF cho phép áp dụng nhiều bộ lọc liên tiếp, do đó có thể can thiệp vào sự tương tác giữa các bộ lọc: thay đổi thứ tự, tham số,...
- Object Streams: PDF cho phép nhúng các đối tượng gián tiếp bên trong một stream nén.
3. Objects and Graph-based Object Model
- Recursion Bombs: Một đối tượng tham chiếu chính nó để tạo ra đệ quy vô tận
- Type Confusion: Tham chiếu đến đối tượng khác với mong đợi của parser
4. Incremental Updates and Shadow Attacks
- Shadow Attacks: Thay đổi nội dung hiển thị sau khi tài liệu đã được kí số bằng cách thao túng Xref trong phần cập nhật.
- Kiểm tra tính hợp lệ nếu có nhiều trailer và xref trong cùng một văn 
### Đánh giá hạn chế của biểu diễn trung gian đa năng đối với PDF
1. SDK Sanitization
- Việc sử dụng SDK để sinh mã PDF được thiết kế để tạo ra các tệp PDF hợp lệ. Khi DIR yêu cầu tạo một đối tượng, SDK sẽ tự động tính toán offset, cập nhật bẳng xref và thiết lập độ dài stream chính xác. Do đó vô tình loại bỏ khả năng tạo ra các tệp pdf bán hợp lệ và biến dạng cấu trúc.
2. Loss of Structural Semantics
- Các object không cần thiết phải nằm trong page mà có thể được tham chiếu bởi page, nhưng DIR dạng cây không thể biểu diễn sự khác biệt giữa đối tượng trực tiếp và đối tượng gián tiếp. Do đó không thể tạo ra các tệp có các tham chiếu lơ lửng hoặc tham chiếu vòng.
3. File Metadata Ignorance
- Trong DIR không có các thành phẩn như xref table, trailer. DIR tập trung vào nội dung hiển thị mà bỏ qua cấu trúc vật lý (physical layout), do đó các lỗ hổng liên quan đến offset, versioning, hay chữ ký số không thể được phát hiện thông qua phương pháp này.
### Đề xuất PDF-IR
Đồ thị đối tượng có tuần tự hoá (Serializable Object Graph) với quyền kiểm soát chi tiết đối với cấu trúc vật lý của tệp
1. Kiến trúc
2. Đột biến