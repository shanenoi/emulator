.text
movz x0, #1
movz x1, #2
str  x0, [sp, #-8]!
str  x1, [sp, #-8]!
ldr  x2, [sp], #8
ldr  x3, [sp], #8
hlt  #0
