## Tấn công Xuyên lớp (Inter-layer Interaction)

- Fuzzing đa tầng tập trung vào sự tương tác giữa PDF DOM và JavaScript Engine. Đây là hướng đi hứa hẹn  vì sự tương tác giữa JS và PDF DOM là nơi phát sinh nhiều lỗ hổng nghiêm trọng.
- **Mục tiêu:** Tìm lỗi tại "cầu nối" (bridge) giữa cấu trúc dữ liệu PDF và môi trường thực thi JavaScript (Adobe Acrobat JavaScript API).
- **Điểm mới:** Tạo ra các tệp PDF mà trong đó mã JS thực hiện các thao tác thay đổi cấu trúc PDF (như thêm/xóa trang, thay đổi thuộc tính field) một cách liên tục và cực đoan để kích hoạt các lỗi Race Condition hoặc Use-After-Free trong bộ nhớ C++ của trình đọc.
- **Đối tượng tấn công:** Adobe Acrobat Pro, Foxit Reader.

### Khái niệm
- `PDF DOM` (Document Object Model) là cấu trúc dữ liệu tĩnh (thường có dạng cây) của PDF, là kết quả của quá trình parse bằng chương trình như Adobe. Mọi tương tác với văn bản PDF sẽ được thực hiện trực tiếp tới cấu trúc dữ liệu này.
- `JavaScript Enginge` là một thành phần bên trong của Adobe Acrobat, enginge này khác với enginge node.js và v8 của Chrome, mà là một thư viện nhỏ được nhúng vào bên trong file `AcroRd32.exe`, nhiệm vụ của nó là chạy các đoạn code JS nằm bên trong file pdf.
- Mục đích ban đầu của việc chèn `javascript` vào file pdf là để người dùng tương tác với file pdf đấy luôn, bao gồm: autoformat, tính toán tự động, kiểm tra dữ liệu, ẩn/hiện nội dung, kết nối và gửi dữ liệu. Nhưng các trình đọc pdf phổ biến như Chrome, Edge,...đã cố tình bỏ tính năng này vì lí do bảo mật và tốc độ, chỉ còn các phần mềm chính chủ như Adobe Acrobat Reader (hoặc Foxit Reader) là còn giữ tính năng này thôi.


### 1. Các mã nguồn nên kế thừa (The Tech Stack)

Kế thừa từ 3 dự án mã nguồn mở sau:

#### Lõi Fuzzer (Engine): WinAFL

- Đây là công cụ tiêu chuẩn để fuzzing phần mềm đóng trên Windows (như Adobe Reader). Nó cung cấp khả năng đo Code Coverage thông qua DynamoRIO.
- WinAFL chính là AFL nhưng về mặt kĩ thuật thực thi thay đổi để chạy được trên Windows. Cụ thể những phần giống nhau bao gồm: coverage-guided, mutation strategies, scheduler (cơ chế ưu tiên input). Còn những phần khác nhau bao gồm: WinAFL tập trung vào những phần mềm mã nguồn đóng (thay vì mã nguồn mở như AFL) và để đo coverage cho phần mềm đóng cần sử dụng DynamoRIO để chèn code theo dõi ngay lúc chương trình đang chạy (dynamic bninary instrumentation). Ngoài ra, lý do quan trọng nhất khiến WinAFL ra đời là Windows không có cơ chế fork() giống Linux. Trên Linux, mỗi lần thử một file pdf, AFL sẽ bật chương trình lên, chạy rồi tắt đi, trong Linux việc này rất nhanh do fork(). Tuy nhiên trên Windows không có chức năng này nên việc bật/tắt chương trình là rất lâu, do đó WinAFL sử dụng một mẹo là bật chương trình lên một lần duy nhất, cho nó chạy đến hàm cần thiết (target function), ghi nhớ trạng thái của CPU/RAM lúc này, rồi sau đó ném data vào và để target function chạy, sau khi chạy xong thì sẽ reset về trạng thái trước khi chạy (do đã ghi nhớ trạng thái từ trước), lấy data mới và chạy tiếp.

#### Bộ sinh mã JS (Generator): Nautilus

Nautilus có khả năng sinh các chuỗi lệnh dựa trên ngữ pháp (Grammar). Bạn sẽ lấy module sinh mã của Nautilus để tạo ra các đoạn AcroJS "độc hại".

#### Thư viện xử lý PDF (Manipulator): pikepdf (Python)

Thư viện này rất mạnh và ổn định để nhúng mã JS vào tệp PDF có sẵn mà không làm hỏng cấu trúc tệp.
### 2. Quy trình phát triển CrossBridge (Step-by-Step)
#### Bước 1: Thiết lập môi trường đo lường (Instrumentation)
Cấu hình WinAFL để nó có thể "nhìn" thấy bên trong Adobe Reader.
- Target: AcroRd32.exe
- Hàm mục tiêu (Target Function):  cần tìm hàm chịu trách nhiệm mở tệp PDF (thường là trong AcroRd32.dll hoặc AcroForm.api). WinAFL sẽ lặp lại việc gọi hàm này để fuzzing mà không cần khởi động lại toàn bộ chương trình.

#### Bước 2: Xây dựng bộ Grammar cho AcroJS

Dựa trên các API nhạy cảm của AcroJS  (deletePages, addAnnot, destroy),  viết tệp ngữ pháp cho Nautilus.
Nautilus sẽ xuất ra một tệp .js thô chứa các kịch bản tương tác xuyên lớp.

#### Bước 3: Tạo "Cầu nối" (The Bridge Script)
Viết một script Python đóng vai trò trung gian:
- Nhận mã JS từ Nautilus.
- Dùng pikepdf mở một file PDF mẫu (Seed).
- Chèn mã JS vào sự kiện /OpenAction của tệp PDF.
- Lưu tệp PDF này vào thư mục đầu vào của WinAFL.

### 3. Kiến trúc hệ thống CrossBridge mẫu

Dưới đây là cách các thành phần tương tác:
- Input: Một bộ Grammar (định nghĩa các API AcroJS).
- Generator (Nautilus): Sinh ra 1000 biến thể kịch bản JS khác nhau.
- Fuzzer (WinAFL):
    - Lấy tệp PDF (đã nhúng JS).
    - Chạy Adobe Reader.
    - Nếu gây ra Crash: Lưu tệp lại để phân tích lỗi.
    - Nếu có Coverage mới: Báo hiệu cho Generator biết để tập trung sinh thêm các kịch bản tương tự.
## Việc cụ thể cần làm:
1. Cài đặt WinAFL: Thử fuzzing một chương trình đơn giản trên Windows (như gdiplus.dll kèm theo ví dụ của WinAFL) để hiểu cách nó hoạt động.
2. Đọc bài Nautilus. (Nautilus: Fishing for Deep Bugs with Grammar-Aided Greybox Fuzzing, https://github.com/nautilus-fuzz/nautilus)
Viết thử Grammar: Dùng Nautilus sinh ra các đoạn JS đơn giản và dùng pikepdf nhúng thủ công vào một file PDF, mở bằng Adobe Reader để xem nó có thực thi đúng không.
3. Tìm hiểu về DynamoRIO: Đây là công cụ WinAFL dùng để đo coverage. Bạn cần biết cách dùng lệnh drrun.exe để chạy Adobe Reader.
https://github.com/nautilus-fuzz/nautilus
4. Đọc đọc tả PDF (ISO 320000) liệt kê ra các   API nhạy cảm của AcroJS  (deletePages, addAnnot, destroy, ..),  viết tệp ngữ pháp cho Nautilus.
Từ bộ Grammar (định nghĩa các API AcroJS nhạy cảm), Nautilus sẽ tạo ra JS, sau đó chèn JS vào PDF và thực hiện fuzz