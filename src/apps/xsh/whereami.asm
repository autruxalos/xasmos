[BITS 64]
global xsh_cmd_whereami
xsh_cmd_whereami:
    mov bl, 0x0B
    mov rsi, .pipe
    call xk_print
    lea rsi, [rel xsh_cwd_name]
    call xk_print
    mov rsi, .pipe
    call xk_println
    ret
.pipe db '|', 0
