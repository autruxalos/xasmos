; =============================================================================
; sprusr — elevate to superuser (changes prompt from @ to %)
; [modular XSH command]
; =============================================================================
[BITS 64]

msg_sprusr_ok: db 'XSH: elevating to root.', 10, 0

xsh_cmd_sprusr:
    mov  byte [rel xsh_is_root], 1
    mov  rsi, msg_sprusr_ok
    mov  bl,  0x0C
    call xk_print
    ret
