/* =============================================================================
 * XASMOS — 64-bit PVH & Long Mode Ignition Stub (LLVM-MC / GAS Syntax)
 * Path: source|arch|amd64|boot|multiboot2.s
 * ============================================================================= */

.code64
.section .text
.global _start
.extern kernel_main

/* 
 * Xen PVH Note: Tells modern QEMU -kernel that this binary supports 
 * direct 64-bit long-mode handoff with paging already enabled.
 */
.section .note.Xen, "a", @note
    .long 4             /* Name size ("Xen" + null terminator) */
    .long 8             /* Descriptor size (64-bit entry address) */
    .long 0             /* Note type: XEN_ELFNOTE_ENTRY (64-bit entry point) */
    .asciz "Xen"        /* Vendor name string */
    .align 4
    .quad _start        /* 64-bit virtual entry point address */
    .align 4

_start:
    cli

    /* Set up stack pointer using RIP-relative addressing for 64-bit mode */
    lea stack_top(%rip), %rsp

    /* Align stack to 16 bytes as required by System V AMD64 ABI */
    andq $-16, %rsp

    /* Jump directly into the C exokernel core */
    call kernel_main

.halt:
    hlt
    jmp .halt

.section .bss
.align 16
stack_bottom:
    .space 16384                    /* 16 KB stack allocation */
stack_top:
