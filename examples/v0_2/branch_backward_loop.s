.text
.global _start

_start:
    movz x0, #3

loop:
    sub  x0, x0, #1
    cbz  x0, done
    b    loop

done:
    hlt  #0