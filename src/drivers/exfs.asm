; =============================================================================
; EXFS — Sistema de archivos del exokernel [XSPEC-0002]
; =============================================================================
[BITS 64]

EXFS_MAGIC      equ 0x53465845
EXFS_SB_LBA     equ 1
EXFS_BM_LBA     equ 2
EXFS_XOBJ_LBA   equ 6
EXFS_DATA_LBA   equ 38
XOBJ_SIZE       equ 64
XOBJ_PER_SEC    equ 8
XOBJ_FREE       equ 0
XOBJ_DIR        equ 1
XOBJ_DOCUMENT   equ 3

msg_exfs_ok:      db '[EXFS] Superblock validated', 10, 0
msg_exfs_format:  db '[EXFS] Unformatted -- formatting disk...', 10, 0
msg_exfs_ready:   db '[EXFS] Ready. Root directory: |', 10, 0

global exfs_ata_read
exfs_ata_read:
    push rax
    push rbx
    push rcx
    push rdx

    mov  dx, 0x1F7
.w1:
    in   al, dx
    test al, 0x80
    jnz  .w1

    mov  rbx, rax
    mov  dx,  0x1F6
    mov  al,  0xE0
    or   al,  bh
    out  dx,  al

    mov  dx,  0x1F2
    mov  al,  1
    out  dx,  al

    mov  dx,  0x1F3
    mov  rax, rbx
    out  dx,  al
    shr  rax, 8
    mov  dx,  0x1F4
    out  dx,  al
    shr  rax, 8
    mov  dx,  0x1F5
    out  dx,  al

    mov  dx,  0x1F7
    mov  al,  0x20
    out  dx,  al

.w2:
    in   al, dx
    test al, 0x08
    jz   .w2

    cld
    mov  rcx, 256
    mov  dx,  0x1F0
    rep  insw

    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax
    ret

global exfs_ata_write
exfs_ata_write:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi

    mov  dx, 0x1F7
.w1:
    in   al, dx
    test al, 0x80
    jnz  .w1

    mov  rbx, rax
    mov  dx,  0x1F6
    mov  al,  0xE0
    out  dx,  al

    mov  dx,  0x1F2
    mov  al,  1
    out  dx,  al
    mov  dx,  0x1F3
    mov  rax, rbx
    out  dx,  al
    shr  rax, 8
    mov  dx,  0x1F4
    out  dx,  al
    shr  rax, 8
    mov  dx,  0x1F5
    out  dx,  al

    mov  dx,  0x1F7
    mov  al,  0x30
    out  dx,  al

.w2:
    in   al, dx
    test al, 0x08
    jz   .w2

    cld
    mov  rcx, 256
    mov  dx,  0x1F0
    rep  outsw

    mov  dx,  0x1F7
    mov  al,  0xE7
    out  dx,  al
.w3:
    in   al, dx
    test al, 0x80
    jnz  .w3

    pop  rsi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax
    ret

global exfs_init
exfs_init:
    push rax
    push rbx
    push rdi
    push rsi

    lea  rdi, [rel exfs_io_buf]
    mov  eax, EXFS_SB_LBA
    call exfs_ata_read

    mov  eax, [exfs_io_buf]
    cmp  eax, EXFS_MAGIC
    je   .ok

    mov  rsi, msg_exfs_format
    mov  bl,  0x0E
    call xk_print
    call exfs_format_disk
    jmp  .done

.ok:
    mov  rsi, msg_exfs_ok
    mov  bl,  0x0A
    call xk_print

.done:
    mov  qword [exfs_cur_dir_lba], EXFS_DATA_LBA
    mov  rsi, msg_exfs_ready
    mov  bl,  0x0A
    call xk_print

    pop  rsi
    pop  rdi
    pop  rbx
    pop  rax
    ret

exfs_format_disk:
    push rax
    push rbx
    push rcx
    push rdi
    push rsi

    lea  rdi, [rel exfs_io_buf]
    xor  rax, rax
    mov  rcx, 512/8
    rep  stosq

    mov  dword [exfs_io_buf + 0x00], EXFS_MAGIC
    mov  word  [exfs_io_buf + 0x04], 1
    mov  word  [exfs_io_buf + 0x06], 512
    mov  dword [exfs_io_buf + 0x08], 4096
    mov  dword [exfs_io_buf + 0x0C], EXFS_BM_LBA
    mov  dword [exfs_io_buf + 0x10], 4
    mov  dword [exfs_io_buf + 0x14], EXFS_XOBJ_LBA
    mov  dword [exfs_io_buf + 0x18], 32
    mov  dword [exfs_io_buf + 0x1C], EXFS_DATA_LBA

    mov  eax, EXFS_SB_LBA
    lea  rsi, [rel exfs_io_buf]
    call exfs_ata_write

    lea  rdi, [rel exfs_io_buf]
    xor  rax, rax
    mov  rcx, 512/8
    rep  stosq

    mov  rbx, EXFS_XOBJ_LBA
    mov  rcx, 32
.cls:
    mov  eax, ebx
    lea  rsi, [rel exfs_io_buf]
    call exfs_ata_write
    inc  rbx
    loop .cls

    lea  rdi, [rel exfs_io_buf]
    xor  rax, rax
    mov  rcx, 512/8
    rep  stosq

    mov  byte  [exfs_io_buf + 0x00], XOBJ_DIR
    mov  byte  [exfs_io_buf + 0x01], 0
    mov  byte  [exfs_io_buf + 0x04], '|'
    mov  byte  [exfs_io_buf + 0x05], 0
    mov  dword [exfs_io_buf + 0x24], 0
    mov  dword [exfs_io_buf + 0x28], EXFS_DATA_LBA
    mov  dword [exfs_io_buf + 0x2C], EXFS_DATA_LBA

    mov  eax, EXFS_XOBJ_LBA
    lea  rsi, [rel exfs_io_buf]
    call exfs_ata_write

    pop  rsi
    pop  rdi
    pop  rcx
    pop  rbx
    pop  rax
    ret

; exfs_find_obj — RSI=nombre, RCX=tipo (0=cualquiera)
; RAX=indice global (-1 si no existe), RBX=LBA sector, RDX=indice en sector
global exfs_find_obj
exfs_find_obj:
    push r8
    push r9
    push r10
    push r11
    push rdi

    mov  r8,  rsi
    mov  r9,  rcx
    xor  r10, r10

.sec:
    cmp  r10, 32
    jge  .notfound

    mov  eax, r10d
    add  eax, EXFS_XOBJ_LBA
    lea  rdi, [rel exfs_io_buf]
    call exfs_ata_read

    xor  r11, r11
.obj:
    cmp  r11, XOBJ_PER_SEC
    jge  .sec_next

    mov  rax, r11
    shl  rax, 6
    lea  rdi, [rel exfs_io_buf]
    add  rdi, rax

    mov  al, [rdi]
    test al, al
    jz   .skip

    mov  eax, [rdi + 0x2C]
    cmp  rax, [exfs_cur_dir_lba]
    jne  .skip

    test r9, r9
    jz   .chkname
    mov  al, [rdi]
    cmp  al, r9b
    jne  .skip

.chkname:
    lea  rsi, [rdi + 0x04]
    mov  rdi, r8
    call xk_strcmp
    test rax, rax
    jnz  .skip

    mov  rax, r10
    imul rax, XOBJ_PER_SEC
    add  rax, r11
    mov  rbx, r10
    add  rbx, EXFS_XOBJ_LBA
    mov  rdx, r11

    pop  rdi
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    ret

.skip:
    inc  r11
    jmp  .obj

.sec_next:
    inc  r10
    jmp  .sec

.notfound:
    mov  rax, -1
    pop  rdi
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    ret

exfs_alloc_slot:
    push rbx
    push rcx
    push rdi

    xor  rbx, rbx
.sec:
    cmp  rbx, 32
    jge  .full

    mov  eax, ebx
    add  eax, EXFS_XOBJ_LBA
    lea  rdi, [rel exfs_io_buf]
    call exfs_ata_read

    xor  rcx, rcx
.obj:
    cmp  rcx, XOBJ_PER_SEC
    jge  .sec_next
    mov  rax, rcx
    shl  rax, 6
    lea  rdi, [rel exfs_io_buf]
    add  rdi, rax
    cmp  byte [rdi], 0
    je   .found
    inc  rcx
    jmp  .obj

.found:
    mov  rax, rbx
    add  rax, EXFS_XOBJ_LBA
    mov  rdx, rcx
    clc
    pop  rdi
    pop  rcx
    pop  rbx
    ret

.sec_next:
    inc  rbx
    jmp  .sec

.full:
    stc
    pop  rdi
    pop  rcx
    pop  rbx
    ret

exfs_alloc_block:
    push rbx
    push rcx
    push rdi

    mov  eax, EXFS_DATA_LBA
    xor  rbx, rbx
.sec:
    cmp  rbx, 32
    jge  .done

    push rax
    mov  eax, ebx
    add  eax, EXFS_XOBJ_LBA
    lea  rdi, [rel exfs_io_buf]
    call exfs_ata_read
    pop  rax

    xor  rcx, rcx
.obj:
    cmp  rcx, XOBJ_PER_SEC
    jge  .sec_next
    push rdi
    mov  rdi, rcx
    shl  rdi, 6
    lea  rdi, [exfs_io_buf + rdi]
    cmp  byte [rdi], 0
    je   .skip2
    mov  edx, [rdi + 0x28]
    test edx, edx
    jz   .skip2
    cmp  eax, edx
    jg   .skip2
    mov  eax, edx
    inc  eax
.skip2:
    pop  rdi
    inc  rcx
    jmp  .obj

.sec_next:
    inc  rbx
    jmp  .sec

.done:
    pop  rdi
    pop  rcx
    pop  rbx
    ret

; exfs_make_obj — RSI=nombre, BL=tipo. RAX=0 OK, -1 sin espacio, -2 existe
global exfs_make_obj
exfs_make_obj:
    push rbx
    push rcx
    push rdx
    push rdi
    push rsi
    push r8
    push r9

    mov  r8, rsi
    movzx r9, bl

    mov  rcx, 0
    call exfs_find_obj
    cmp  rax, -1
    jne  .exists

    call exfs_alloc_slot
    jc   .nospace

    mov  ebx, eax
    lea  rdi, [rel exfs_io_buf]
    call exfs_ata_read

    mov  rax, rdx
    shl  rax, 6
    lea  rdi, [rel exfs_io_buf]
    add  rdi, rax

    push rdi
    push rcx
    mov  rcx, XOBJ_SIZE/8
    push rax
    xor  rax, rax
    rep  stosq
    pop  rax
    pop  rcx
    pop  rdi

    mov  al, r9b
    mov  [rdi], al

    lea  rdi, [rdi + 0x04]
    mov  rsi, r8
    mov  rcx, 31
    rep  movsb
    mov  byte [rdi], 0

    mov  rax, rdx
    shl  rax, 6
    lea  rdi, [rel exfs_io_buf]
    add  rdi, rax

    mov  eax, [exfs_cur_dir_lba]
    mov  [rdi + 0x2C], eax

    call exfs_alloc_block
    mov  [rdi + 0x28], eax

    mov  eax, ebx
    lea  rsi, [rel exfs_io_buf]
    call exfs_ata_write

    xor  rax, rax
    jmp  .ret

.exists:
    mov  rax, -2
    jmp  .ret
.nospace:
    mov  rax, -1
.ret:
    pop  r9
    pop  r8
    pop  rsi
    pop  rdi
    pop  rdx
    pop  rcx
    pop  rbx
    ret

global exfs_list_dir
exfs_list_dir:
    push rax
    push rbx
    push rcx
    push rdx
    push rdi
    push rsi

    xor  rbx, rbx
.sec:
    cmp  rbx, 32
    jge  .done

    mov  eax, ebx
    add  eax, EXFS_XOBJ_LBA
    lea  rdi, [rel exfs_io_buf]
    call exfs_ata_read

    xor  rcx, rcx
.obj:
    cmp  rcx, XOBJ_PER_SEC
    jge  .sec_next

    mov  rax, rcx
    shl  rax, 6
    lea  rdi, [rel exfs_io_buf]
    add  rdi, rax

    mov  al, [rdi]
    test al, al
    jz   .skip_o

    mov  edx, [rdi + 0x2C]
    cmp  rdx, [exfs_cur_dir_lba]
    jne  .skip_o

    mov  al, [rdi]
    cmp  al, XOBJ_DIR
    je   .is_dir

    mov  bl, 0x0F
    jmp  .print_it

.is_dir:
    push rax
    mov  al, '|'
    mov  bl, 0x0B
    call xk_putchar
    pop  rax
    mov  bl, 0x0B

.print_it:
    push rax
    lea  rsi, [rdi + 0x04]
    call xk_print
    pop  rax

    cmp  al, XOBJ_DIR
    jne  .no_pipe
    push rax
    mov  al, '|'
    mov  bl, 0x0B
    call xk_putchar
    pop  rax

.no_pipe:
    push rax
    mov  al, 10
    mov  bl, 0x07
    call xk_putchar
    pop  rax

.skip_o:
    inc  rcx
    jmp  .obj

.sec_next:
    inc  rbx
    jmp  .sec

.done:
    pop  rsi
    pop  rdi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax
    ret

global exfs_read_obj_data
exfs_read_obj_data:
    push rbx
    push rcx
    push rdx
    push rdi
    push rsi
    push r12

    mov  r12, rdi
    xor  rcx, rcx
    call exfs_find_obj
    cmp  rax, -1
    je   .notfound

    mov  eax, ebx
    lea  rdi, [rel exfs_io_buf]
    call exfs_ata_read

    mov  rax, rdx
    shl  rax, 6
    lea  rsi, [rel exfs_io_buf]
    add  rsi, rax

    mov  eax, [rsi + 0x28]
    mov  ecx, [rsi + 0x24]

    mov  rdi, r12
    call exfs_ata_read

    mov  rax, rcx
    jmp  .done

.notfound:
    mov  rax, -1
.done:
    pop  r12
    pop  rsi
    pop  rdi
    pop  rdx
    pop  rcx
    pop  rbx
    ret

global exfs_write_obj_data
exfs_write_obj_data:
    push rbx
    push rcx
    push rdx
    push rdi
    push rsi
    push r12
    push r13

    mov  r12, rdi
    mov  r13, rcx

    xor  rcx, rcx
    call exfs_find_obj
    cmp  rax, -1
    je   .notfound

    mov  eax, ebx
    lea  rdi, [rel exfs_io_buf]
    call exfs_ata_read

    mov  rax, rdx
    shl  rax, 6
    lea  rdi, [rel exfs_io_buf]
    add  rdi, rax

    mov  eax, [rdi + 0x28]
    mov  [rdi + 0x24], r13d

    push rax
    mov  eax, ebx
    lea  rsi, [rel exfs_io_buf]
    call exfs_ata_write
    pop  rax

    mov  rsi, r12
    call exfs_ata_write

    xor  rax, rax
    jmp  .done

.notfound:
    mov  rax, -1
.done:
    pop  r13
    pop  r12
    pop  rsi
    pop  rdi
    pop  rdx
    pop  rcx
    pop  rbx
    ret

; exfs_delete_obj — RSI=nombre. RAX=0 OK, -1 no existe.
; Unico y generico: no distingue archivo vs directorio, como se pidio.
global exfs_delete_obj
exfs_delete_obj:
    push rbx
    push rcx
    push rdx
    push rdi

    xor  rcx, rcx
    call exfs_find_obj
    cmp  rax, -1
    je   .notfound

    mov  eax, ebx
    lea  rdi, [rel exfs_io_buf]
    call exfs_ata_read

    mov  rax, rdx
    shl  rax, 6
    lea  rdi, [rel exfs_io_buf]
    add  rdi, rax
    mov  byte [rdi], 0

    mov  eax, ebx
    lea  rsi, [rel exfs_io_buf]
    call exfs_ata_write

    xor  rax, rax
    jmp  .done

.notfound:
    mov  rax, -1
.done:
    pop  rdi
    pop  rdx
    pop  rcx
    pop  rbx
    ret
