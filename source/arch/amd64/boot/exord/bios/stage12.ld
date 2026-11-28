ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(7); }
SECTIONS { . = 0x7C00;
  .boot : { KEEP(*(.boot)) *(.boot.text) } :text
  . = 0x7DFE; .sig : { KEEP(*(.sig)) } :text
  . = 0x7E00; .text : { *(.text*) *(.rodata*) *(.data*) *(.bss*) } :text
  . = 0x9E00; }
