.code16
.section .text
.global _boot16
_boot16:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp

    /* Enable A20 fast gate */
    inb $0x92, %al
    orb $0x02, %al
    outb %al, $0x92

    /* Load 32 sectors (16 KiB) from sector 2 to 0x1000:0x0000 (phys 0x10000) */
    movw $0x1000, %ax
    movw %ax, %es
    xorw %bx, %bx
    movb $0x02, %ah
    movb $16, %al
    movb $0x00, %ch
    movb $0x02, %cl
    movb $0x00, %dh
    movb $0x80, %dl
    int $0x13

    lgdt gdt_desc
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmp $0x08, $pm32

.align 8
gdt_start:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt_end:
gdt_desc:
    .word gdt_end - gdt_start - 1
    .long gdt_start

.code32
pm32:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl $0x90000, %esp
    movw $0x0F58, 0xB8000
    movw $0x0F58, 0xB8000
    ljmp $0x08, $0x10000
