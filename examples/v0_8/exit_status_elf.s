.text
.global _start

_start:
    movz x0, #8              // exit status
    movz x8, #93             // fake exit
    svc  #0
