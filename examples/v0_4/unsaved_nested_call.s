.text
.global _start
_start:
    movz x0, #0
    bl first
    hlt #0

first:
    add x0, x0, #1
    bl second
    add x0, x0, #1
    ret

second:
    add x0, x0, #1
    ret
