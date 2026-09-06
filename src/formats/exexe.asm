; =============================================================================
; ARCHIVO DE INFRAESTRUCTURA: xexe.asm
; Plantilla base para ejecutables del Exokernel XOS
; =============================================================================

; Definimos el origen de memoria virtual estándar para las aplicaciones de usuario.
; En nuestro exokernel, las aplicaciones se mapean por defecto en la dirección 0x500000.
bits 64                         ; Las aplicaciones corren en el Modo Largo nativo del Phenom

; =============================================================================
; CABECERA FORMAL XEXE (16 Bytes Fijos)
; =============================================================================
xexe_header:
    .magic       db "XEXE"      ; 4 Bytes - Firma mágica de identificación
    .entry_point dd xexe_main   ; 4 Bytes - Dirección exacta de la primera instrucción a ejecutar
    .flags       dw 0x0000      ; 2 Bytes - Atributos del ejecutable (Ej: 0=Usuario, 1=Sistema)
    .reservado   dd 0x00000000  ; 6 Bytes - Reservados para expansión futura (Alineación a 16 bytes)

; =============================================================================
; PUNTO DE ENTRADA DE LA APLICACIÓN
; =============================================================================
xexe_main:
    ; Al entrar aquí, el Exokernel ha transferido el control a la aplicación.
    ; Como es un Exokernel, la aplicación tiene acceso directo a los recursos asignados.

    call app_inicializar        ; Configurar entorno local de la aplicación
    call app_ejecutar           ; Lógica principal (Editor, Administrador, etc.)
    
    ; Terminar la ejecución de forma limpia devolviendo el control al Exokernel.
    ; Usamos la instrucción nativa "sysret" o un salto directo al CLI del Kernel.
    mov rax, 0                  ; Código de salida 0 (Sin errores)
    jmp 0x10000                 ; Volver a la dirección base del XKernel (Bucle de comandos)

; =============================================================================
; SUBRUTINAS INTERNAS DE LA INFRAESTRUCTURA XEXE
; =============================================================================
app_inicializar:
    ; Limpieza de registros generales para evitar fugas de datos del kernel o procesos previos
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    ret

; =============================================================================
; SECCIÓN EXTENSIBLE: AQUÍ SE ESCRIBE EL PROGRAMA REAL
; =============================================================================
app_ejecutar:
    ; --- Ejemplo de código mínimo: Pintar una señal en pantalla ---
    ; Acceso directo a la memoria de video (Exokernel style) si el kernel dio el permiso.
    mov rdi, 0xB8000            ; Dirección base de la pantalla de texto
    add rdi, 160                ; Moverse a la segunda línea (80 caracteres * 2 bytes)
    
    mov al, '['
    mov [rdi], al
    mov byte [rdi+1], 0x0E      ; Letra Amarilla
    
    mov al, 'A'
    mov [rdi+2], al
    mov byte [rdi+3], 0x0F      ; Letra Blanca
    
    mov al, 'P'
    mov [rdi+4], al
    mov byte [rdi+5], 0x0F
    
    mov al, ']'
    mov [rdi+6], al
    mov byte [rdi+7], 0x0E
    
    ret
