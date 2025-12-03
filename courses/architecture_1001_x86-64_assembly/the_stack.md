## The Stack 
- `stack` là một khu vực "trừu tượng" nằm trong RAM mà OS cấp phát cho một chương trình khi chương trình đấy chạy. Tùy hệ điều hành và có sử dụng address space layout randomization (ASLR) hay không mà `stack` sẽ bắt đầu tại những vị trí khác nhau.
![process memory layout](image-3.png)
- `RSP` trỏ đến đầu của `stack` tức nơi có địa chỉ bé nhất, những giá trị nào nằm ngoài khoảng xác định sẽ được coi là `undefined`
- những thông tin có thể tìm thấy trong `stack`: `return addresses`, `local variable`, truyền `argument` giữa các `functions`, cấp phát động thông qua `alloca()`, tiết kiệm không gian cho `register`.
### Push and Pop Instruction
- không có push and pop trong visual studio?
- 32-bit mode -> push/pop will add/remove value 32 bits a time rathar than 64 bit and thus they decrement/increment rsp by 4 rather than 4 at a time, likewise in 16 bit mode.
#### Push
- push quadword (8 bytes) onto the stack
- decrements the stack pointer RSP by 8 (not 8 bytes, 8 bits, just 8, because this is the value of address) (stack address decrease, so the more growth the smaller)
- operand can be the value of register or value from memory
- in Intel syntax, `[]` means to fetch the value at that address (like dereferenceing a pointer)
- `r/mX` can take 4 forms:
    - register -> `rbx`
    - memory, base-only -> `[rbx]`
    - memory, base+index*scale -> `[rbx+rcx\*X]`
    - memory, base+index*scale+displacement -> `[rbx+rvx\*X+Y]`
- `r/mX` example:
    - push register : `push rbx`
    - push memory : `push [rbx]`, `push [rbx+rcx*4]`, `push [rbx+rcx*8+0x20]`
- WinDbg sử dụng kí hiệu "`" để ghi chú mỗi 32 bit cho dễ đọc.
- `stack` trước và sau khi thực hiện lệnh `push rax`
![stack before, after](image-8.png)
#### Pop
- pop a value from the stack
- operand can be register or memory address in the form `r/mX` 
- `stack` trước và sau khi thực hiện lệnh `pop rax`
![stack before and after pop rax](image-9.png)
