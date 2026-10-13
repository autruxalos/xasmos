/******************************************************************************
 * XASMOS - xeret entry point under UEFI (GOP framebuffer edition)
 * Path: source|arch|amd64|kernel|xeret|main.c (UEFI variant)
 ******************************************************************************/
#include "efi_types.h"

extern void fb_console_init(FramebufferInfo *fb);
extern void xk_println(const char *str);
/* extern void exit_main_executor(void);  -- wire in once shell is ported */

void kernel_main(FramebufferInfo *fb) {
    fb_console_init(fb);

    xk_println("XASMOS MAINFRAME EXOKERNEL -- xeret (UEFI/GOP)");
    xk_println("Framebuffer online. Boot Services exited successfully.");

    /* exit_main_executor(); */

    for (;;) { __asm__ volatile ("hlt"); }
}
