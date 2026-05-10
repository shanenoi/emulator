/*
 * This file is intentionally not built by `make examples`.
 * It shows the kind of hosted/libc C program that v0.9 does not support.
 * A normal build of this file would require libc startup, printf, dynamic
 * linking or extra runtime objects, which remain outside the emulator scope.
 */
#include <stdio.h>

int main(void) {
    printf("hello from hosted C\n");
    return 0;
}
