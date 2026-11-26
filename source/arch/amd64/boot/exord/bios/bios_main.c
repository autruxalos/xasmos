#include "bios_handoff.h"

static uint8_t g_bios_mmap_buf[XERET_MMAP_MAX_BYTES];
static XeretHandoff g_bios_handoff;

const XeretHandoff *exord_bios_get_handoff(void) {
    return (g_bios_handoff.magic == XERET_HANDOFF_MAGIC) ? &g_bios_handoff : 0;
}

void kernel_main(uint32_t mb_magic, uint32_t mb_info_addr) {
    const uint32_t *mbi = (const uint32_t *)(uintptr_t)mb_info_addr;
    const uint8_t *mmap_ptr = 0;
    uint32_t mmap_len = 0u;
    BiosBootParams bp;
    bp.fb_addr = 0xFD000000ULL;
    bp.fb_width = 1024u;
    bp.fb_height = 768u;
    bp.fb_pitch = 4096u;
    bp.fb_bpp = 32u;
    bp.image_base = 0x00100000ULL;
    bp.image_size = 0x00200000ULL;

    if (mb_magic == MULTIBOOT_BOOTLOADER_MAGIC && mbi && (mbi[0] & (1u << 6))) {
        mmap_len = mbi[11];
        mmap_ptr = (const uint8_t *)(uintptr_t)mbi[12];
    }
    (void)exord_bios_build_handoff(mb_magic, mmap_ptr, mmap_len, &bp,
                                   g_bios_mmap_buf, sizeof(g_bios_mmap_buf), &g_bios_handoff);
    for (;;) __asm__ volatile("hlt");
}
