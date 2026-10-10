; =============================================================================
; XASMOS — Multiboot2 Minimal Header (Assembly Stub)
; =============================================================================
bits 32

section .multiboot
header_start:
    dd 0xe85250d6                ; Magic number (Multiboot 2 specification)
    dd 0                         ; Architecture (0 = 32-bit protected mode)
    dd header_end - header_start ; Header length
    dd -(0xe85250d6 + 0 + (header_end - header_start)) ; Checksum
    
    ; Multiboot end tag obligatorio
    dw 0
    dw 0
    dd 8
header_end:

section .text
global _start
extern kernel_main

_start:
    cli                          ; Deshabilitar interrupciones
    mov esp, stack_top           ; Configurar la pila segura en memoria
    
    call kernel_main             ; Salto definitivo al código en C del Mainframe

.halt:
    hlt
    jmp .halt

section .bss
align 16
stack_bottom:
    resb 16384                   ; 16 KB de pila asignada para el núcleo
stack_top:
