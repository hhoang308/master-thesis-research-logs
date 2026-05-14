## Khảo sát các CVE có payload liên quan đến PDF
#### CVE-2024-4367 (Mozilla PDF.js)
1. Reference:
https://codeanlabs.com/2024/05/cve-2024-4367-arbitrary-js-execution-in-pdf-js/
https://github.com/s4vvysec/CVE-2024-4367-POC
2. Triage
    - CPE: pdf.js library (by Mozilla) version < 4.2.67, ảnh hưởng trực tiếp đến Firefox phiên bản < 126 và các ứng dụng sử dụng thư viện này.
    - CWE: CWE-94 (Improper Control of Generation of Code - Code Injection)
    - CVSS (Common Vulnerability Scoring System):
        - Attack Vector (AV): Local/Network
        - Attack Complexity (AC):
        - Privileges Required (PR): None
        - User Interaction (UI): Required (Người dùng phải mở tệp PDF độc hại)
3. Attack Surface
    - Target Object: Cơ chế render Font (cụ thể là hàm biên dịch Glyph - ký tự) của PDF.js
    - Data Flow:
        - Source: Lấy từ mảng FontMatrix - một metadata được định nghĩa bên trong các đối tượng Font (ví dụ, Type1 font) của cấu trúc file PDF.
        - Sink: Hàm `new Function()` của JavaScript. PDF.js lấy dữ liệu từ Source, nối chuỗi và đưa thẳng vào tham số tạo hàm của Sink mà không qua kiểm tra kiểu dữ liệu.
4. Technical
    - Context: Để tăng tốc độ render các ký tự font trên màn hình, PDF.js không render từng nét một cách tuần tự, mà nó tự động generate ra một hàm JavaScript (dùng new Function) chứa sẵn các lệnh vẽ (như `c.save()`, `c.transform()`) rồi mới thực thi hàm đó để tối ưu hiệu năng.
    - Patch Analysis:
    - Root Cause Analysis: Trong bộ lệnh được generate ra có lệnh `c.transform()`, lệnh này nhận các tham số được lấy từ mảng FontMatrix. Developers của PDF.js mặc định tin tưởng rằng mảng FontMatrix này chỉ chứa number. Tuy nhiên, PDF format cho phép định nghĩa dữ liệu kiểu String bên trong mảng này. Khi nối chuỗi (join) để nhét vào new Function, nếu có một chuỗi chữ, nó sẽ được chèn nguyên văn mà không có dấu ngoặc kép bao quanh. Trong PDF, chuỗi được đặt trong dấu ngoặc đơn ().
5. POC:
    - Payload:
        - Chọn một file PDF bất kỳ có tham số `/FontMatrix`, ví dụ định dạng cơ bản `/FontMatrix [1 2 3 4 5 6]`.
        - Chèn payload JS bằng dấu ngoặc đơn của PDF `/FontMatrix [1 2 3 4 5 (0\); alert\('Pwned!'\)]`.
        - Khi PDF.js render file này, nó sẽ generate ra đạn code JS như sau:
        ```
        c.save();
        c.transform(1, 2, 3, 4, 5, 0); alert('Pwned!');
        c.scale(size,-size);
        ```

6. Exploitability Assessment:
- Tùy thuộc bối cảnh.
#### CVE-2024-49576 (Foxit)
1. Reference:
https://www.sentinelone.com/vulnerability-database/cve-2024-49576/
2. Triage
    - CPE: Foxit PDF Reader version < 2024.3.0.26795
    - CWE: CWE-416 (Use After Free)
    - CVSS:
        - Attack Vector (AV): Network/Local
        - Attack Complexity (AC):
        - Privileges Required (PR):
        - User Interaction (UI): Required (Người dùng phải mở file PDF).
3. Attack Surface
    - Target Object: Đối tượng CBF_Widget (được dùng để xử lý các Checkbox trong PDF form) trong JS Enginge tích hợp trong Foxit Reader cho phép tương tác với các thành phần PDF.
    - Data Flow
        - Source: JavaScript code trong PDF
        - Sink: CBF_Widget object
4. Technical
    - Patch Analysis:
    - Root Cause Analysis:
        - Khi JavaScript Enginge trong PDF ra lệnh xóa hoặc thay đổi một Checkbox (CBF_Widget), Foxit sẽ giải phóng vùng nhớ chứa đối tượng đó.
        - Tuy nhiên, một thành phần khác của chương trình vẫn giữ lại pointer tới vùng nhớ vừa giải phóng đó mà không biết nó đã bị giải phóng.
        - Sử dụng kỹ thuật Heap Spraying để ngay sau khi vùng nhớ bị giải phóng, họ sẽ ép chương trình cấp phát một vùng nhớ mới có kích thước tương đương nhưng chứa dữ liệu độc hại (do kẻ tấn công kiểm soát). Khi chương trình quay lại sử dụng con trỏ cũ, nó sẽ vô tình thực thi dữ liệu độc hại đó.
5. POC
    - PoC (Proof of Concept):
        - Sử dụng WinDbg
        - Tạo một file PDF có nhúng mã JS, thực hiện các thao tác: khởi tạo checkbox, thực hiện giải phóng checkbox, truy cập lại đối tượng để gây crash
        - Quan sát qua WinDbg để thấy chương trình đang cố gắng truy cập bố nhớ không hợp lệ.
6. Exploitability Assessment
    - Impact:
    - Exploitability:
    - Mitigation:
