#ifndef XASMOS_BIOS_HANDOFF_H
#define XASMOS_BIOS_HANDOFF_H
#include <stdint.h>
#include "efi_types.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u
#define MULTIBOOT_MEM_AVAILABLE    1u

typedef struct __attribute__((packed)) {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} MbMmapEntry;

typedef struct {
    uint64_t fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint8_t  fb_bpp;
    uint64_t image_base;
    uint64_t image_size;
} BiosBootParams;

int exord_bios_build_handoff(uint32_t mb_magic,
                             const uint8_t *mb_mmap_buf,
                             uint32_t mb_mmap_len,
                             const BiosBootParams *params,
                             uint8_t *out_efi_mmap_buf,
                             uint64_t out_efi_mmap_cap,
                             XeretHandoff *out_handoff);
#endif
