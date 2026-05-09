.text
.global _start
_start:
    movz x0, #1
    b target
    movz x0, #99
target:
    hlt #0
