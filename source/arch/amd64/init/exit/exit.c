/******************************************************************************
 * XASMOS - System bootstrap coordinator (EXIT)
 * Path: source/arch/amd64/init/exit/exit.c
 *
 * EXIT is the first userland code handed off by the exokernel. It runs the
 * bootstrap stages in order, then gives control to the shell.
 ******************************************************************************/
#include "exit.h"
#include "stage_volumes.h"

extern void shell_main(void);
extern void xk_println(const char *str);

void exit_main_executor(void) {
    xk_println("[EXIT] Initializing XASMOS userland environment...");

    if (stage_volumes() != 0) {
        xk_println("[EXIT ERROR] volume stage failed. Halting system.");
        while (1) {
            __asm__ volatile ("cli; hlt");
        }
    }

    xk_println("[EXIT] Spawning core shell (XSH)...");
    shell_main();

    /* The shell returns only on halt; trap the system safely either way. */
    xk_println("[EXIT ERROR] Core shell terminated unexpectedly. Halting system.");
    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}
