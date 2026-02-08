## WinDBG

#### loading binary
- `ctrl + e` to load binary.
- `q` command or `stop debugging` to stop the process.
#### running and restart binaries
- by default, WinDBG will stop at a breakpoint in `ntdll.dll` file after the program is loaded but before `main()` is called.
- breakpoint can be set based on a symbol name (if you have symbol).
- `bp $exentry` is the command to set breakpoint at first assembly instruction.
#### hardware and software breakpoints
- unlimited number of software breakpoints, because the debugger overwrite the first byte of the instruction (the instruction you set for breakpoint) with special byte `0xcc` (indicates `interrupt 3` instruction). so when the CPU runs to that instruction, it see `0xcc`, it calls debugger, debugger pop up, when you hit continue, the debugger will reverse to the original first byte, continue executing and replace `0xcc` again for the next hit.
- Intel set up 4 special register for hardware debug, which is dr0, dr1, dr2 and dr3. cpu will loads the address you want to monitor in these registers. when CPU read/write/execute in any address, cpu will silently compare with the address in these registers and it will stop when it finds the similar address.
#### attach to running userspace process
- `attach to a process` (shortcut `f6`)
- các phần mềm hiện đại như chrome, adobe reader,...thường hoạt động theo mô hình launcher và renderer, tức sẽ chạy 1 process cha, process này sau khi chạy lên sẽ thực hiện vài thao tác kiểm tra,...rồi sau đó sẽ tạo ra 1 process con, process con này mới chính là đơn vị sẽ thực hiện xử lý các chương trình của người dùng, sau đó process cha sẽ bị terminate hoặc vào trạng thái sleep. nên nếu muốn debug process con thì phải attach sau khi đã mở xong process cha và chương trình chạy ổn định.
- có vài chương trình chạy ngầm không thể tắt trên hệ điều hành mà muốn debug thì phải sử dụng tính năng này.

#### commands
- `.reload /f`: force reload symbols, wait a few minutes.
- `u $exentry`: linear unassemble (~ 8 lines from the given address).
- `uf <address>`: unassemble function, displays all assembly code of the given function, consists of jmp.
- `g`: continue executing.
- `.restart`: restart program execution from the beginning.
- `bp <address or symbol name>`: set breakpoint at that given address.
- `bl`: breakpoint lists.
- `bc <breakpoint number given by bl command>`: breakpoint clear.
- `ba <access type> <data size> <address>`: break on access command
    - break on write, for example, `ba w 4 000000000014fdf4`, which means break when 4 bytes are written to address `000000000014fdf4`.
    - Intel hardware doesn't support break-on-read only, so some debugger stimulates this by setting both a break on read/write and a break on write and then you have to determine yourself if the break must have been a read-only access. in short, it hits when access this address (even if read or write), for example, `ba r 4 000000000014fdf4`.
    - break on execute, `ba e 1 0000000140001050`.
- `bd <breakpoint number given by bl command>`: breakpoint disable.
- `be <breakpoint number given by bl command>`: breakpoint enable.
- `r <register> = <value>`: modify register.
- `db <address> L<number>`: display <number> bytes starting at <address>.
- `dd <address> L<number>`: display double words.
- `dq <address> L<number>`: display quad words.
- `da <address>`: display as ascii string at that address until first null terminator.
- `eb <address> <vallue>`: edit bytes.
- `k`: get stack backtrace.
- `p`: step over a single instruction.
- `t`: step into.
- `gu`: step out.
### Notes
- sau khi biên dịch ra một chương trình bất kì, đều có file .map hoặc .pdb nào đó để lưu symbol. tuy nhiên, các file này chứa nhiều thông tin nhạy cảm, ví dụ như tên hàm, tên biến, cấu trúc dữ liệu nội bộ,...nên trước khi phát hành bản thương mại, nhà sản xuất sẽ thực hiện bước lột bỏ, họ sẽ giữ lại file gốc (tức file .map hoặc .pdb) và chỉ gửi cho người dùng file .exe và file .dll (ở dạng binary) để chạy chương trình thôi.
- 