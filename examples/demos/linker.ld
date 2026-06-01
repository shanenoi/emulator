ENTRY(_start)
SECTIONS
{
  . = 0x1000;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  . = 0x3000;
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
}
