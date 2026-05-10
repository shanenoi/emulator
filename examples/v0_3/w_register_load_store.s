.text
movz x0, #0xffff, lsl #16
add  x0, x0, #0x123
str  w0, [sp, #-4]!
ldr  w1, [sp], #4
hlt  #0
