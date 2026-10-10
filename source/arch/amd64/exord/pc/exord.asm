;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; XASMOS - EXORD Master Boot Record (MBR) Bootloader
;; INTEGRACIÓN: BLOQUE 1 (Lectura de Disco) + BLOQUE 2 (Salto a 32-bits)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
bits 16                         ; Iniciamos en Modo Real de 16 bits
global _start

KERNEL_OFFSET equ 0x1000        ; Dirección de segmento donde cargamos el código

section .text

_start:
    ;; 1. Configurar los registros de segmento base de forma segura
    xor ax, ax                  
    mov ds, ax                  
    mov es, ax                  
    mov ss, ax                  
    mov sp, 0x7C00              ; Pila reubicada debajo del MBR

    mov [BOOT_DRIVE], dl        ; Guardar la unidad de arranque dada por la BIOS

    ;; 2. Mensaje inicial en pantalla
    mov si, msg_welcome
    call bios_print_string

    ;; 3. EJECUCIÓN DEL BLOQUE 1: Cargar sectores del disco a la RAM
    mov bx, KERNEL_OFFSET       
    mov es, bx
    xor bx, bx                  ; ES:BX = 0x1000:0000
    mov dh, 16                  ; Cargar 16 sectores
    call disk_load

    mov si, msg_disk_success
    call bios_print_string

    ;; 4. EJECUCIÓN DEL BLOQUE 2: Preparar la transición a 32 bits
    cli                         ; 1. Deshabilitar interrupciones de 16-bits de la BIOS
    lgdt [gdt_descriptor]       ; 2. Cargar la estructura física de la GDT en la CPU

    mov eax, cr0
    or eax, 0x1                 ; 3. Activar el bit de Modo Protegido (PE) en CR0
    mov cr0, eax

    ;; 4. Realizar el SALTO LEJANO (Far Jump). Esto limpia la tubería de 16 bits 
    ;; y fuerza al procesador a usar nuestro selector de código de 32 bits.
    jmp CODE_SEG:init_32bit     

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; RUTINAS AUXILIARES EN 16 BITS (BLOQUE 1)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
disk_load:
    push dx
    mov ah, 0x02                
    mov al, dh                  
    mov ch, 0x00                
    mov dh, 0x00                
    mov cl, 0x02                
    mov dl, [BOOT_DRIVE]        
    int 0x13                    
    jc disk_error               
    pop dx                      
    cmp dh, al                  
    jne disk_error              
    ret

disk_error:
    mov si, msg_disk_error
    call bios_print_string
.halt_system:
    cli                         
    hlt                         
    jmp .halt_system

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
;; ESTRUCTURAS DE DATOS DE LA GDT DE 32 BITS (BLOQUE 2)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
align 8
gdt_start:
    dd 0x0                      ; Descriptor nulo obligatorio (8 bytes vacíos)
    dd 0x0

gdt_code:                       ; Descriptor de segmento de código plano
    dw 0xFFFF                   ; Límite (Bits 0-15)
    dw 0x0                      ; Base (Bits 0-15)
    db 0x0                      ; Base (Bits 16-23)
    db 10011010b                ; Banderas de acceso (Código ejecutable/lectura)
    db 11001111b                ; Granularidad + Límite (Bits 16-19)
    db 0x0                      ; Base (Bits 24-31)

gdt_data:                       ; Descriptor de segmento de datos plano
    dw 0xFFFF                   ; Límite (Bits 0-15)
    dw 0x0                      ; Base (Bits 0-15)
    db 0x0                      ; Base (Bits 16-23)
    db 10010010b                ; Banderas de acceso (Datos lectura/escritura)
    db 11001111b                ; Granularidad
    db 0x0                      ; Base (Bits 24-31)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; Tamaño de la GDT (menos 1)
    dd gdt_start                ; Dirección física de inicio de la GDT

;; Constantes para identificar los desplazamientos (offsets) de los segmentos
CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

;; Mensajes y variables
BOOT_DRIVE:        db 0
msg_welcome:       db "EXORD v1.0 - Inicializando XASMOS...", 0x0D, 0x0A, 0
msg_disk_success:  db "EXFS: Sectores del nucleo leidos con exito en RAM.", 0x0D, 0x0A, 0
msg_disk_error:    db "CRITICAL ERROR: Fallo masivo al leer el almacenamiento.", 0x0D, 0x0A, 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; SECCIÓN DE 32 BITS (EL BUS SE HA ENSANCHADO)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
bits 32
init_32bit:
    ;; Ahora los registros apuntan a descriptores de 32 bits planos en la GDT
    mov ax, DATA_SEG            
    mov ds, ax                  
    mov es, ax                  
    mov fs, ax                  
    mov gs, ax                  
    mov ss, ax                  

    mov ebp, 0x90000            ; Actualizamos la pila a un área espaciosa y segura
    mov esp, ebp                

    ;; Por ahora congelamos aquí la ejecución. 
    ;; El Bloque 3 tomará este punto para crear las tablas de páginas PML4.
.loop_32:
    hlt
    jmp .loop_32

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; FIRMA MBR OBLIGATORIA
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
times 510 - ($ - $$) db 0       
dw 0xAA55                       
