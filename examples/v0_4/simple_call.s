.text
.global _start
_start:
    movz x0, #2
    bl add_three
    hlt #0

add_three:
    add x0, x0, #3
    ret
