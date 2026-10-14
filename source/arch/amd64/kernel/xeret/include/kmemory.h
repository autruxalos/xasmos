/******************************************************************************
 * XASMOS - Physical memory (xeret internal)
 * Path: source|kernel|include|kmemory.h
 *
 * Flat bitmap of 4 KiB frames. Huge-page (2 MiB) helpers group 512 frames.
 * Not exposed to apps — apps receive Hardware Tokens, not raw frame numbers.
 ******************************************************************************/
#ifndef XASMOS_KMEMORY_H
#define XASMOS_KMEMORY_H

#include "../arch/amd64/boot/efi/efi_types.h"

/* 4 KiB physical frame */
#define XERET_FRAME_SHIFT   12
#define XERET_FRAME_SIZE    (1ULL << XERET_FRAME_SHIFT)

/* 2 MiB huge frame = 512 × 4 KiB */
#define XERET_HUGE_SHIFT    21
#define XERET_HUGE_SIZE     (1ULL << XERET_HUGE_SHIFT)
#define XERET_FRAMES_PER_HUGE 512ULL

/* Bitmap capacity: enough for 128 GiB of RAM at 4 KiB granularity.
 * 128 GiB / 4 KiB = 33_554_432 frames → 4 MiB of bits.
 * Adjust XERET_BITMAP_MAX_FRAMES if you target more RAM. */
#define XERET_BITMAP_MAX_FRAMES  (128ULL * 1024ULL * 1024ULL * 1024ULL / XERET_FRAME_SIZE)
#define XERET_BITMAP_BYTES       (XERET_BITMAP_MAX_FRAMES / 8)

typedef struct {
    UINT64 total_frames;     /* frames covered by bitmap (phys_end / 4K) */
    UINT64 free_frames;
    UINT64 used_frames;
    UINT64 phys_end;         /* exclusive */
    UINT8 *bits;             /* 1 = free, 0 = used/reserved */
} PhysBitmap;

/* Build bitmap from UEFI memory map inside XeretHandoff. Returns 0 on success. */
int  phys_init_from_handoff(const XeretHandoff *hand);

/* Allocate n contiguous 4 KiB frames. Returns physical address or 0 on failure. */
UINT64 phys_alloc_frames(UINT64 n);

/* Free n frames starting at physical address addr (must be frame-aligned). */
void   phys_free_frames(UINT64 addr, UINT64 n);

/* Allocate one 2 MiB-aligned huge region (512 frames). Returns phys or 0. */
UINT64 phys_alloc_huge(void);

/* Mark an arbitrary physical range used (e.g. kernel image, framebuffer). */
void   phys_reserve_range(UINT64 addr, UINT64 size);

/* Query */
UINT64 phys_free_count(void);
UINT64 phys_total_count(void);

#endif /* XASMOS_KMEMORY_H */
