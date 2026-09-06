; =============================================================================
; pwd — displays current directory [modular XSH command]
; =============================================================================
[BITS 64]

xsh_cmd_whereami:
    mov  bl, 0x0B
    mov  rsi, msg_pipe
    call xk_print
    lea  rsi, [rel xsh_cwd_name]
    call xk_print
    mov  rsi, msg_pipe
    call xk_println
    ret
