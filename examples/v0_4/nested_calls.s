.text
.global _start
_start:
    movz x0, #0
    bl first
    hlt #0

first:
    stp x29, x30, [sp, #-16]!
    add x0, x0, #1
    bl second
    add x0, x0, #1
    ldp x29, x30, [sp], #16
    ret

second:
    add x0, x0, #1
    ret
