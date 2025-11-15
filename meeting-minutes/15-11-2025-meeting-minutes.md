Hiện tại có 2 phương án cho luận văn tốt nghiệp
Phương án 1: Sử dụng AST (sử dụng cái có sẵn hoặc tự phát triển, nhưng khả năng rất cao là có sẵn rồi) và phát triển một phương pháp mutate các subtree cụ thể (dựa trên các lỗ hổng pdf đã tìm được) cho định dạng PDF nhằm tăng code coverage,...thay vì generic như superion.
Phương án 2: Sử dụng AST để xây dựng biểu diễn trung gian rồi thực hiện mutate trên biểu diễn trung gian đó để làm giàu về mặt semantic bởi vì thực tế, người tấn công sẽ sử dụng ngữ nghĩa để ẩn code tấn công (chèn mã độc vào một đoạn nào đó). Hiện tại đã có người nghĩ ra ý tưởng và thực hiện phương pháp này rồi với javascript engine rồi (đọc báo trong phần js runtime drive của thầy). Hiện tại thầy có nhóm nghiên cứu tương tự với database nhưng chưa tải tài liệu tham khảo lên, sẽ hỏi lại sau
2 phương án này có thể độc lập hoặc làm cùng nhau được và hoàn thành dù chỉ 1 trong 2 là đã đủ ra trường rồi.

## Công việc tuần tới

1. Nghiên cứu các CVE liên quan đến PDF
2. Tham khảo bài báo fuzzil và Fuzzili
