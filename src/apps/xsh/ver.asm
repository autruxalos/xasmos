[BITS 64]
global xsh_cmd_ver
xsh_cmd_ver:
    mov rsi, .msg
    mov bl, 0x0B
    call xk_print
    ret
.msg db 'xasmos / Wayward Kernel', 10, 0
