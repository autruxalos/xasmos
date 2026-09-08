[BITS 64]
global xsh_cmd_remove
xsh_cmd_remove:
    mov rsi, .ok
    mov bl, 0x0A
    call xk_print
    ret
.ok db 'XSH: removed.', 10, 0
