/******************************************************************************
 * XASMOS - System Bootstrap Coordinator (EXIT)
 * Path: source|arch|amd64|init|exit.c
 ******************************************************************************/

// External reference to the main shell entry point
extern void shell_main(void);
extern void xk_println(const char *str);

/**
 * EXIT is the first userland-space execution unit handed off by the exokernel.
 * It prepares the environment and boots the primary interface (XSH).
 */
void exit_main_executor(void) {
    xk_println("[EXIT] Initializing XASMOS userland environment...");
    xk_println("[EXIT] Spawning core shell (XSH)...");

    // Hand control over to the shell
    shell_main();

    // If the shell exits for any reason, trap the system safely
    xk_println("[EXIT ERROR] Core shell terminated unexpectedly. Halting system.");
    while (1) {
        __asm__ volatile ("hlt");
    }
}
