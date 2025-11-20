# PDFium Google Chrome CVE
## CVE-2025-1918
### References
https://www.cve.org/CVERecord?id=CVE-2025-1918


### Description
`Out of bounds read` in PDFium in Google Chrome prior to 134.0.6998.35 allowed a remote attacker to potentially perform `out of bounds memory access` via a crafted PDF file. (Chromium security severity: Medium)



# How to analysis CVE

1. Tìm hiểu về chức năng và nguyên lý hoạt động của program thông qua tài liệu
2. Phân tích source code để hiểu các tính năng thực sự hoạt động như thế nào
3. Sử dụng thư viện được audit liên tục luôn luôn an toàn hơn là tự bản thân implement một cái khác với tính năng tương đương
4. Compile với tuỳ chọn ASAN (address sanitizer) và biên dịch với tuỳ chọn không sử dụng optimizaiton
5. Ưu tiên fuzzing những thứ mà ít người fuzzing -> tăng khả năng có lỗi



-------------------------------------------------------
1. `ldd` = list dynamic dependencies 
-> print the shared libraries required by each program and its mapping resolution (if the library was found)
-> `linux-vdso.so.1` and `linux-gate.so.1` is virtual shared object provided by the kernel to speed up system calls.
-> don't use `ldd` on untrusted binary because it may attempt to execute the program to determine its dependencies, use `objdump` or `readelf` instead.

2. compiler flags
2.1 address sanitizer
- mục đích: tìm bugs memory corruption tại runtime
- là builtin tool của compiler (gcc hoặc clang) 
- nguyên lý hoạt động là instrument code để thêm các extra checks quanh các biến và những đoạn cấp phát bộ nhớ, nếu chương trình truy cập vào những vùng nhớ không cho phép thì ASan sẽ dừng chương trình ngay lập tức và in ra lỗi.
-> PHẢI ĐỌC THÊM TÀI LIỆU VỀ CÁI NÀY SAU

2.2 optimization
- mục đích: khiến chương trình chạy nhanh hơn và tốn ít bộ nhớ hơn
- nguyên lí hoạt động là nó sẽ phân tích logic và viết lại code sao cho tối ưu hơn cho CPU mà chức năng vẫn không đổi.
- tuy nhiên điều này cũng khó hơn cho việc debug bằng gdb vì mã máy lúc này đã không còn khớp với source code nữa.


3. `file` command
- xác định loại type dựa trên nội dung thay vì dựa trên extension
- nguyên lí hoạt động dựa trên `magic number` tại đầu mỗi file. `magic number` ở đây chính là chuỗi byte đặc biệt đại diện cho một định dạng file duy nhất 
- database được lưu trong `/usr/share/misc/magic.mgc`

4. FFmpeg (Fast Forward MPEG)
- opensource framework để xử lý multmedia data, hầu hết các video player như VLC hoặc các dịch vụ streaming như Youtube đều sử dụng cái này.
- chia ra làm 2 loại: command line tool để convert files, các library để sử dụng trong các chương trình khác.
- mục đích: chuyển đổi định dạng giữa các file, nén và giải nén?, gửi video đến một server và nhận nó, chỉnh sửa video frames,...
- "./ffmpeg -i https://localhost:12345/" 
	-i: argument sau là input (source of data)
	=> hoạt động như một client, đọc media stream từ server localhost port 12345 (điều này có nghĩa là server localhost đấy cũng phải đóng vai trò là server và có gửi dữ liệu gì đó mới được)

