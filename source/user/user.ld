ENTRY(_start)
PHDRS { rx PT_LOAD FLAGS(5); rw PT_LOAD FLAGS(6); }
SECTIONS {
  rx 0x8000000000 : { *(.text*) *(.rodata*) } :rx
  rw 0x8000010000 : { *(.data*) *(.bss*) *(COMMON) } :rw
  /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
