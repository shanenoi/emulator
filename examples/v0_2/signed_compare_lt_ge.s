.text
.global _start
_start:
    movz x0, #4
    cmp x0, #5
    b.lt less
    movz x1, #99
less:
    movz x2, #1
    cmp x2, #1
    b.ge done
    movz x3, #99
done:
    hlt #0
