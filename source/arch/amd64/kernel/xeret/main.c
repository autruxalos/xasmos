/******************************************************************************
 * XASMOS - Mainframe Exokernel Core Entry (Pure C)
 ******************************************************************************/

void kernel_main(void) {
    // Puntero directo a la memoria de video de texto en modo protegido
    char *vga_buffer = (char*) 0xB8000;
    const char *banner = "XASMOS MAINFRAME EXOKERNEL ONLINE";
    
    // Pintar el mensaje de inicio directamente en el hardware de video
    for (int i = 0; banner[i] != '\0'; i++) {
        vga_buffer[i * 2]     = banner[i];
        vga_buffer[i * 2 + 1] = 0x0F; // Atributo: Texto blanco brillante sobre fondo negro
    }

    // Bucle de parada del núcleo (el exokernel descansa y cede control a las LibOS)
    while (1) {
        __asm__ volatile ("hlt");
    }
}
