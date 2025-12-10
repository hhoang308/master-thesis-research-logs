#### CALL - Call Procedure
- mục tiêu: sử dụng khi muốn chuyển sang thực hiện một hàm khác nhưng sau khi thực hiện hàm đó xong thì vẫn quay trở lại chỗ trước đó được.
- quy trình thực hiện:
    1. thêm địa chỉ của lệnh tiếp theo vào `stack`
    2. sửa giá trị thanh ghi `RET` thành địa chỉ của lệnh nơi kết quả trả về
    3. sửa `RIP` thành giá trị của lệnh vừa được thêm vào `stack`
- tham số:
    1. `destination address` có thể là địa chỉ tuyệt đối hoặc địa chỉ tương đối (tức giá trị thanh ghi nào đó +- offset,...)

#### RET - Return from Procedure
- 2 định dạng
    1. Dạng 1: `pop` the top of the stack into `RIP` (implicitly increments stack pointer, `RSP`)
    2. Dạng 2: `pop` the top of the stack into `RIP` and also add a constant number of bytes to `RSP`, for example, `ret 0x8`, `ret 0x20`, etc.

#### MOV - Move
- Có thể sử dụng để chuyển giữa: register to register, memory to register and vice verse, immediate to register and memory.
- **KHÔNG ĐƯỢC PHÉP SỬ DỤNG** để chuyển giữa memory to memory
for example
- `mov [rbx], imm32` is immediate to memory
- `mov rbx, imm64` is immediate to register
- `mov rbx, rax` is register to register
- `mov [rbx], rax` is register to memory
- ` mov rax, [rbx]` is memory to register

#### ADD and SUB
- `add rsp, 8` means `rsp = rsp + 8`
- `sub rax, [rbx*2]` means `rax = rax - memorypointedtoby(rbx*2)`

### Example
![example 1](image-10.png)
- Đầu tiên `rsp` sẽ chứa địa chỉ của `main()` frame trước khi nó được gọi, do đó nó không khớp với giá trị nào trong hàm `main()` cả.
- `sub rsp, 28h` : do đây là `rsp` chứ không phải `[rsp]` nên `rsp = 14FE08h - 28h = 14FDE0`
- `call func(0140001000h)` : cập nhật giá trị thanh ghi `rsp` thành `1'40001019` vì đây là lệnh kế tiếp ngay sau lệnh `call`, hiểu đơn giản là sau khi tính toán xong hàm `call` thì lệnh thực hiện tiếp theo sẽ là lệnh có địa chỉ `1'40001019`
- `mov eax, 0BEEFh` : `rax = BEEFh`. Thanh ghi 64-bits, nếu chỉ ghi vào 32-bits LSB (tức ghi vào `eax`) thì CPU sẽ tự điền 0 vào 32-bits MSB còn lại. Nhưng nếu chỉ ghi vào 8-bits LSB (tức `al`) hoặc 16-bits LSB (tức `ax`) thì CPU sẽ dữ nguyên các giá trị của các bits còn lại trên `rax` chứ không tự điền 0 vào. 
- `ret` pop() value on top of the stack, thực thi trở lại dòng lệnh kế tiếp lệnh `call`
- `mov eax, 0F00Dh`
- `add rsp, 28h` để trở lại địa chỉ của `main()` frame. Chưa có giải thích lí do cụ thể vì sao lại có cộng và trừ 28h này nhưng tác giả thấy điều này trong Visual Studio.
- `ret` : thực thi tiếp lệnh tại địa chỉ `1'40001349`
Nếu bật tối ưu thì compiler sẽ bỏ qua hàm `func()` vì kết quả trả về của nó không được sử dụng.

### Assembly from Source
#### Setting Visual Studio
    1. Set as Startup Project
    2. Debug Information Format $\to$ Program Database
    3. Support Just My Code Debugging $\to$ No
    4. Default Basic Runtime Checks
    5. Disable Security Check
    6. Disable Incremental Linking
    7. Disable Randomized Base Address
#### GDB
Các lệnh trích xuất các thông tin sau bằng GDB

    1. Registers
    2. Memory (Address & Value)
    3. Call stack
    4. Disassembly
### How to read 2-operands instructions
- Tham số: `destination` và `source` có thể viết dưới dạng `r/mX` chứ không được phép viết dưới dạng `r/mXs` vì dạng này cho phép memory to memory transfer, thứ mà kiến trúc x86 cấm.
- `r/m` là viết tắt của register or memory, không bao gồm immediate (tức hằng số) trong assembly, ví dụ, `r/m32` là một thanh ghi hoặc memory location 32 bits.
- `r/mXs` không phải là một từ khóa chính thống mà nó chỉ mang ý nghĩa là "không được phép tồn tại 2 toán từ cùng là định dạng `r/mX` trong cùng một lệnh", ví dụ, `mov [var1], [var2]`.
#### Intel
_Destination $\gets$ Source(s)_
same with `y = 2x + 1`
for example:
- `mov rbp, rsp`
- `add rsp, 0x14` means `rsp = rsp + 0x14`
#### AT&T
_Source(s) $\to$ Destination_
same with `1 + 2 = 3`
for example: (register get a % prefix and immediates get a $)
- `mov %rsp, %rbp`
- `add $0x14, %rsp` means `rsp = rsp + 0x14`
