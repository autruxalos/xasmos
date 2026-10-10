;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; XASMOS - EXORD Master Boot Record (MBR) Bootloader
;; BLOQUE 1: Rutina de Lectura de Almacenamiento mediante BIOS (16-bits)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
bits 16                         ; Generar código nativo de 16 bits
global _start

;; Dirección física destino donde EXORD cargará la Etapa 2 / Kernel xeret
;; Usamos 0x1000:0000 (Dirección física 0x10000), una zona libre y segura en RAM.
KERNEL_OFFSET equ 0x1000

section .text

_start:
    ;; 1. Configurar los registros de segmento base de forma segura
    xor ax, ax                  
    mov ds, ax                  
    mov es, ax                  
    mov ss, ax                  
    mov sp, 0x7C00              ; Pila ubicada de forma segura debajo del MBR

    ;; Guardar el identificador del disco de arranque que nos da la BIOS en DL
    mov [BOOT_DRIVE], dl

    ;; 2. Mensaje inicial en pantalla
    mov si, msg_welcome
    call bios_print_string

    ;; 3. EJECUCIÓN DEL BLOQUE 1: Cargar sectores del disco a la RAM
    mov bx, KERNEL_OFFSET       ; ES:BX -> Segmento de destino en RAM
    mov es, bx
    xor bx, bx                  ; ES:BX = 0x1000:0000 (Dirección física 0x10000)
    
    mov dh, 16                  ; Cantidad de sectores que vamos a leer (Aprox 8KB para xeret)
    call disk_load

    ;; 4. Mensaje de éxito al leer el disco
    mov si, msg_disk_success
    call bios_print_string

    ;; JUMP a la memoria cargada (Por ahora un bucle hasta hacer el salto de bits en el Bloque 2)
.hang:
    hlt
    jmp .hang

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; RUTINA ESPECÍFICA DEL BLOQUE 1: LECTURA DE DISCO (INT 0x13)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
disk_load:
    push dx                     ; Preservar el número de sectores solicitados en la pila

    mov ah, 0x02                ; Función BIOS: Leer sectores desde la unidad
    mov al, dh                  ; Número de sectores a leer (pasado en DH)
    mov ch, 0x00                ; Cilindro 0
    mov dh, 0x00                ; Cabeza 0
    mov cl, 0x02                ; Empezar a leer desde el sector 2 (El sector 1 es este MBR)
    mov dl, [BOOT_DRIVE]        ; Unidad de almacenamiento (Cargada al inicio)
    int 0x13                    ; Llamada a la interrupción de la BIOS

    jc disk_error               ; Si el bit de acarreo (Carry Flag) se activa, hubo un error físico

    pop dx                      ; Recuperar DX original de la pila
    cmp dh, al                  ; Comparar los sectores solicitados (DH) con los leídos (AL)
    jne disk_error              ; Si no coinciden, el cargado fue incompleto
    ret

disk_error:
    mov si, msg_disk_error
    call bios_print_string
.halt_system:
    cli                         ; Apagar interrupciones
    hlt                         ; Congelar la CPU por completo
    jmp .halt_system

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; PROCEDIMIENTOS AUXILIARES
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
bios_print_string:
    lodsb                       
    or al, al                   
    jz .done
    mov ah, 0x0E                
    int 0x10                    
    jmp bios_print_string
.done:
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; DATOS DE CONFIGURACIÓN Y MENSAJES
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
BOOT_DRIVE:        db 0
msg_welcome:       db "EXORD v1.0 - Inicializando XASMOS...", 0x0D, 0x0A, 0
msg_disk_success:  db "EXFS: Sectores del nucleo leidos con exito en RAM.", 0x0D, 0x0A, 0
msg_disk_error:    db "CRITICAL ERROR: Fallo masivo al leer el almacenamiento.", 0x0D, 0x0A, 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; FIRMA MBR OBLIGATORIA
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
times 510 - ($ - $$) db 0       
dw 0xAA55                       
