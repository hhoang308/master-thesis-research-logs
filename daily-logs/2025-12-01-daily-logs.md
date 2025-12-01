Multi-level Fuzzing for Document File Formats with Intermediate Representations

- bài báo này tập trung vào các target là document software, cụ thể là các software xử lý files dạng document (html, pdf, docx,...)
- kết quả đầu ra của bài báo là phát triển một intermediate document representation (DIR) cho document files và đề xuất một multi-level mutations (ví dụ page level, objectlevel, attribute level,...) thực hiện ngay trên DIR của document. DIR sẽ biểu diễn document dưới dạng một tập các pages, trong các pages sẽ chứa các object mà trong các object lại chứa các attributes.
- nguyên nhân dẫn đến bài toán này là do các công cụ generation-based fuzzing và strucutre-aware mutation tools có 2 nhược điểm chính: đầu tiên là phải tự tạo một grammar model cho từng document format -> tốn thời gian, thứ hai là chỉ thực hiện mutations đơn level -> nautilus thực hiện mutation các sub-tree, freedom thực hiện mutation các attribute của html files
