### 1. Compile with GDB
Phải thêm options `-ggdb` hoặc `-g` thì mới sử dụng được gdb
`gcc -ggdb fibber.c -o fibber_bin`
### 2. Load Binary
Có 2 cách thực hiện
- Cách 1: `gdb --quiet ./fibber_bin`
    - Trong đó: --quiet is equivalent to -q
- Cách 2: Run "gdb -q" and then load the executable to be run with the `file "<path_to_executable>"`
### 3. Quit gdb
Enter "quit" or "q" at the (gdb) command prompt:

### 4. Commands: run, start, continue
The `run` (short form: r) command will `run` the program just the same as if you had invoked int from the CLI
If your program requires CLI arguments, they can be passed after the `run` command, the same way they would be if you were directly invoking the program.
```
#include <stdio.h>
void main(int argc, char ** argv){
    if(argv[1] != NULL && argv[1] != ""){
        printf("You entered %s for argv[1]\n", argv[1]);
    } else {
        printf("You didn't enter an argv[1]\n");
    }
}
```
The result is:
```
user@ubuntu:~$ gdb ./echo_bin -q
Reading symbols from ./echo_bin...
(gdb) r
Starting program: /home/user/echo_bin 
You didn't enter an argv[1]
[Inferior 1 (process 60868) exited with code 034]
(gdb) r hi
Starting program: /home/user/echo_bin hi
You entered hi for argv[1]
[Inferior 1 (process 60872) exited with code 033]
(gdb) r yo
Starting program: /home/user/echo_bin yo
You entered yo for argv[1]
[Inferior 1 (process 60873) exited with code 033]
```
The `start` command is like `run`, except it sets a breakpoint on the entry point of the program. Like `run`, you can specify additional CLI arguments after the start command

If you started the program with `start`, you can continue its execution from the temporary breakpoint it sets by using `continue` (short form: `c`).

### 5. Working with Breakpoints in Source & Assembly Debugging

#### Setting Code Breakpoints:

Breakpoints can be set with the `break` command (short form: `b`).
```
(gdb) break main
Breakpoint 1 at 0x11a9
(gdb) b fibbonacci
Breakpoint 2 at 0x1169
```
#### Listing Code Breakpoints:

Breakpoints can be listed with either `info breakpoints` or the shorter forms `info break` and info b
```
(gdb) info breakpoints
Num     Type           Disp Enb Address            What
1       breakpoint     keep y   0x00000000000011a9 <main>
2       breakpoint     keep y   0x0000000000001169 <fibbonacci>
(gdb) info b
Num     Type           Disp Enb Address            What
1       breakpoint     keep y   0x00000000000011a9 <main>
2       breakpoint     keep y   0x0000000000001169 <fibbonacci>
```
#### Unsetting Code Breakpoints:

The `clear <address>` command can remove a breakpoint at the address, specified either by a symbol name, or a `*` followed by an absolute memory address.
```
(gdb) clear *0x00000000000011a9
(gdb) info b
Deleted breakpoint 1 Num     Type           Disp Enb Address            What
2       breakpoint     keep y   0x0000000000001169 <fibbonacci>
(gdb) clear fibbonacci
(gdb) info b
Deleted breakpoint 2 No breakpoints or watchpoints.
```
The `delete <breakpoint number from info breakpoints>` (short form: `d`) will delete the specific breakpoint given by a number taken from the `info b` output.
```
(gdb) b main
Breakpoint 3 at 0x11a9
(gdb) info b
Num     Type           Disp Enb Address            What
3       breakpoint     keep y   0x00000000000011a9 <main>
(gdb) delete 3
(gdb) info b
No breakpoints or watchpoints.
```
Breakpoints as offets vs. addresses in memory

Note in the below how when breakpoints are initially printed out, they are small numbers. But then when the program is started, and they are printed out again, they are larger numbers. This is because before the program is run, the “address” is really just an offset from the start of the code execution of the executable. The debugger doesn’t yet know where the program will be loaded into memory, until it’s actually executed. This is also true because operating systems may randomize where the executable is loaded in memory (a mechanism called Address Space Layout Randomization (ASLR), which is designed to help mitigate some security vulnerabilities.) Therefore, after the executable is started, the debugger now can display the real addresses for the executable after it has been loaded in memory and started. (Note: if you do the same steps, your addresses will often differ from what’s shown below.)

#### Enable and Disable Breakpoints
- `disable <breakpoint number>`
- `enable <breakpoint number>`
#### Listing source code lines

This option is only relevant to situations in which you have the source code for the program being debugged, and it was built with symbols enabled (the `-g` or `-ggdb` flags to gcc). It will not be useful when reverse engineering `opaque binaries`, but it is useful where you compile source code yourself.

The `list` command will show you source code surrounding the current location you are stopped at.

The `list <function name>` command will show source code before and after the given function.

The `list <source file name>:<line number>` command will show source code before and after the given line in the given file.

#### Examining Assembly
- The `disassemble` command (short form: `disas`) by itself will show you assembly surrounding the current location you are stopped at. The `disassemble <address or symbol name>` command will disassemble the memory specified by the address or symbol name.
    - You can optionally give the `/r` argument after `disassemble` to display the raw bytes which make up the instructions.
    - You can optionally give the `/m` argument after `disassemble` to mix source code and assembly.
    - To give both arguments simultaneously, they must be combined as `/rm`.

- Alternatively, one form of the `x` (examine memory) command can display some number of instructions (10 in the below example) at a given address. That form is: `x/10i <address>`
- One form of the display command, can ensure that every time the program stops, that some number of instructions (10 in the below example) are printed out starting at the address given in a register that represents the next assembly instruction for the CPU to execute. That form is: `display/10i <instruction pointer / program counter>`
- On x86 systems the register is named the “instruction pointer”, and specifically “RIP” for 64-bit code, so the command would be `display/10i $rip`
- On x86 systems the register is named the “instruction pointer”, and specifically “EIP” for 32-bit code, so the command would be `display/10i $eip`
- On ARM or RISC-V systems the registers is named the “program counter” would be `display/10i $pc`

### 6. Examining and Modifying Registers and Memory
#### Examine Registers and Memory
- Rất nhiều command trong gdb hỗ trợ `format specifier`, chỉ cần thêm cái này vào sau lệnh thì sẽ giúp biểu diễn thông tin sang một dạng khác. `format specifier` (hay còn gọi là `/FMT` vì trong `gdb help` gọi là như thế) là một tổ hợp bao gồm: `/nfu`, trong đó `n`, `f` và `u` là tùy chọn và sẽ tự động chuyển về một giá trị default hoặc giá trị nào đó đã sử dụng trước. Ví dụ như lệnh `display /10i` là `n` bằng 10 tương đương in ra 10, `f` là i trong instruction. **Chú ý, word trong GDB là 4 bytes, word trong thuật ngữ của Intel là 2 bytes, word trong thuật ngữ của RISC-V là 2 bytes.**
    - Một vài giá trị thông dụng của `f`:
        - `x` for hexadecimal
        - `d` for signed decimal
        - `u` for unsigned decimal
        - `c` for ASCII characters
        - `s` for a full (presumed-null-terminated) ASCII string.
        - `i` for instruction
    - Một vài giá trị thông dụng của `u`:
        - `b` for bytes
        - `h` for half-words (2 bytes)
        - `w` for words (4 bytes)
        - `g` for giant-words (8 bytes)`
- The `info registers` (short form: `info r`) command will by default print some set of registers which the GDB developers consider most commonly needed. You can also use the `print` command (short form: `p`) with a `/FMT` specifier, using only the `f` format specifier, to print a single register. The print command requires the register name to be prefixed with a `$`. The `info r` command requires no such prefix. On Intel this can lead to confusion with the x86-64 AT&T syntax requirement that immediates are prefixed with a `$` whereas registers are prefixed with a `%`. This will be something you will just have to memorize and keep in mind
    ```
    (gdb) info r rax rbx rsp
    rax            0x5555555551a8      93824992235944
    rbx            0x7fffffffdde8      140737488346600
    rsp            0x7fffffffdcb0      0x7fffffffdcb0
    (gdb) print/x $rax
    $1 = 0x5555555551a8
    (gdb) print/d $rax
    $2 = 93824992235944
    ```
- The `x` command (for examine memory) supports the `/FMT` specifier.
    ```
    (gdb) x/8xb $rsp
    0x7fffffffe038: 0xb3    0xd0    0xde    0xf7    0xff    0x7f    0x00    0x00
    (gdb) x/4xh $rsp
    0x7fffffffe038: 0xd0b3  0xf7de  0x7fff  0x0000
    (gdb) x/2xw $rsp
    0x7fffffffe038: 0xf7ded0b3  0x00007fff
    (gdb) x/1xg $rsp
    0x7fffffffe038: 0x00007ffff7ded0b3
    (gdb) x/s 0x555555556008
    0x555555556008: "First %d elements of the Fibbonacci sequence: "
    (gdb) x/3i $rip
    => 0x5555555551a9 <main>:   endbr64
    0x5555555551ad <main+4>: push   %rbp
    0x5555555551ae <main+5>: mov    %rsp,%rbp
    ```
#### Modifying Register
- The `set` command, when used in conjunction with an equals sign can be used to set registers. Note that the size written was the size of the register (16 bits, ax) rather than the size of the immediate (32 bits).
    ```
    (gdb) set $rax = 0xdeadbeeff00dface
    (gdb) p/x $rax
    $15 = 0xdeadbeeff00dface
    (gdb) set $ax = 0xcafef00d
    (gdb) p/x $rax
    $16 = 0xdeadbeeff00df00d
    ```
#### Modifying Memory
- Similar to registers, memory can be modified with the `set` command, also optionally specifying a C-style type in order to specify the length to write. Immediates which are smaller than the size of the specified memory write are zero-extended, not sign-extended.
    ```
    (gdb) x/1xg $rsp
    0x7fffffffe038: 0x00007ffff7ded0b3
    (gdb) set {char}$rsp = 0xFF
    (gdb) x/1xg $rsp
    0x7fffffffe038: 0x00007ffff7ded0ff
    (gdb) set {short}$rsp = 0xFF
    (gdb) x/1xg $rsp
    0x7fffffffe038: 0x00007ffff7de00ff
    (gdb) set {short}$rsp = 0xFFFF
    (gdb) x/1xg $rsp
    0x7fffffffe038: 0x00007ffff7deffff
    (gdb) set {long long}$rsp = 0x1337bee7
    (gdb) x/1xg $rsp
    0x7fffffffe038: 0x000000001337bee7
    ```
    Note that for the `set {short}$rsp = 0xFF` it did not sign-extend the 1-byte value to be a 2-byte short value of 0xFFFF. It just zero-extended the 0xFF to 0x00FF, which is why the bottom 2 bytes became 0x00FF.
#### Updating Stack View
- This can be accomplished simply by using the display command previously discussed, with a `/FMT` format specifier indicating how many hex words (32-bit) or giant-words (64-bit) you’d like to display starting at the address in the stack pointer register. You should generally match the size, to that of the pointer size. So words on 32-bit architectures, and giant-words on 64-bit.
    ```
    (gdb) display/10xg $rsp
    1: x/10xg $rsp
    0x7fffffffdcb0: 0x00007fffffffddff      0x00007fffffffdde8
    0x7fffffffdcc0: 0x00007fffffffdd60      0x00007ffff7c2a1ca
    0x7fffffffdcd0: 0x00007fffffffdd10      0x00007fffffffdde8
    0x7fffffffdce0: 0x0000000155554040      0x00005555555551a8
    0x7fffffffdcf0: 0x00007fffffffdde8      0xde4826cc3f709021
    (gdb) display/10xw $rsp
    2: x/10xw $rsp
    0x7fffffffdcb0: 0xffffddff      0x00007fff      0xffffdde8      0x00007fff
    0x7fffffffdcc0: 0xffffdd60      0x00007fff      0xf7c2a1ca      0x00007fff
    0x7fffffffdcd0: 0xffffdd10      0x00007fff
    (gdb) display/11xw $rsp
    3: x/11xw $rsp
    0x7fffffffdcb0: 0xffffddff      0x00007fff      0xffffdde8      0x00007fff
    0x7fffffffdcc0: 0xffffdd60      0x00007fff      0xf7c2a1ca      0x00007fff
    0x7fffffffdcd0: 0xffffdd10      0x00007fff      0xffffdde8
    ```
#### Stack Backtrace
- `backtrace` (short form: `bt`) provides a call stack backtrace. The most-recently called function is on top of the backtrace, and the least-recently called function is on the bottom. Or another way to say it is that the function on top was called by the function below it. And the address shown is where the function will return to when it’s done.
    ```
    (gdb) bt
    #0  fibbonacci (n=0) at ./fibber.c:4
    #1  0x0000555555555742 in main () at ./fibber.c:18
    ```
### Stepping Through Assembly
- `nexti` (short form: `ni`) _steps over_ the next instruction, continuing from the instruction after it, especially if it is a control-flow instruction that might return to the same location (like `call` type instructions.)
- `stepi` (short form: `si`) _steps into_ the next instruction, continuing from wherever that instruction would lead, especially if it is a control-flow instruction (like `call` or `jump` type instructions.)
- `finish` (short form: `fin`) _steps out_ of the current function context, finishing executing the rest of the code until the end of the function.
### Run Until Address
- If you just want to step forward some number of instructions, but don’t want to set and then delete a breakpoint, or don’t want to count how many instructions it is, you can use the `until <address>` (short form: `u`) command.
- Note that, however that if for some reason the address is never reached (e.g. because control flow branches some other direction) the until command will also break upon exit from the current function.
### 7. GDB Misc
### GDB Commands File
- Often you will be starting and stopping the same or different executables multiple times in gdb. You may want to always run the same commands each time you start gdb, to set up a view of a common set of registers, memory, or instructions. Or you may want to set some common breakpoints. This can be accomplished by creating a plain text file (e.g. `~/x.cfg`), and entering the set of gdb commands which you want to be executed in gdb on startup, one per line. Then when you start gdb, specify the commands file with the `-x` option (e.g. `-x ~/x.cfg`).
- Recommended starting commands, copy and paste to your own ~/x.cfg commands text file.
    ```
    display/10i $rip
    display/x $rbp
    display/x $rsp
    display/x $rax
    display/x $rbx
    display/x $rcx
    display/x $rdx
    display/x $rdi
    display/x $rsi
    display/x $r8
    display/x $r9
    display/x $r12
    display/x $r13
    display/x $r14
    display/10gx $rsp
    start
    ```
    Expected result:
    ```
    hoangnh8@tag3l480t2:~/master-thesis-daily-logs/courses/debuggers_1012_introduction_to_gdb/excersises$ gdb -q -x x.cfg fibber_bin
    Reading symbols from fibber_bin...
    Temporary breakpoint 1 at 0x11b4: file fibber.c, line 23.

    This GDB supports auto-downloading debuginfo from the following URLs:
    <https://debuginfod.ubuntu.com>
    Enable debuginfod for this session? (y or [n]) [answered N; input not from terminal]
    Debuginfod has been disabled.
    To make this setting permanent, add 'set debuginfod enabled off' to .gdbinit.
    [Thread debugging using libthread_db enabled]
    Using host libthread_db library "/lib/x86_64-linux-gnu/libthread_db.so.1".

    Temporary breakpoint 1, main () at fibber.c:23
    23          unsigned int n = 10;
    1: x/10i $rip
    => 0x5555555551b4 <main+12>:    movl   $0xa,-0x4(%rbp)
    0x5555555551bb <main+19>:    mov    -0x4(%rbp),%eax
    0x5555555551be <main+22>:    mov    %eax,%esi
    0x5555555551c0 <main+24>:    lea    0xe41(%rip),%rax        # 0x555555556008
    0x5555555551c7 <main+31>:    mov    %rax,%rdi
    0x5555555551ca <main+34>:    mov    $0x0,%eax
    0x5555555551cf <main+39>:    call   0x555555555070 <printf@plt>
    0x5555555551d4 <main+44>:    movl   $0x0,-0x8(%rbp)
    0x5555555551db <main+51>:    jmp    0x555555555201 <main+89>
    0x5555555551dd <main+53>:    mov    -0x8(%rbp),%eax
    2: /x $rbp = 0x7fffffffdcc0
    3: /x $rsp = 0x7fffffffdcb0
    4: /x $rax = 0x5555555551a8
    5: /x $rbx = 0x7fffffffdde8
    6: /x $rcx = 0x555555557db8
    7: /x $rdx = 0x7fffffffddf8
    8: /x $rdi = 0x1
    9: /x $rsi = 0x7fffffffdde8
    10: /x $r8 = 0x0
    11: /x $r9 = 0x7ffff7fca380
    12: /x $r12 = 0x1
    13: /x $r13 = 0x0
    14: /x $r14 = 0x555555557db8
    15: x/10xg $rsp
    0x7fffffffdcb0: 0x00007fffffffdda0      0x00007fffffffdde8
    0x7fffffffdcc0: 0x00007fffffffdd60      0x00007ffff7c2a1ca
    0x7fffffffdcd0: 0x00007fffffffdd10      0x00007fffffffdde8
    0x7fffffffdce0: 0x0000000155554040      0x00005555555551a8
    0x7fffffffdcf0: 0x00007fffffffdde8      0x95e5670c8c18b5d4
    ```
### 8. Cheat Sheet
https://darkdust.net/files/GDB%20Cheat%20Sheet.pdf
https://users.ece.utexas.edu/~adnan/gdb-refcard.pdf