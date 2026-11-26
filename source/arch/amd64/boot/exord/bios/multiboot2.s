.code32
.section .text.startup
.global _start
.extern kernel_main
_start:
    movl $0x90000, %esp
    pushl $0
    pushl $0x2BADB002
    call kernel_main
1:  hlt
    jmp 1b
