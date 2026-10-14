/******************************************************************************
 * XASMOS - xeret entry (UEFI / GOP)
 * Path: source|arch|amd64|kernel|xeret|main.c
 ******************************************************************************/
#include "../../boot/efi/efi_types.h"
#include "../../../kernel/include/kmemory.h"

extern void fb_console_init(FramebufferInfo *fb);
extern void xk_print(const char *str);
extern void xk_println(const char *str);
extern void xk_putchar(char c);

/* Minimal hex printer — no libc. */
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
    UINT64 demo;

    if (hand == 0 || hand->magic != XERET_HANDOFF_MAGIC) {
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    fb_console_init(&hand->fb);

    xk_println("XASMOS MAINFRAME EXOKERNEL -- xeret (UEFI/GOP)");
    xk_println("Boot Services exited. Handoff accepted.");

    xk_print("  fb base=");
    print_u64_hex(hand->fb.base);
    xk_print("  ");
    print_u64_dec(hand->fb.width);
    xk_print("x");
    print_u64_dec(hand->fb.height);
    xk_println("");

    xk_print("  mmap size=");
    print_u64_dec(hand->mmap_size);
    xk_print("  desc_size=");
    print_u64_dec(hand->mmap_desc_size);
    xk_print("  phys_end=");
    print_u64_hex(hand->phys_end);
    xk_println("");

    if (phys_init_from_handoff(hand) != 0) {
        xk_println("FATAL: physical bitmap init failed.");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }

    xk_print("  frames total=");
    print_u64_dec(phys_total_count());
    xk_print("  free=");
    print_u64_dec(phys_free_count());
    xk_println("");

    /* Smoke test: allocate one 4 KiB frame and one 2 MiB huge page */
    demo = phys_alloc_frames(1);
    xk_print("  alloc 4K  -> ");
    print_u64_hex(demo);
    xk_println("");

    demo = phys_alloc_huge();
    xk_print("  alloc 2MiB-> ");
    print_u64_hex(demo);
    xk_println("");

    xk_print("  free after smoke=");
    print_u64_dec(phys_free_count());
    xk_println("");

    xk_println("xeret physical notary online.");
    /* Next: token ledger + EXIT/LibOS handoff */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
