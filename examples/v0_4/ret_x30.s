.text
.global _start
_start:
    movz x0, #1
    bl set_x1
    movz x0, #2
    hlt #0

set_x1:
    movz x1, #3
    ret x30
