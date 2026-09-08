[BITS 64]
global xsh_cmd_halt
xsh_cmd_halt:
    mov rsi, .msg
    mov bl, 0x0C
    call xk_print
    cli
    hlt
    jmp $
.msg db 10, 'xasmos halted.', 10, 0
