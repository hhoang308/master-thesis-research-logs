// echo.c: A simple program to take input on the command line and echo it back out
// Compile it with:

// gcc -ggdb echo.c -o echo_bin

// Confirm it executes by running “./echo_bin yo” (without quotes).
#include <stdio.h>
void main(int argc, char **argv)
{
    if (argv[1] != NULL && argv[1] != "")
    {
        printf("You entered %s for argv[1]\n", argv[1]);
    }
    else
    {
        printf("You didn't enter an argv[1]\n");
    }
}