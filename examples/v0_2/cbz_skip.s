.text
.global _start
_start:
    movz x0, #0
    cbz x0, target
    movz x1, #99
target:
    hlt #0
