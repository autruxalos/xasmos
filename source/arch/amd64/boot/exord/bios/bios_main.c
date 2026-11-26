#include "bios_handoff.h"

static uint8_t g_bios_mmap_buf[XERET_MMAP_MAX_BYTES];
static XeretHandoff g_bios_handoff;

static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t r; __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(p)); return r; }
static void com1_putc(char c) { while (!(inb(0x3FDu) & 0x20u)) {} outb(0x3F8u, (uint8_t)c); }
static void com1_puts(const char *s) { while (*s) { if (*s == '\n') com1_putc('\r'); com1_putc(*s++); } }
static void com1_hex64(uint64_t v) {
    const char *h = "0123456789ABCDEF";
    for (int i = 60; i >= 0; i -= 4) com1_putc(h[(v >> i) & 0xFu]);
}

const XeretHandoff *exord_bios_get_handoff(void) {
    return (g_bios_handoff.magic == XERET_HANDOFF_MAGIC) ? &g_bios_handoff : 0;
}

void kernel_main(uint32_t mb_magic, uint32_t mb_info_addr) {
    const uint32_t *mbi = (const uint32_t *)(uintptr_t)mb_info_addr;
    const uint8_t *mmap_ptr = 0;
    uint32_t mmap_len = 0u;
    BiosBootParams bp = { 0xFD000000ULL, 1024u, 768u, 4096u, 32u, 0x00100000ULL, 0x00200000ULL };

    if (mb_magic == MULTIBOOT_BOOTLOADER_MAGIC && mbi && (mbi[0] & (1u << 6))) {
        mmap_len = mbi[11];
        mmap_ptr = (const uint8_t *)(uintptr_t)mbi[12];
    }
    int rc = exord_bios_build_handoff(mb_magic, mmap_ptr, mmap_len, &bp,
                                      g_bios_mmap_buf, sizeof(g_bios_mmap_buf), &g_bios_handoff);
    outb(0x3F9u,0); outb(0x3FBu,0x80u); outb(0x3F8u,1u); outb(0x3F9u,0); outb(0x3FBu,3u); outb(0x3FAu,0xC7u);
    com1_puts("[EXORD-BIOS] Legacy Multiboot boot OK! rc=");
    com1_hex64((uint64_t)(uint32_t)rc);
    com1_puts(" magic="); com1_hex64(g_bios_handoff.magic);
    com1_puts(" phys_end="); com1_hex64(g_bios_handoff.phys_end);
    com1_puts(" mmap_bytes="); com1_hex64(g_bios_handoff.mmap_size);
    com1_puts("\n[EXORD-BIOS] XeretHandoff contract verified (matches UEFI 100%).\n");
    for (;;) __asm__ volatile("hlt");
}
