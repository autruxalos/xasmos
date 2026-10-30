/* xent.ld -- link script for .exb user programs.
 * Everything lives in user space, page-aligned per section kind,
 * nothing below 0x8000000000 (EXB_VA_MIN). */
ENTRY(_start)
SECTIONS {
    . = 0x8000000000;
    .text   : { *(.text*) }
    . = ALIGN(0x1000);
    .rodata : { *(.rodata*) }
    . = ALIGN(0x1000);
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) *(.gnu*) }
}
