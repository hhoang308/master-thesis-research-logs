- CWE (Common Weakness Enumeration) là một danh sách các loại điểm yếu hoặc kiểu lỗi trong phần mềm có thể dẫn đến các lỗ hổng 
# Mục tiêu
1. Tìm kiếm các CVE xuất hiện trong Adobe Acrobat Reader trong năm 2025 với mục độ severity từ Medium trở lên, hiểu được các lỗi này khai thác điểm gì, nằm ở đâu, tìm kiếm các POC và tái hiện lại.
# Danh sách các CVE tìm được
1. https://www.cve.org/CVERecord?id=CVE-2025-43575
- Phiên bản: 24.001.30235, 20.005.30763, 25.001.20521 and earlier
- Loại lỗi: out-of-bounds write (viết dữ liệu vào ngoài vùng được cho phép)
- Khai thác: Thực thi một đoạn code ngẫu nhiên trong bối cảnh của người dùng hiện tại
- Điều kiện: Nạn nhân phải mở một file độc hại.
