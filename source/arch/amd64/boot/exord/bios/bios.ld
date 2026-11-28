ENTRY(exord_bios_main)
SECTIONS { . = 0x100000;
  .text : { *(.text.entry) *(.text*) }
  .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }
