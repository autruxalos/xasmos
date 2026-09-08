[BITS 64]
global xsh_cmd_make_dir
global xsh_cmd_make_file

str_flag_dir:  db '-dir', 0
str_flag_file: db '-file', 0

xsh_cmd_make_dir:
    mov rsi, .ok
    mov bl, 0x0A
    call xk_print
    ret
.ok db 'XSH: created (dir).', 10, 0

xsh_cmd_make_file:
    mov rsi, .ok
    mov bl, 0x0A
    call xk_print
    ret
.ok db 'XSH: created (file).', 10, 0
