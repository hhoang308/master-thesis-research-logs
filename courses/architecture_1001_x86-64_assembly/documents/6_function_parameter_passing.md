## Excersise
![pass 1 parameter](image-33.png)
### Solution
```
14fe08 : 1'40001389
14fe00 : 
14fdf8 :
14fdf0 : 
14fde8 : 
14fde0 : 00000000'00000011
14fdd8 : 1'4000102e
14fdd0 : 
14fdc8 :
14fdc0 : 00000000'00000011
```
```
ecx : 11h
eax : 11h
```
![solution pass 1 parameter](image-34.png)
## Excersise
![too many parameters](image-35.png)
### Solution
```
14fe08 : 1'40001389
14fe00 : 
14fdf8 :
14fdf0 : 0x55
14fde8 : 0x44
14fde0 : 0x33
14fdd8 : 0x22
14fdd0 : 0x11
14fdc8 : 1'40001000
14fdc0 : 
14fdb8 : 
14fdb0 : xxxxxxxx'ffffffef
```
```
eax : 0x22
ecx : 0x11
r9d : 0x44
r8d : 0x33
edx : 0x22
```
![solution too many parameters](image-36.png)
## Takeaway
- Ngày xưa, tại kiến trúc 32 bit x86, mọi tham số được đẩy vào `stack` (tức là `RAM`), tuy nhiên `RAM` vẫn chậm (nếu so với `register`). Do đó, `fast call` được sử dụng, quy định CPU phải sử dụng 4 `register` để lưu 4 tham số đầu tiên bao gồm thứ tự như sau: `rcx`, `rdx`, `r8`, `r9`. Tại `caller function`, nó không lưu các tham số vào `shadow store` mà chỉ đơn giản là reserve 32 bytes of empty stack space (the `shadow`, và nó sẽ luôn luôn reserve 32 bytes này kể cả có sử dụng tham số hay không), sau đó lưu giá trị của parameter vào `register` và thực hiện lệnh `call`. `Empty space` này tồn tại cho `callee function` (the function being called), nếu `callee function` hết `register` hoặc muốn sử dụng các fast call `register` cho việc tính toán thì `callee function` sẽ copy giá trị từ thanh ghi ra các `shadow store` đã reserver từ bước trên. Mọi tham số từ tham số thứ 5 trở đi (vượt quá 4), sẽ được lưu tại vùng nhớ stack ngay sau `shadow store` (nhìn solution của excersise toomanyparameter.c là hiểu).
- Do compiler sẽ luôn reserve 32 bytes of empty stack space cho 4 fast call register, do đó kể cả khi không sử dụng tham số nào, stack frame vẫn chứa 32 bytes cho fast call register. Các caller function luôn luôn sẽ có dòng `sub rsp, 28h` hoặc lớn hơn `28h` vì lí do này. Ví dụ về việc reserve 32 bytes như sau:
![call a subroutine](image-37.png)
![stack frame time out](image-38.png)