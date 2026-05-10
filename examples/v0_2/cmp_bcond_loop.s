.text
.global _start

_start:
    movz x0, #0
    movz x1, #5

loop:
    add  x0, x0, #1
    cmp  x0, x1
    b.lt loop

    hlt  #0
