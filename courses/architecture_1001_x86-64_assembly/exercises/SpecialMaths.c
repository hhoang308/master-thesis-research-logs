#include <stdlib.h>

int main(int argc, char **argv)
{
    int a;
    a = atoi(argv[1]);
    return a * argc + a;
}