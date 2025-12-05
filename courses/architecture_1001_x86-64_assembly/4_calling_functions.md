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

### How to read 2-operands instructions
- tham số: `destination` và `source` có thể viết dưới dạng `r/mX` chứ không được phép viết dưới dạng `r/mXs` vì dạng này cho phép memory to memory transfer, thứ mà kiến trúc x86 cấm.
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
