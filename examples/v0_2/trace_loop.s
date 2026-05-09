.text
.global _start
_start:
    movz x0, #3
loop:
    sub x0, x0, #1
    cbnz x0, loop
    hlt #0
