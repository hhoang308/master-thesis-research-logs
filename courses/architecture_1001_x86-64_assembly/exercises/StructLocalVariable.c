/* gcc -fno-stack-protector ArrayLocalVariable.c */
/* gdb ./a.out -q -x gdbCfg.cfg */
// #pragma pack(1)
typedef struct mystruct
{
    short a;
    int b[6];
    long long c;
} mystruct_t;
// #pragma

short main()
{
    mystruct_t foo;
    foo.a = 0xbabe;
    foo.c = 0xba1b0ab1edb100d;
    foo.b[0] = 0xabcd;
    foo.b[1] = 0xcdef;
    foo.b[2] = 0x1234;
    foo.b[3] = 0x5678;
    foo.b[4] = 0x12ab;
    foo.b[5] = 0x34cd;
    return foo.b[4];
}