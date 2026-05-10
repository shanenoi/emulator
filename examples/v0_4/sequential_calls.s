.text
.global _start
_start:
    // x0 counts how many times inc has been called.
    movz x0, #0

    // Each BL writes a fresh return address into x30.
    // This is safe because each call returns before the next call starts.
    bl inc
    bl inc
    bl inc

    hlt #0

inc:
    add x0, x0, #1
    ret
