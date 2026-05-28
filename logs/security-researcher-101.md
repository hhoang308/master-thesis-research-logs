## Quy trình nghiên cứu CVE
1. Tìm kiếm CVE và các thông tin liên quan
2. Triage (Định vị)
    - CPE (Common Platform Enumeration): Xác định chính xác phiên bản phần mềm, phần cứng,... bị ảnh hưởng.
    - CWE (Common Weakness Enumeration): Phân loại loại lỗ hổng (Buffer Overflow, Use-After-Free,...), vì CWE sẽ giúp hình dung ngay công cụ và phương pháp cần sử dụng (ví dụ, lỗi liên quan đến bộ nhớ cần quan tâm đến stack/heap,...).
    - CVSS (Common Vulnerability Scoring System): Phân tích các vector trong CVSS (AV, AC, PR, UI) để biết:
        - Attack Vector (AV): Lỗ hổng có thể bị khai thác từ xa (Network) hay chỉ khi kẻ tấn công có quyền truy cập vật lý (Physical).
        - Attack Complexity (AC): Mức độ phức tạp của việc khai thác (Low/High).
        - Privileges Required (PR): Mức độ đặc quyền cần thiết để khai thác (None/Low/High).
        - User Interaction (UI): Có cần người dùng tương tác để khai thác hay không (None/Required).
3. Attack Surface (Phân tích bề mặt tấn công)
    - Target Object: Xác định đối tượng tấn công cụ thể (binary, API, driver,...).
    - Data Flow: Xác định luồng dữ liệu đi vào từ đâu và điểm gây lỗi.
4. Technical Deep Dive (Nghiên cứu kỹ thuật chuyên sâu)
    - Patch Analysis: So sánh mã nguồn trước và sau khi vá để xem họ thay đổi những gì (ví dụ thêm check null, check boundary,...).
    - Root Cause Analysis: Tìm hiểu nguyên nhân gốc rễ của lỗi (ví dụ, nếu lỗi memory thì phải hiểu trạng thái của memory tại thời điểm gây lỗi, với lỗi về logic thì phải hiểu flow của chương trình,...).
5. POC (Tái hiện lỗi)
    - Dựng môi trường an toàn: Sử dụng VM và đảm bảo máy không có kết nối ra mạng ngoài (nếu nghiên cứu mã độc).
    - Debug & Trace: Sử dụng các công cụ debug (GDB, WinDbg, Frida...) tùy HĐH.
    - PoC (Proof of Concept): Tìm kiếm các script công khai để tái hiện.
6. Exploitability Assessment (Đánh giá khả năng khai thác)
Không phải CVE cũng dẫn đến RCE (Remote Code Execution).
    - Impact: Đánh giá mức độ ảnh hưởng của lỗi (RCE, DoS, Information Disclosure,...).
    - Exploitability: Đánh giá mức độ dễ dàng để khai thác lỗi (dựa trên CVSS và các yếu tố kỹ thuật).
    - Mitigation: Tìm hiểu các cơ chế bảo mật hiện tại có ngăn chặn được không, ví dụ, ASLR, Stack Canaries.

## Template
1. Reference
2. Triage
    - CPE:
    - CWE:
    - CVSS:
        - Attack Vector (AV):
        - Attack Complexity (AC):
        - Privileges Required (PR):
        - User Interaction (UI):
3. Attack Surface
    - Target Object:
    - Data Flow:
        - Source:
        - Sink:
4. Technical
    - Patch Analysis:
    - Root Cause Analysis:
5. POC
    - PoC (Proof of Concept):
6. Exploitability Assessment
    - Impact:
    - Exploitability:
    - Mitigation: