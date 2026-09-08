[BITS 64]
global xsh_cmd_sprusr
xsh_cmd_sprusr:
    mov byte [xsh_is_sprusr], 1
    mov rsi, .msg
    mov bl, 0x0C
    call xk_print
    ret
.msg db 'sprusr mode enabled', 10, 0
