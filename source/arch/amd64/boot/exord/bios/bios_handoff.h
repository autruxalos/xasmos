#ifndef XASMOS_BIOS_HANDOFF_H
#define XASMOS_BIOS_HANDOFF_H
#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u
#define MULTIBOOT_MEM_AVAILABLE    1u
#define XERET_HANDOFF_MAGIC        0x5845524554484E44ULL
#define XERET_MMAP_MAX_BYTES       65536u
#define PixelBlueGreenRedReserved8BitPerColor 1u

typedef struct {
    uint64_t base;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t pixel_format;
} FramebufferInfo;

typedef struct {
    uint64_t        magic;
    FramebufferInfo fb;
    uint64_t        mmap_addr;
    uint64_t        mmap_size;
    uint64_t        mmap_desc_size;
    uint32_t        mmap_desc_version;
    uint32_t        _pad;
    uint64_t        phys_end;
    uint64_t        image_base;
    uint64_t        image_size;
} XeretHandoff;

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

int exord_bios_build_handoff(uint32_t mb_magic, const uint8_t *mb_mmap_buf, uint32_t mb_mmap_len,
                             const BiosBootParams *params, uint8_t *out_efi_mmap_buf,
                             uint64_t out_efi_mmap_cap, XeretHandoff *out_handoff);
#endif
