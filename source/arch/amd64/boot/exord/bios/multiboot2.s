.code32
.section .multiboot
.align 4
.long 0x1BADB002                      /* magic Multiboot v1 real */
.long 0x00000003                      /* flags: alinear módulos + info de memoria */
.long -(0x1BADB002 + 0x00000003)      /* checksum */

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
    .space 16384
stack_top:
