/* =============================================================================
 * XASMOS — Multiboot2 Minimal Header (LLVM-MC / GAS Syntax)
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
