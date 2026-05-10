.text
.global _start
_start:
    movz x0, #0
    movz x29, #0x1234
    bl framed
    hlt #0

framed:
    stp x29, x30, [sp, #-16]!
    movz x29, #0xabcd
    add x0, x0, #1
    ldp x29, x30, [sp], #16
    ret
