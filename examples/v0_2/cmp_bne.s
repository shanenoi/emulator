.text
.global _start
_start:
    movz x0, #4
    cmp x0, #5
    b.ne notequal
    movz x1, #99
notequal:
    movz x2, #1
    hlt #0
