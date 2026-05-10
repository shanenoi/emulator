.text
movz x0, #42
str  x0, [sp, #-8]!
ldr  x1, [sp], #8
hlt  #0
