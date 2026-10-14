/******************************************************************************
 * XASMOS - xeret entry (UEFI / GOP)
 * Path: source|arch|amd64|kernel|xeret|main.c
 *
 * Order: handoff → fb → physical bitmap → own page tables → tokens → halt
 ******************************************************************************/
#include "efi_types.h"
#include "kmemory.h"
#include "capability.h"
#include "paging.h"

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

static void fatal(const char *msg) {
    xk_println(msg);
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void kernel_main(XeretHandoff *hand) {
    XToken tok4k, tok2m, stale;
    UINT64 phys, frames;
    UINT64 fb_bytes;

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

    /* 1. Physical bitmap */
    if (phys_init_from_handoff(hand) != 0) {
        fatal("FATAL: physical bitmap init failed.");
    }

    xk_print("  frames free=");
    print_u64_dec(phys_free_count());
    xk_print("/");
    print_u64_dec(phys_total_count());
    xk_println("");

    /* 2. Own page tables (2 MiB identity + framebuffer) */
    fb_bytes = (UINT64)hand->fb.pixels_per_scanline * 4ULL *
               (UINT64)hand->fb.height;
    if (xpt_init_identity(hand->phys_end, hand->fb.base, fb_bytes) != 0) {
        fatal("FATAL: page table install failed.");
    }
    xk_print("  pml4 phys=");
    print_u64_hex(xpt_pml4_phys());
    xk_println("  (own tables, 2MiB identity)");

    /* 3. Token ledger smoke */
    xtok_init();

    tok4k = xtok_alloc_ram(XTOK_OWNER_FIRST_APP, 1, XTOK_RIGHTS_RAM_DEFAULT);
    xk_print("  token 4K  = ");
    print_u64_hex(tok4k);
    xk_println("");

    if (tok4k == XTOKEN_INVALID ||
        !xtok_validate(tok4k, XTOK_RIGHT_READ | XTOK_RIGHT_WRITE)) {
        fatal("FATAL: 4K token validate failed.");
    }

    if (xtok_ram_info(tok4k, &phys, &frames) == 0) {
        xk_print("    phys=");
        print_u64_hex(phys);
        xk_print(" frames=");
        print_u64_dec(frames);
        xk_println("");
    }

    tok2m = xtok_alloc_ram_huge(XTOK_OWNER_FIRST_APP, XTOK_RIGHTS_RAM_DEFAULT);
    xk_print("  token 2MiB= ");
    print_u64_hex(tok2m);
    xk_println("");

    stale = tok4k;
    if (xtok_release(tok4k) != 0) {
        fatal("FATAL: release 4K failed.");
    }
    if (xtok_validate(stale, XTOK_RIGHT_READ)) {
        fatal("FATAL: stale token still valid after release.");
    }
    xk_println("  stale token rejected (generation guard OK).");

    xk_print("  tokens live=");
    print_u64_dec((UINT64)xtok_live_count());
    xk_print("  frames free=");
    print_u64_dec(phys_free_count());
    xk_println("");

    xk_println("xeret notary online (bitmap + page tables + token ledger).");
    /* Next: IDT, XQUEUE, EXIT/LibOS */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
