; =============================================================================
; sysinfo — system information [separate XSH package]
; Ported to 64-bit: original version used BIOS INT 10h/12h/15h,
; which don't exist in Long Mode. Uses kernel primitives instead.
; =============================================================================
[BITS 64]

xsh_cmd_exofetch:
    mov  rsi, exofetch_logo
    mov  bl,  0x0B
    call xk_print

    mov  rsi, msg_exo_os
    mov  bl,  0x0F
    call xk_println

    mov  rsi, msg_exo_arch
    mov  bl,  0x0F
    call xk_println

    mov  rsi, msg_exo_modo
    mov  bl,  0x0F
    call xk_println

    mov  rsi, msg_exo_shell
    mov  bl,  0x0F
    call xk_println

    ret

msg_exo_os:    db 'OS: XASMOS (Exokernel)', 0
msg_exo_arch:  db 'Architecture: x86-64', 0
msg_exo_modo:  db 'Mode: Long Mode', 0
msg_exo_shell: db 'Shell: XSH v0.2', 0

exofetch_logo:
    db "                                                             #;                                     ", 10
    db "                                                          ,###S                                     ", 10
    db "                                                        :###+SS                                     ", 10
    db "                                                      .S##,  SS                                     ", 10
    db "                                                    ,##S.    SS;                                    ", 10
    db "                                                   S##,      .S%                                    ", 10
    db "                                                 +#%.        .S?                                    ", 10
    db "                                               *##,          .S?                                    ", 10
    db "                                             +#S:             *S,                                   ", 10
    db "                   .........................S#*............   +S,                                   ", 10
    db "                    .:;SSSSSSSS##########SS##########SSSSSSS%%%%%SSS%++*?.                          ", 10
    db "                                         ?SSSS??               %S                                    ", 10
    db "                                       ;SSS;.                  %%                                    ", 10
    db "                                      ;SS*                     ,%*                                  ", 10
    db "                                     %%S%                      ,?+                                  ", 10
    db "                                   .%%.S?                       *?.                                 ", 10
    db "                                  .%%  SS                       +?.                                 ", 10
    db "                                 .?*   %%,                      ,??                                 ", 10
    db "                                .%+    :%S,                      **                                 ", 10
    db "                                ?*       +%%,                    :*;                                ", 10
    db "                               *+         .;***::                ,*:                                ", 10
    db "                              :*               ;;;;;;;;+*????+:.  +*                                ", 10
    db "                              ;                                   ;+                                ", 10
    db "                              .                                    ;+                               ", 10, 0
