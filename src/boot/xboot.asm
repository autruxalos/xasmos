; =============================================================================
; XBOOT - XASMOS Bootloader  [XSPEC-0001]
; Loads XKERNEL from sector 1 to 0x0000:0x9000 via extended LBA
; =============================================================================
[BITS 16]
[ORG 0x7C00]

xboot_main:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [BOOT_DRIVE], dl

    mov ax, 0x0003
    int 0x10

    mov si, MSG_LOADING
    call bios_print

    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc  .use_chs
    cmp bx, 0xAA55
    jne .use_chs

    mov si, MSG_LBA
    call bios_print

    push dword 0
    push dword 1
    push word  0x0000
    push word  0x9000
    push word  64
    push word  0x0010

    mov si, sp
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    add sp, 16
    jc  .error
    jmp .loaded

.use_chs:
    mov si, MSG_CHS
    call bios_print

    mov ax, 0x0000
    mov es, ax
    mov bx, 0x9000

    mov ah, 0x02
    mov al, 63
    mov ch, 0
    mov dh, 0
    mov cl, 2
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc  .error

    add bx, 63 * 512
    mov ah, 0x02
    mov al, 63
    mov ch, 0
    mov dh, 1
    mov cl, 1
    mov dl, [BOOT_DRIVE]
    int 0x13

.loaded:
    mov si, MSG_OK
    call bios_print
    jmp 0x0000:0x9000

.error:
    mov si, MSG_ERROR
    call bios_print
.halt:
    cli
    hlt
    jmp .halt

bios_print:
    mov ah, 0x0E
    mov bx, 0x0007
.loop:
    lodsb
    or  al, al
    jz  .done
    int 0x10
    jmp .loop
.done:
    ret

BOOT_DRIVE  db 0
MSG_LOADING db 'XASMOS Boot - Loading...', 13, 10, 0
MSG_LBA     db 'LBA Mode', 13, 10, 0
MSG_CHS     db 'CHS Mode', 13, 10, 0
MSG_OK      db 'Kernel loaded! Jumping...', 13, 10, 0
MSG_ERROR   db 'ERROR disk read!', 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
