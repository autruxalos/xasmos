; =============================================================================
; XKERNEL — XASMOS Exokernel Core [XSPEC-0004]
; x86-64, loaded by XBOOT at 0x9000
; =============================================================================
[BITS 16]
org 0x9000

kernel_16_entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x8000

    mov si, msg_16
    call print16

    lgdt [gdt32_ptr]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:kernel_32_entry

print16:
    mov ah, 0x0E
    mov bx, 0x0007
.lp:
    lodsb
    or  al, al
    jz  .ret
    int 0x10
    jmp .lp
.ret:
    ret

msg_16 db 'XKERNEL 16-bit OK', 13, 10, 0

; (unchanged - 16-bit init message is self-explanatory)

align 8
gdt32_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt32_end:
gdt32_ptr:
    dw gdt32_end - gdt32_start - 1
    dd gdt32_start

align 8
gdt64_start:
    dq 0x0000000000000000
    dq 0x00209A0000000000
    dq 0x0000920000000000
gdt64_end:
gdt64_ptr:
    dw gdt64_end - gdt64_start - 1
    dd gdt64_start

align 16
times 512  db 0
stack_top_32:

align 16
times 2048 db 0
stack_top_64:

[BITS 32]
kernel_32_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, stack_top_32

    mov eax, cr4
    or  eax, (1 << 5)
    mov cr4, eax

    mov edi, 0x1000
    xor eax, eax
    mov ecx, 0x3000 / 4
    rep stosd
    mov dword [0x1000], 0x2003
    mov dword [0x2000], 0x3003
    mov dword [0x3000], 0x0083
    mov eax, 0x1000
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8)
    wrmsr

    lgdt [gdt64_ptr]
    mov eax, cr0
    or  eax, 0x80000001
    mov cr0, eax

    jmp 0x08:kernel_64_entry

[BITS 64]
kernel_64_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, stack_top_64
    cld                       ; asegurar DF=0 para todas las rep string ops

    call xk_init_video
    call exit_main_executor

.halt:
    cli
    hlt
    jmp .halt

; =============================================================================
; VARIABLES GLOBALES DEL KERNEL
; =============================================================================
global cursor_pos
global readline_buf
global exfs_cur_dir_name
global exfs_cur_dir_lba
global exfs_io_buf

cursor_pos:         dw 0
readline_buf:       times 256 db 0
exfs_cur_dir_name:  db '|', 0
                     times 126 db 0
exfs_cur_dir_lba:   dq 0
exfs_io_buf:        times 512 db 0

VGA_BASE equ 0xB8000
VGA_COLS equ 80
VGA_ROWS equ 25

global xk_init_video
xk_init_video:
    push rdi
    push rcx
    push rax
    mov  rdi, VGA_BASE
    mov  rcx, VGA_COLS * VGA_ROWS
    mov  ax,  0x0720
    rep  stosw
    mov  word [cursor_pos], 0
    pop  rax
    pop  rcx
    pop  rdi
    ret

global xk_init_keyboard
xk_init_keyboard:
    push rax
.flush:
    in   al, 0x64
    test al, 1
    jz   .done
    in   al, 0x60
    jmp  .flush
.done:
    pop  rax
    ret

global xk_scroll
xk_scroll:
    push rsi
    push rdi
    push rcx
    cld
    mov  rsi, VGA_BASE + VGA_COLS * 2
    mov  rdi, VGA_BASE
    mov  rcx, VGA_COLS * (VGA_ROWS - 1)
    rep  movsw
    mov  rdi, VGA_BASE + VGA_COLS * (VGA_ROWS - 1) * 2
    mov  rcx, VGA_COLS
    mov  ax,  0x0720
    rep  stosw
    mov  word [cursor_pos], VGA_COLS * (VGA_ROWS - 1)
    pop  rcx
    pop  rdi
    pop  rsi
    ret

global xk_putchar
xk_putchar:
    push rax
    push rbx
    push rcx
    push rdi

    cmp al, 10
    je  .newline
    cmp al, 13
    je  .cr
    cmp al, 8
    je  .backspace

    movzx rcx, word [cursor_pos]
    cmp   rcx, VGA_COLS * VGA_ROWS
    jl    .write
    call  xk_scroll
    movzx rcx, word [cursor_pos]
.write:
    shl  rcx, 1
    add  rcx, VGA_BASE
    mov  ah, bl
    mov  word [rcx], ax
    inc  word [cursor_pos]
    jmp  .done

.newline:
    movzx rax, word [cursor_pos]
    xor   rdx, rdx
    mov   rcx, VGA_COLS
    div   rcx
    inc   rax
    imul  rax, VGA_COLS
    cmp   rax, VGA_COLS * VGA_ROWS
    jl    .setpos
    call  xk_scroll
    jmp   .done
.setpos:
    mov   word [cursor_pos], ax
    jmp   .done

.cr:
    movzx rax, word [cursor_pos]
    xor   rdx, rdx
    mov   rcx, VGA_COLS
    div   rcx
    imul  rax, VGA_COLS
    mov   word [cursor_pos], ax
    jmp   .done

.backspace:
    cmp  word [cursor_pos], 0
    je   .done
    dec  word [cursor_pos]
    movzx rcx, word [cursor_pos]
    shl  rcx, 1
    add  rcx, VGA_BASE
    mov  word [rcx], 0x0720
    jmp  .done

.done:
    pop rdi
    pop rcx
    pop rbx
    pop rax
    ret

global xk_print
xk_print:
    push rax
    push rsi
.lp:
    lodsb
    test al, al
    jz   .done
    call xk_putchar
    jmp  .lp
.done:
    pop rsi
    pop rax
    ret

global xk_println
xk_println:
    call xk_print
    push rax
    push rbx
    mov  al, 10
    mov  bl, 0x07
    call xk_putchar
    pop  rbx
    pop  rax
    ret

scancode_map:
    db 0,0,'1','2','3','4','5','6','7','8','9','0','-','=',8,9
    db 'q','w','e','r','t','y','u','i','o','p','[',']',13,0
    db 'a','s','d','f','g','h','j','k','l',59,39,96,0,92
    db 'z','x','c','v','b','n','m',44,46,47,0,0,0,32
    times (256 - ($ - scancode_map)) db 0

; xk_readline — RDI=buffer, RCX=max chars. RAX=longitud, buffer null-terminado
global xk_readline
xk_readline:
    push rbx
    push rdx
    push rdi
    push rcx

    xor  rdx, rdx

.rd:
    in   al, 0x64
    test al, 1
    jz   .rd
    in   al, 0x60

    cmp  al, 0x80
    jge  .rd

    push rbx
    lea  rbx, [rel scancode_map]
    movzx rax, al
    mov  al, [rbx + rax]
    pop  rbx

    test al, al
    jz   .rd

    cmp  al, 13
    je   .enter
    cmp  al, 8
    je   .bs

    cmp  rdx, rcx
    jge  .rd
    mov  [rdi], al
    inc  rdi
    inc  rdx
    mov  bl, 0x0F
    call xk_putchar
    jmp  .rd

.bs:
    test rdx, rdx
    jz   .rd
    dec  rdi
    dec  rdx
    push rax
    mov  al, 8
    mov  bl, 0x07
    call xk_putchar
    mov  al, ' '
    call xk_putchar
    mov  al, 8
    call xk_putchar
    pop  rax
    jmp  .rd

.enter:
    mov  byte [rdi], 0
    mov  rax, rdx
    push rax
    mov  al, 10
    mov  bl, 0x07
    call xk_putchar
    pop  rax

    pop  rcx
    pop  rdi
    pop  rdx
    pop  rbx
    ret

; xk_getkey — lectura cruda de una tecla (para editores/apps interactivas)
; Salida: AL = ascii traducido (0 si no mapeado), AH = scancode crudo
global xk_getkey
xk_getkey:
    push rbx
    push rdx
.rd:
    in   al, 0x64
    test al, 1
    jz   .rd
    in   al, 0x60
    cmp  al, 0x80
    jge  .rd
    mov  dl, al
    lea  rbx, [rel scancode_map]
    movzx rax, al
    mov  al, [rbx + rax]
    mov  ah, dl
    pop  rdx
    pop  rbx
    ret

global xk_strcmp
xk_strcmp:
    push rsi
    push rdi
    push rbx
.lp:
    mov  al, [rsi]
    mov  bl, [rdi]
    cmp  al, bl
    jne  .neq
    test al, al
    jz   .eq
    inc  rsi
    inc  rdi
    jmp  .lp
.eq:
    xor  rax, rax
    pop  rbx
    pop  rdi
    pop  rsi
    ret
.neq:
    mov  rax, 1
    pop  rbx
    pop  rdi
    pop  rsi
    ret

global xk_strlen
xk_strlen:
    push rsi
    xor  rax, rax
.lp:
    cmp  byte [rsi], 0
    je   .done
    inc  rsi
    inc  rax
    jmp  .lp
.done:
    pop  rsi
    ret

global xk_strncpy
xk_strncpy:
    test rcx, rcx
    jz   .done
.lp:
    mov  al, [rsi]
    mov  [rdi], al
    inc  rsi
    inc  rdi
    test al, al
    jz   .done
    dec  rcx
    jnz  .lp
.done:
    ret

; =============================================================================
; MODULOS DEL SISTEMA
; =============================================================================
%include "src/drivers/exfs.asm"
%include "src/init/exit.asm"
%include "src/apps/xsh/xsh.asm"
