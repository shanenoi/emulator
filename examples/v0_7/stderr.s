.text
.global _start

_start:
    movz x0, #2              // fd = stderr
    movz x1, #0x1020         // buffer address, loaded as raw .text at 0x1000
    movz x2, #13             // length
    movz x8, #64             // fake write
    svc  #0
    movz x0, #0              // exit status
    movz x8, #93             // fake exit
    svc  #0

message:
    .ascii "error, v0.7!\n"
