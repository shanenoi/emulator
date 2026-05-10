.text
.global _start
_start:
    movz x0, #0x100c
    b helper
    movz x1, #0xdead
    hlt #0

helper:
    movz x2, #7
    ret x0
