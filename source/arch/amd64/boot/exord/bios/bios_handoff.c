#include "bios_handoff.h"

typedef struct {
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} BiosEfiMemDesc;

int exord_bios_build_handoff(uint32_t mb_magic,
                             const uint8_t *mb_mmap_buf,
                             uint32_t mb_mmap_len,
                             const BiosBootParams *params,
                             uint8_t *out_efi_mmap_buf,
                             uint64_t out_efi_mmap_cap,
                             XeretHandoff *out_handoff) {
    if (mb_magic != MULTIBOOT_BOOTLOADER_MAGIC || !params || !out_efi_mmap_buf || !out_handoff) return -1;
    uint64_t desc_sz = sizeof(BiosEfiMemDesc);
    uint64_t max_descs = out_efi_mmap_cap / desc_sz;
    uint64_t count = 0u;
    uint64_t max_phys = params->image_base + params->image_size;

    uint32_t off = 0u;
    while (mb_mmap_buf && off + 24u <= mb_mmap_len && count < max_descs) {
        const MbMmapEntry *e = (const MbMmapEntry *)(const void *)(mb_mmap_buf + off);
        uint32_t step = e->size + 4u;
        if (step < 24u || off + step > mb_mmap_len) break;
        if (e->len >= 4096ULL) {
            BiosEfiMemDesc *d = (BiosEfiMemDesc *)(void *)(out_efi_mmap_buf + count * desc_sz);
            d->Type = (e->type == MULTIBOOT_MEM_AVAILABLE) ? 7u : 0u;
            d->Pad = 0u;
            d->PhysicalStart = e->addr;
            d->VirtualStart = e->addr;
            d->NumberOfPages = e->len / 4096ULL;
            d->Attribute = 0x0FULL;
            if (e->type == MULTIBOOT_MEM_AVAILABLE && (e->addr + e->len) > max_phys) {
                max_phys = e->addr + e->len;
            }
            count++;
        }
        off += step;
    }

    out_handoff->magic = XERET_HANDOFF_MAGIC;
    out_handoff->fb.base = params->fb_addr;
    out_handoff->fb.width = params->fb_width;
    out_handoff->fb.height = params->fb_height;
    out_handoff->fb.pixels_per_scanline = (params->fb_bpp >= 24u && params->fb_pitch > 0u)
        ? (params->fb_pitch / (params->fb_bpp / 8u)) : params->fb_width;
    out_handoff->fb.pixel_format = PixelBlueGreenRedReserved8BitPerColor;
    out_handoff->mmap_addr = (uint64_t)(uintptr_t)out_efi_mmap_buf;
    out_handoff->mmap_size = count * desc_sz;
    out_handoff->mmap_desc_size = desc_sz;
    out_handoff->mmap_desc_version = 1u;
    out_handoff->_pad = 0u;
    out_handoff->phys_end = max_phys;
    out_handoff->image_base = params->image_base;
    out_handoff->image_size = params->image_size;
    return (count > 0u) ? 0 : -2;
}
