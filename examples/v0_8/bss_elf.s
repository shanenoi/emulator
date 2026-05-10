.text
.global _start

_start:
    movz x0, #1              // fd = stdout
    movz x1, #0x2000         // zero-filled .bss buffer
    movz x2, #1              // write one NUL byte
    movz x8, #64             // fake write
    svc  #0
    movz x0, #0
    movz x8, #93
    svc  #0

.bss
zero_byte:
    .skip 1
