#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bios_handoff.h"

int main(void) {
    MbMmapEntry ent;
    ent.size = 20u;
    ent.addr = 0x00100000ULL;
    ent.len  = 0x07F00000ULL; /* 127 MiB */
    ent.type = MULTIBOOT_MEM_AVAILABLE;

    BiosBootParams bp = { 0xFD000000ULL, 1280u, 720u, 5120u, 32u, 0x00100000ULL, 0x00100000ULL };
    uint8_t out_mmap[512];
    XeretHandoff h;
    memset(&h, 0, sizeof(h));

    assert(exord_bios_build_handoff(MULTIBOOT_BOOTLOADER_MAGIC, (const uint8_t *)&ent, sizeof(ent),
                                    &bp, out_mmap, sizeof(out_mmap), &h) == 0);
    assert(h.magic == XERET_HANDOFF_MAGIC);
    assert(h.fb.width == 1280u && h.fb.height == 720u && h.fb.pixels_per_scanline == 1280u);
    assert(h.mmap_size == h.mmap_desc_size && h.mmap_desc_version == 1u);
    assert(h.phys_end == 0x08000000ULL);
    puts("PASS: test_bios_handoff");
    return 0;
}
