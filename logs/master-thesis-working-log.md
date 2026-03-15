Đề tài 1. Sử dụng Nautilus tạo input nhằm fuzzing JS engine trong Adobe Reader và Foxit Reader
- [subtask] [in-progress] Chạy fuzzing WinAFL cho Adobe Reader với hiệu suất thấp (chạy qua GUI).
    - Mục tiêu: Fuzzing được
    - Vấn đề:
    - Phương pháp thực hiện:
        - Cách 1: Sử dụng `-fuzz_iterations 1` để tự khởi động acrobat, nạp file và tự giết tiến trình mỗi test case nhưng làm thế này làm bỏ đi lớp theo dõi của DynamoRIO.
        - Cách 2: Tìm target offset trong `acrobat.exe`.
            - 
- [subtask] Chạy fuzzing WinAFL cho Adobe Reader với hiệu suất cao (sử dụng harness).
- [subtask] Gộp Nautilus và WinAFL lại với nhau.
