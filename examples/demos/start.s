.text
.global _start
.extern main
_start:
    bl main
    movz x8, #93
    svc #0
