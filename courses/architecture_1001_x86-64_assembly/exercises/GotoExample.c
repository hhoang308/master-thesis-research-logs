#include <stdio.h>

int main()
{
    goto label;
    printf("skipped");
label:
    printf("goto ftwaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    return 0xb01dface;
}