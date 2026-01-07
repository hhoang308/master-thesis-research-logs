#define uint64 unsigned long long
// int func(uint64 a, uint64 b, uint64 c, uint64 d, uint64 e) {
//     int i = a + b - c + d - e;
//     return i;
// }

// int main() {
//     return func(0x11, 0x22, 0x33, 0x44, 0x55);
// }

int func(uint64 a, uint64 b, uint64 c, uint64 d, uint64 e, uint64 f, uint64 g)
{
    int i = a + b - c + d - e + f - g;
    return i;
}

int main()
{
    return func(0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77);
}