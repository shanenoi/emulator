.text
.global _start

_start:
    movz x0, #1              // fd = stdout
    movz x1, #0x2000         // message lives in the ELF data segment
    movz x2, #13             // length
    movz x8, #64             // fake write
    svc  #0
    movz x0, #0              // exit status
    movz x8, #93             // fake exit
    svc  #0

.data
message:
    .ascii "hello, v0.8!\n"
