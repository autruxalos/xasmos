/******************************************************************************
 * XASMOS - Mainframe Exokernel Core Entry (Pure C)
 * Path: source|arch|amd64|kernel|xeret|main.c
 ******************************************************************************/

// External reference to the physical memory allocator
extern void physical_mem_init(unsigned long long total_ram_bytes);

void kernel_main(void) {
    // Direct pointer to text-mode VGA video memory
    char *vga_buffer = (char*) 0xB8000;
    const char *banner = "XASMOS MAINFRAME EXOKERNEL ONLINE";
    
    // Paint the startup message directly onto the video hardware
    for (int i = 0; banner[i] != '\0'; i++) {
        vga_buffer[i * 2]     = banner[i];
        vga_buffer[i * 2 + 1] = 0x0F; // Bright white text on black background
    }

    // Initialize physical memory allocator (Assume 64MB baseline for QEMU test)
    // 64 MB = 67,108,864 bytes
    physical_mem_init(67108864ULL);

    // Kernel stop loop (exokernel idles and hands control over to LibOS)
    while (1) {
        __asm__ volatile ("hlt");
    }
}
