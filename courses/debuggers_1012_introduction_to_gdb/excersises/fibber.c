// fibber.c: A simple recursive Fibbonacci sequence calculation program
// Compile it with:

// gcc -ggdb fibber.c -o fibber_bin

// The “-ggdb” option adds GDB debugging symbols to the binary. Confirm it executes by running “./fibber_bin” (without quotes).
#include <stdio.h>

unsigned int fibbonacci(unsigned int n)
{
    if (n <= 1)
    {
        return n;
    }
    else
    {
        return (fibbonacci(n - 1) + fibbonacci(n - 2));
    }
}

int main()
{
    unsigned int n = 10;

    printf("First %d elements of the Fibbonacci sequence: ", n);

    for (unsigned int i = 0; i < n; i++)
    {
        printf("%d ", fibbonacci(i));
    }
    printf("\n");
    return 0;
}