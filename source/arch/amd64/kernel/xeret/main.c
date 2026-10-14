/******************************************************************************
 * XASMOS - xeret entry (UEFI / GOP)
 * Path: source|arch|amd64|kernel|xeret|main.c
 ******************************************************************************/
#include "../../boot/efi/efi_types.h"
#include "../../../kernel/include/kmemory.h"
#include "../../../kernel/include/capability.h"

extern void fb_console_init(FramebufferInfo *fb);
extern void xk_print(const char *str);
extern void xk_println(const char *str);
extern void xk_putchar(char c);

static void print_u64_hex(UINT64 v) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[17];
    int i;
    for (i = 15; i >= 0; i--) {
        buf[i] = hex[v & 0xF];
        v >>= 4;
    }
    buf[16] = 0;
    xk_print("0x");
    xk_print(buf);
}

static void print_u64_dec(UINT64 v) {
    char buf[21];
    int i = 20;
    buf[i] = 0;
    if (v == 0) {
        xk_putchar('0');
        return;
    }
    while (v > 0 && i > 0) {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    xk_print(&buf[i]);
}

void kernel_main(XeretHandoff *hand) {
    XToken tok4k, tok2m, stale;
    UINT64 phys, frames;

    if (hand == 0 || hand->magic != XERET_HANDOFF_MAGIC) {
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    fb_console_init(&hand->fb);

    xk_println("XASMOS MAINFRAME EXOKERNEL -- xeret (UEFI/GOP)");
    xk_println("Boot Services exited. Handoff accepted.");

    xk_print("  fb ");
    print_u64_dec(hand->fb.width);
    xk_print("x");
    print_u64_dec(hand->fb.height);
    xk_print("  phys_end=");
    print_u64_hex(hand->phys_end);
    xk_println("");

    if (phys_init_from_handoff(hand) != 0) {
        xk_println("FATAL: physical bitmap init failed.");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    xtok_init();

    xk_print("  frames free=");
    print_u64_dec(phys_free_count());
    xk_print("/");
    print_u64_dec(phys_total_count());
    xk_println("");

    /* --- Smoke: 4 KiB token --- */
    tok4k = xtok_alloc_ram(XTOK_OWNER_FIRST_APP, 1, XTOK_RIGHTS_RAM_DEFAULT);
    xk_print("  token 4K  = ");
    print_u64_hex(tok4k);
    xk_println("");

    if (!xtok_validate(tok4k, XTOK_RIGHT_READ | XTOK_RIGHT_WRITE)) {
        xk_println("FATAL: 4K token validate failed.");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }
    if (xtok_ram_info(tok4k, &phys, &frames) == 0) {
        xk_print("    phys=");
        print_u64_hex(phys);
        xk_print(" frames=");
        print_u64_dec(frames);
        xk_println("");
    }

    /* --- Smoke: 2 MiB huge token --- */
    tok2m = xtok_alloc_ram_huge(XTOK_OWNER_FIRST_APP, XTOK_RIGHTS_RAM_DEFAULT);
    xk_print("  token 2MiB= ");
    print_u64_hex(tok2m);
    xk_println("");

    /* --- Release 4K and prove generation guard --- */
    stale = tok4k;
    if (xtok_release(tok4k) != 0) {
        xk_println("FATAL: release 4K failed.");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }
    if (xtok_validate(stale, XTOK_RIGHT_READ)) {
        xk_println("FATAL: stale token still valid after release.");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }
    xk_println("  stale token rejected (generation guard OK).");

    xk_print("  tokens live=");
    print_u64_dec(xtok_live_count());
    xk_print("  frames free=");
    print_u64_dec(phys_free_count());
    xk_println("");

    xk_println("xeret notary online (bitmap + token ledger).");
    /* Next: own page tables, then EXIT/LibOS */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
