; =============================================================================
; make -dir <name> / make -file <name> — creates a directory or file
; Single command with a flag, no separate mkdir/touch identity.
; [modular XSH command]
; =============================================================================
[BITS 64]

msg_make_badflag: db "XSH: make needs '-dir' or '-file' as first argument.", 10, 0

xsh_cmd_make:
    cmp  qword [xsh_argc], 3
    jl   .noarg

    ARG 1
    mov  rdi, str_flag_dir
    call xk_strcmp
    test rax, rax
    jz   .is_dir

    ARG 1
    mov  rdi, str_flag_file
    call xk_strcmp
    test rax, rax
    jz   .is_file

    mov  rsi, msg_make_badflag
    mov  bl,  0x0C
    call xk_print
    ret

.is_dir:
    ARG 2
    mov  bl, XOBJ_DIR
    call exfs_make_obj
    jmp  .report

.is_file:
    ARG 2
    mov  bl, XOBJ_DOCUMENT
    call exfs_make_obj

.report:
    test rax, rax
    jz   .ok
    cmp  rax, -2
    je   .exists
    mov  rsi, msg_err_nospace
    mov  bl,  0x0C
    call xk_print
    ret
.ok:
    mov  rsi, msg_created
    mov  bl,  0x0A
    call xk_print
    ret
.exists:
    mov  rsi, msg_err_exists
    mov  bl,  0x0E
    call xk_print
    ret
.noarg:
    mov  rsi, msg_missing_arg
    mov  bl,  0x0C
    call xk_print
    ret
