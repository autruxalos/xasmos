/* =============================================================================
 * XASMOS — Multiboot2 Minimal Header & PVH Note (LLVM-MC / GAS Syntax)
 * Path: source|arch|amd64|boot|multiboot2.s
 * ============================================================================= */
.code32

.section .multiboot, "a"
header_start:
    .long 0xe85250d6                /* Magic number */
    .long 0                         /* Architecture 0 (32-bit protected mode) */
    .long header_end - header_start /* Header length */
    .long -(0xe85250d6 + 0 + (header_end - header_start)) /* Checksum */
    
    /* Multiboot end tag */
    .short 0
    .short 0
    .long 8
header_end:

/* PVH Note to allow direct loading via modern QEMU -kernel */
.section .note.Xen, "a", @note
    .long 4             /* Name size ("Xen" + null terminator) */
    .long 8             /* Descriptor size (64-bit entry address) */
    .long 18            /* Note type: XEN_ELFNOTE_PHYS32_ENTRY */
    .asciz "Xen"        /* Vendor name */
    .align 4
    .quad _start        /* Kernel entry point address */
    .align 4

.section .text
.global _start
.extern kernel_main

_start:
    cli
    movl $stack_top, %esp
    call kernel_main

.halt:
    hlt
    jmp .halt

.section .bss
.align 16
stack_bottom:
    .space 16384                    /* 16 KB stack */
stack_top:
