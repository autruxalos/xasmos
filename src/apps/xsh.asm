; =============================================================================
; XSH — Shell del exokernel XASMOS [XSPEC-0006]
; Cada comando vive en su propio archivo /src/apps/xsh/<comando>.asm
; Este archivo solo tiene: bucle principal, parser, dispatcher y prompt.
; Prompt: |user|<usuario>|@   (usuario normal)
;         |user|<usuario>|%   (sprusr / superusuario)
; =============================================================================
[BITS 64]

XSH_BUF_LEN  equ 255
XSH_ARGC_MAX equ 8
XSH_ARG_LEN  equ 64

; -----------------------------------------------------------------------
; Estado compartido del shell (visible para todos los comandos incluidos)
; -----------------------------------------------------------------------
xsh_linebuf:      times XSH_BUF_LEN + 1 db 0
xsh_args:         times XSH_ARGC_MAX * XSH_ARG_LEN db 0
xsh_argc:         dq 0
xsh_cwd_name:     times 128 db 0
xsh_username:     times 32 db 0
xsh_is_root:      db 0               ; 0 = usuario normal, 1 = sprusr
write_databuf:    times 512 db 0
read_databuf:     times 512 db 0

default_username: db 'autruxalos', 0

; -----------------------------------------------------------------------
; Prompt: |user|<usuario>|@ o %
; -----------------------------------------------------------------------
msg_prompt_seg1:  db 'user', 0
msg_prompt_at:    db '@ ', 0
msg_prompt_pct:   db '% ', 0
msg_pipe:         db '|', 0

; Mensajes generales del shell (usados por varios comandos)
msg_unknown_cmd:  db 'XSH: comando no reconocido. Escribe "ver" para ayuda.', 10, 0
msg_missing_arg:  db 'XSH: falta argumento.', 10, 0
msg_err_exists:   db 'XSH: ya existe.', 10, 0
msg_err_notfound: db 'XSH: no encontrado.', 10, 0
msg_err_nospace:  db 'XSH: sin espacio en disco.', 10, 0
msg_created:      db 'XSH: creado.', 10, 0
msg_removed:      db 'XSH: eliminado.', 10, 0
msg_write_done:   db 10, 'XSH: escrito.', 10, 0
msg_read_start:   db 10, '--- contenido ---', 10, 0
msg_read_end:     db '--- fin ---', 10, 0
str_dotdot:       db '..', 0

; -----------------------------------------------------------------------
; Tabla de comandos — un renglon por comando modular
; -----------------------------------------------------------------------
cmd_table:
    dq str_cmd_ver,        xsh_cmd_ver
    dq str_cmd_clear,      xsh_cmd_clear
    dq str_cmd_list,       xsh_cmd_list
    dq str_cmd_make_dir,   xsh_cmd_make_dir
    dq str_cmd_make_file,  xsh_cmd_make_file
    dq str_cmd_remove,     xsh_cmd_remove
    dq str_cmd_read,       xsh_cmd_read
    dq str_cmd_write,      xsh_cmd_write
    dq str_cmd_ubicate,    xsh_cmd_ubicate
    dq str_cmd_whereami,   xsh_cmd_whereami
    dq str_cmd_sprusr,     xsh_cmd_sprusr
    dq str_cmd_exofetch,   xsh_cmd_exofetch
    dq str_cmd_halt,       xsh_cmd_halt
    dq 0, 0

str_cmd_ver:        db 'ver', 0
str_cmd_clear:      db 'clear', 0
str_cmd_list:       db 'list', 0
str_cmd_make_dir:   db 'make-dir', 0
str_cmd_make_file:  db 'make-file', 0
str_cmd_remove:     db 'remove', 0
str_cmd_read:       db 'read', 0
str_cmd_write:      db 'write', 0
str_cmd_ubicate:    db 'ubicate', 0
str_cmd_whereami:   db 'whereami', 0
str_cmd_sprusr:     db 'sprusr', 0
str_cmd_exofetch:   db 'exofetch', 0
str_cmd_halt:       db 'halt', 0

; -----------------------------------------------------------------------
; PUNTO DE ENTRADA DEL SHELL
; -----------------------------------------------------------------------
global xsh_main
xsh_main:
    lea  rdi, [rel xsh_cwd_name]
    mov  byte [rdi], '|'
    mov  byte [rdi+1], 0

    lea  rsi, [rel default_username]
    lea  rdi, [rel xsh_username]
    mov  rcx, 31
    call xk_strncpy

    mov  byte [rel xsh_is_root], 0

.loop:
    call xsh_print_prompt

    lea  rdi, [rel xsh_linebuf]
    mov  rcx, XSH_BUF_LEN
    call xk_readline

    lea  rsi, [rel xsh_linebuf]
    call xsh_parse_args

    cmp  qword [xsh_argc], 0
    je   .loop

    call xsh_dispatch
    jmp  .loop

; xsh_print_prompt — imprime |user|<usuario>|@ o %
xsh_print_prompt:
    mov  bl, 0x0B
    mov  rsi, msg_pipe
    call xk_print

    mov  rsi, msg_prompt_seg1
    call xk_print

    mov  rsi, msg_pipe
    call xk_print

    lea  rsi, [rel xsh_username]
    mov  bl,  0x0F
    call xk_print

    mov  rsi, msg_pipe
    mov  bl,  0x0B
    call xk_print

    cmp  byte [rel xsh_is_root], 0
    jne  .root
    mov  rsi, msg_prompt_at
    mov  bl,  0x0A
    jmp  .p
.root:
    mov  rsi, msg_prompt_pct
    mov  bl,  0x0C
.p:
    call xk_print
    ret

; -----------------------------------------------------------------------
; xsh_parse_args — separa xsh_linebuf en argumentos por espacios
; -----------------------------------------------------------------------
xsh_parse_args:
    push rax
    push rbx
    push rcx
    push rdi
    push rsi

    mov  qword [xsh_argc], 0
    xor  rbx, rbx

.skip_spaces:
    mov  al, [rsi]
    test al, al
    jz   .done
    cmp  al, ' '
    jne  .copy_arg
    inc  rsi
    jmp  .skip_spaces

.copy_arg:
    cmp  rbx, XSH_ARGC_MAX
    jge  .done

    lea  rdi, [rel xsh_args]
    mov  rax, rbx
    imul rax, XSH_ARG_LEN
    add  rdi, rax

    mov  rcx, XSH_ARG_LEN - 1
.copy_char:
    mov  al, [rsi]
    test al, al
    jz   .arg_done
    cmp  al, ' '
    je   .arg_done
    test rcx, rcx
    jz   .arg_done
    mov  [rdi], al
    inc  rdi
    inc  rsi
    dec  rcx
    jmp  .copy_char

.arg_done:
    mov  byte [rdi], 0
    inc  rbx
    inc  qword [xsh_argc]
    jmp  .skip_spaces

.done:
    pop  rsi
    pop  rdi
    pop  rcx
    pop  rbx
    pop  rax
    ret

%macro ARG 1
    lea rsi, [rel xsh_args + %1 * XSH_ARG_LEN]
%endmacro

; -----------------------------------------------------------------------
; xsh_dispatch — busca arg[0] en cmd_table y llama la funcion
; -----------------------------------------------------------------------
xsh_dispatch:
    push rax
    push rbx
    push rsi
    push rdi

    ARG 0
    mov  rdi, rsi

    lea  rbx, [rel cmd_table]
.search:
    mov  rax, [rbx]
    test rax, rax
    jz   .unknown

    mov  rsi, rax
    push rdi
    call xk_strcmp
    pop  rdi
    test rax, rax
    jz   .found

    add  rbx, 16
    jmp  .search

.found:
    mov  rax, [rbx + 8]
    call rax
    jmp  .done

.unknown:
    mov  rsi, msg_unknown_cmd
    mov  bl,  0x0C
    call xk_print

.done:
    pop  rdi
    pop  rsi
    pop  rbx
    pop  rax
    ret

; =============================================================================
; COMANDOS MODULARES — cada uno en su propio archivo
; =============================================================================
%include "src/apps/xsh/ver.asm"
%include "src/apps/xsh/clear.asm"
%include "src/apps/xsh/list.asm"
%include "src/apps/xsh/makedir.asm"
%include "src/apps/xsh/makefile.asm"
%include "src/apps/xsh/remove.asm"
%include "src/apps/xsh/read.asm"
%include "src/apps/xsh/write.asm"
%include "src/apps/xsh/ubicate.asm"
%include "src/apps/xsh/whereami.asm"
%include "src/apps/xsh/sprusr.asm"
%include "src/apps/xsh/halt.asm"
%include "src/apps/xsh/exofetch/exofetch.asm"
