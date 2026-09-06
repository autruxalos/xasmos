; =============================================================================
; locate <name> — changes directory [modular XSH command]
; =============================================================================
[BITS 64]

xsh_cmd_locate:
    cmp  qword [xsh_argc], 2
    jl   .noarg

    ARG 1
    mov  rdi, str_dotdot
    call xk_strcmp
    test rax, rax
    jz   .go_parent

    ARG 1
    mov  rcx, XOBJ_DIR
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
    mov  [exfs_cur_dir_lba], rax

    lea  rsi, [rdi + 0x04]
    lea  rdi, [rel xsh_cwd_name]
    mov  rcx, 127
    call xk_strncpy
    ret

.go_parent:
    mov  qword [exfs_cur_dir_lba], EXFS_DATA_LBA
    lea  rdi, [rel xsh_cwd_name]
    mov  byte [rdi], '|'
    mov  byte [rdi+1], 0
    ret

.notfound:
    mov  rsi, msg_err_notfound
    mov  bl,  0x0C
    call xk_print
    ret
.noarg:
    mov  rsi, msg_missing_arg
    mov  bl,  0x0C
    call xk_print
    ret
