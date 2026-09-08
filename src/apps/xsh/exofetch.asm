[BITS 64]
global xsh_cmd_exofetch
xsh_cmd_exofetch:
    mov rsi, .msg
    mov bl, 0x0B
    call xk_print
    ret
.msg db 'exofetch: stub', 10, 0
