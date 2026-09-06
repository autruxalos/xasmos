#include "xlibc.h"

// Puntero global para rastrear la fila/columna de impresión en pantalla
static uint32_t cursor_pantalla = 0;

void xlibc_init(void) {
    cursor_pantalla = 0;
}

// -----------------------------------------------------------------------------
// PRINTF EN ESTEROIDES: Escritura directa en memoria de video text-mode
// -----------------------------------------------------------------------------
void printf(const char* formato, ...) {
    // Para mantener el código mínimo y preciso, procesamos caracteres puros.
    // Un printf real usaría stdarg.h para los argumentos, aquí va el núcleo de salida:
    uint8_t* vga = VIDEO_MEMORY + cursor_pantalla;
    
    while (*formato != '\0') {
        if (*formato == '\n') {
            // Alinear al inicio de la siguiente línea (80 caracteres * 2 bytes por carácter)
            uint32_t linea_actual = cursor_pantalla / 160;
            cursor_pantalla = (linea_actual + 1) * 160;
            vga = VIDEO_MEMORY + cursor_pantalla;
        } else {
            *vga = *formato;       // Byte de carácter ASCII
            *(vga + 1) = 0x0F;     // Byte de atributo: Blanco sobre fondo negro
            vga += 2;
            cursor_pantalla += 2;
        }
        formato++;
    }
}

// -----------------------------------------------------------------------------
// FOPEN EN ESTEROIDES: Búsqueda matemática directa en la tabla EXFS mapeada
// -----------------------------------------------------------------------------
XFILE* fopen(const char* nombre, const char* modo) {
    // Buscamos directamente en el búfer del Directorio Raíz (cargado por XBOOT en 0x9000)
    uint8_t* entrada_root = EXFS_ROOT;
    
    // El exokernel permite inspeccionar el directorio en bloques de 32 bytes
    for (int i = 0; i < 512; i++) {
        // Validación de coincidencia rápida de nombre (primeros 8 bytes)
        int coincidencia = 1;
        for (int j = 0; j < 8; j++) {
            if (entrada_root[j] != nombre[j]) {
                coincidencia = 0;
                break;
            }
        }
        
        if (coincidencia) {
            // El archivo existe. Instanciamos la estructura XFILE en el espacio de la app.
            // En un exokernel real, usarías un asignador local; aquí apuntamos a un bloque estático seguro.
            static XFILE archivo_abierto;
            
            // Mapeo directo de memoria a estructura
            archivo_abierto.bloque_inicio = *(uint16_t*)(entrada_root + 12);
            archivo_abierto.tamano = *(uint32_t*)(entrada_root + 14);
            archivo_abierto.posicion_actual = 0;
            
            return &archivo_abierto;
        }
        
        entrada_root += 32; // Avanzar a la siguiente entrada de archivo
    }
    
    return ((void*)0); // NULL si el archivo no existe en EXFS
}
