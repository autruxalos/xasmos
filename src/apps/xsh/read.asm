; =============================================================================
; cat <name> — displays file contents [modular XSH command]
; =============================================================================
[BITS 64]

xsh_cmd_read:
    cmp  qword [xsh_argc], 2
    jl   .noarg
    ARG 1
    push rsi
    mov  rsi, msg_read_start
    mov  bl,  0x0E
    call xk_print
    pop  rsi

    lea  rdi, [rel read_databuf]
    call exfs_read_obj_data
    cmp  rax, -1
    je   .notfound

    lea  rsi, [rel read_databuf]
    mov  byte [rsi + rax], 0
    mov  bl,  0x0F
    call xk_print

    mov  rsi, msg_read_end
    mov  bl,  0x0E
    call xk_print
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
