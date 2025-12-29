- Có 2 loại cú pháp hay được sử dụng là Intel Syntax cho kiến trúc x86 và AT&T Syntax cho các hệ thống kế thừa từ UNIX.
- Cần phải biết cả 2 loại cú pháp này vì Security Reseacher sẽ có thể viết dưới dạng cả 2 loại cú pháp và cần phải đọc hiểu các báo cáo này.
- Intel Syntax được ưa chuộng sử dụng cho Windows (và sự thật là nó đọc dễ hiểu hơn thật) với thứ tự sẽ từ trái sang phải, cụ thể: `des <- source`
- AT&T Syntax được ưa chuộng sử dụng trên các hệ thống kế thừa từ Unix và GNU với thứ tự cú pháp sẽ là `source -> des`, trong đó register get a % prefix and immediates get a $.
- Một vài sự khác nhau đáng chú ý như sau:
    - Indicate sizes:
        - Intel indicates size like `mov qword ptr [rax], rbx`
        - AT&T Syntax indicates the size of memory operand is determined from the last character of the instruction, ví dụ: `movq` operates on quad word (qword). Do đó, vài lệnh sẽ được đổi tên to better conform to naming convention for lengths, ví dụ: `cwde` -> `cwtl` (convert sign extend word to long), `movsx` -> `movsbw`
        - `movslq %eax, %rdx` in AT&T Syntax have the same meaning with `movsxd rdx, eax` in Intel Syntax
    - Tính toán
        - `[base + index*scale + disp]` trong Intel tương đương trong AT&T `disp(base, index, scale)`, ví dụ: `call qword ptr [rbx+rsi*4-0xe8]` tương đương `callq *-0xe8(%rbx,%rsi, 4)`, tuy nhiên có vài trường hợp đặc biệt, ví dụ: `mov rdi, qword ptr[rsi+0x8]` có thể được viết thành `mov 0x8(%rsi),%rdi` hoặc `movq` đều được (vì khuyết index và scale), nhưng thường là compiler sẽ lựa chọn cách viết `mov`.
- `endbr` (end branch) is a security feature (`CET`, `control flow enforcement technology`, của intel nhằm đánh dấu các điểm an toàn cho các lệnh nhảy gián tiếp như `jump` và `call`, để nếu CPU nhảy đến một địa chỉ mà lệnh đầu tiên không phải là `endbr` thì sẽ coi đó là hành vi bất thường và crash), when the code is run on system doesn't support this, it acts as nop assembly instruction
- AT&T Syntax được sử dụng trên Linux, Unix, GDB, GCC mặc định và Intel Syntax được dùng cho Windows, các công cụ dịch ngược như IDA, Ghida.
## Example
### SingleLocalVariable.c
![stack frame with base pointer](image-60.png)
- Trong `System V ABI` của Linux/Unix (Windows không có phần này), stack frame sẽ chia ra làm 3 khu vực chính:
    - Khu vực trên `rbp`: vùng của caller, chứa các arguments và return address.
    - Khu vực giữa `rbp` và rsp: vùng stack frame tiêu chuẩn của một hàm, muốn sử dụng phải thực hiện lệnh `sub rsp, X` để nới rộng stack ngay đầu hàm. Ví dụ như vùng `main() frame` trong hình này
    ![alt text](image-61.png)
    `rbp` của hàm sau luôn trỏ ngược về `rbp` của hàm trước đó để tạo thành một linked list, giúp lần ngược lại lịch sử gọi hàm (stack trace).
    - Khu vực dưới `rsp`: vùng redzone. Thông thường, nếu có một ngắt hệ thống xảy ra bất ngờ, hệ điều hành sẽ lưu trạng thái xử lý ngắt vào khu vực này. Tuy nhiên `System V ABI` đưa ra cam kết sẽ không bao giờ chạm vào 128 bytes ngay phía dưới `rsp`, mà nó sẽ bỏ trống 128 bytes rồi mới ghi dữ liệu (tức là nó sẽ ghi tại byte thứ 129). Vậy thì 128 bytes này được sử dụng cho leaf function (tức các hàm không gọi hàm nào nữa)
- Chú thích (nguồn Gemini):
    - Trong hiện đại thì compiler thường bỏ qua `rbp` mà quản lý mọi thứ bằng `rsp` luôn để tiết kiệm lệnh
    - `return address` chuẩn là phải nằm ở `previous frame` chứ không phải `current frame`, vì caller frame là nơi gọi hàm `call` và đẩy return address vào stack, tuy nhiên với góc nhìn ngữ cảnh, `return address` cũng có ý nghĩa quan trọng đối với callee function, do đó nhiều tài liệu vẫn ghi return address thuộc sở hữu của `current frame`
    - `rbp` đóng vai trò là trần và `rsp` đóng vai trò là sàn chứa toàn bộ stack frame của một hàm, khi cần khai báo thêm biến thì sàn sẽ hạ thấp xuống.
    - Khi sử dụng `-fstack-protector`, compiler sẽ chèn giá trị canary (một giá trị đặc biệt nào đó) ngay sát bên dưới địa chỉ mà `rbp` đang trỏ tới, cụ thể trên kiến trúc 64 bit thì nó nằm ở `rbp - 8`. Vì lỗ hổng Buffer Overflow thường bắt nguồn từ việc xuất phát từ các biến cục bộ rồi cố gắng tràn ngược lên trên để ghi đè lên `saved rbp` (`rbp + 0`) và `return address` (`rbp + 8`), và nếu muốn ghi đè thì bắt buộc phải ghi đè lên cả giá trị canary. Do vậy, tại cuối mỗi hàm chỉ cần sử dụng hàm kiểm tra giá trị canary là được, nếu nó khớp thì cho return còn không thì báo lỗi.