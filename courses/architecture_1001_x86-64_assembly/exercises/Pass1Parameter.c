/* gcc -fno-stack-protector ArrayLocalVariable.c */
/* gdb ./a.out -q -x gdbCfg.cfg */
int func(int a){
    int i = a;
    return i;
}

int main(){
    return func(0x11);
}