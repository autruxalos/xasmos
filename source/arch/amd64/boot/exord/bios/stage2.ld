ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.startup*) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
}
