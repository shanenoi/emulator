.text
.global _start
_start:
    movz x0, #5
    movz x1, #0
loop:
    add x1, x1, #1
    sub x0, x0, #1
    cbnz x0, loop
    hlt #0
