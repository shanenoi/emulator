.text
.global _start
_start:
    movz x0, #7
    cmp x0, #7
    b.eq equal
    movz x1, #99
equal:
    movz x2, #1
    hlt #0
