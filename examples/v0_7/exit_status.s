.text
.global _start

_start:
    movz x0, #7              // exit status
    movz x8, #93             // fake exit
    svc  #0
