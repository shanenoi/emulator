    .text
    .global _start
_start:
    movz x0, #10
    sub  x1, x0, #4
    hlt  #0