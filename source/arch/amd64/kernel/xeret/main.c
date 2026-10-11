// Inside source|arch|amd64|kernel|xeret|main.c:

extern void physical_mem_init(unsigned long long total_ram_bytes);
extern void exit_main_executor(void); // Declaring EXIT

void kernel_main(void) {
    char *vga_buffer = (char*) 0xB8000;
    const char *banner = "XASMOS MAINFRAME EXOKERNEL ONLINE";
    
    for (int i = 0; banner[i] != '\0'; i++) {
        vga_buffer[i * 2]     = banner[i];
        vga_buffer[i * 2 + 1] = 0x0F;
    }

    // Initialize memory map (64MB baseline)
    physical_mem_init(67108864ULL);

    // Hand off control to EXIT (which launches XSH)
    exit_main_executor();

    while (1) {
        __asm__ volatile ("hlt");
    }
}
