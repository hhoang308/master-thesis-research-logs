### Exercise SingleLocalVariable.c
![single local variable](image-11.png)
```c++
int func() {
    int i = 0x5ca1ab1e;
    return i;
}

int main() {
    return func();
}
```
### Key
![stack frame](image-13.png)
### Takeaways
- `local variable` lead to an allocations of space on the stack, within the function where the variable is scoped to.
- `0x18` reversed for only `0x4` (int) worth of data.