.text
movz x0, #100
movz x1, #200
stp  x0, x1, [sp, #-16]!
ldp  x2, x3, [sp], #16
hlt  #0
