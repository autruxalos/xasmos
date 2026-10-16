/******************************************************************************
 * XASMOS - xeret entry point under UEFI (GOP framebuffer + memory handoff)
 * Path: source|arch|amd64|kernel|xeret|main.c
 ******************************************************************************/
#include "efi_types.h"
#include "kmemory.h"
#include "capability.h"
#include "diag.h"

extern void fb_console_init(FramebufferInfo *fb);
extern void xk_println(const char *str);
extern void xk_print(const char *str);
extern void xk_init_keyboard(void);
extern void exit_main_executor(void);
extern void serial_puts(const char *s);

/* Mirrors a hex value onto the framebuffer console, not just serial --
 * diag.h's serial_hex only writes to COM1, and these numbers matter even
 * without a serial cable attached. */
static void fb_hex(UINT64 v) {
    static const char digits[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) buf[2 + i] = digits[(v >> (60 - 4 * i)) & 0xF];
    buf[18] = '\0';
    xk_print(buf);
}

void kernel_main(XeretHandoff *hand) {
    fb_console_init(&hand->fb);
    diag_init();
    xk_init_keyboard();

    xk_println("XASMOS MAINFRAME EXOKERNEL -- xeret (UEFI/GOP)");
    xk_println("Boot Services exited. Handoff accepted.");

    if (hand->magic != XERET_HANDOFF_MAGIC) {
        xk_println("EXBADBOOT: handoff magic mismatch. Halting.");
        serial_puts("EXBADBOOT: handoff magic mismatch\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    if (phys_init_from_handoff(hand) != 0) {
        xk_println("EXBADBOOT: physical memory init failed. Halting.");
        serial_puts("EXBADBOOT: phys_init_from_handoff failed\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }
    xtok_init();

    xk_print("fb "); fb_hex(hand->fb.width); xk_print("x"); fb_hex(hand->fb.height);
    xk_print(" phys_end="); fb_hex(hand->phys_end); xk_println("");

    xk_print("frames free="); fb_hex(phys_free_count());
    xk_print("/"); fb_hex(phys_total_count()); xk_println("");

    exit_main_executor();

    for (;;) { __asm__ volatile ("hlt"); }
}
