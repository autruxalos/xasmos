; =============================================================================
; remove <name> — deletes an object, file or directory
; No distinction -dir/-file: EXFS does not differentiate deletion by type.
; [modular XSH command]
; =============================================================================
[BITS 64]

xsh_cmd_remove:
    cmp  qword [xsh_argc], 2
    jl   .noarg
    ARG 1
    call exfs_delete_obj
    test rax, rax
    jz   .ok
    mov  rsi, msg_err_notfound
    mov  bl,  0x0C
    call xk_print
    ret
.ok:
    mov  rsi, msg_removed
    mov  bl,  0x0A
    call xk_print
    ret
.noarg:
    mov  rsi, msg_missing_arg
    mov  bl,  0x0C
    call xk_print
    ret
