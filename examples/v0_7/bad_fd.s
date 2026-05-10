.text
.global _start

_start:
    movz x0, #9              // unsupported fd
    movz x1, #0x1024         // valid but unused buffer address for bad fd
    movz x2, #1
    movz x8, #64             // fake write returns -EBADF in x0
    svc  #0
    movz x8, #93             // exit with low 8 bits of x0 so the error is observable
    svc  #0

byte:
    .byte 0x21
