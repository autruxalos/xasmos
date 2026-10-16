/******************************************************************************
 * XASMOS - Physical frame bitmap (xeret internal)
 * Path: source|arch|amd64|kernel|xeret|mem|physical.c
 *
 * Policy: UEFI ConventionalMemory + BootServices* + Loader* become free
 * after ExitBootServices. Runtime/ACPI-NVS/MMIO stay reserved forever.
 * ACPI reclaim is reserved until an ACPI LibOS claims it via token.
 *
 * Concurrency: one coarse-grained spinlock guards the bitmap. Must never be
 * held while calling into token_ledger.c (see that file's header comment).
 ******************************************************************************/
#include "kmemory.h"
#include "spinlock.h"

/* Static bitmap — no dynamic alloc before the allocator exists. */
static UINT8      g_bitmap_storage[XERET_BITMAP_BYTES];
static PhysBitmap g_phys;
static XSpinlock  g_lock = XSPINLOCK_INIT;

static void bit_set(UINT64 frame) {
    g_phys.bits[frame >> 3] |= (UINT8)(1u << (frame & 7));
}

static void bit_clear(UINT64 frame) {
    g_phys.bits[frame >> 3] &= (UINT8)~(1u << (frame & 7));
}

static int bit_test(UINT64 frame) {
    return (g_phys.bits[frame >> 3] >> (frame & 7)) & 1;
}

static void mark_range_free(UINT64 start, UINT64 pages) {
    UINT64 f, end;

    if (pages == 0) {
        return;
    }
    f = start >> XERET_FRAME_SHIFT;
    end = f + pages;
    if (end > g_phys.total_frames) {
        end = g_phys.total_frames;
    }
    for (; f < end; f++) {
        if (!bit_test(f)) {
            bit_set(f);
            g_phys.free_frames++;
            if (g_phys.used_frames > 0) {
                g_phys.used_frames--;
            }
        }
    }
}

static void mark_range_used(UINT64 start, UINT64 bytes) {
    UINT64 f, end, pages;

    if (bytes == 0) {
        return;
    }
    f = start >> XERET_FRAME_SHIFT;
    pages = (bytes + XERET_FRAME_SIZE - 1) >> XERET_FRAME_SHIFT;
    end = f + pages;
    if (end > g_phys.total_frames) {
        end = g_phys.total_frames;
    }
    for (; f < end; f++) {
        if (bit_test(f)) {
            bit_clear(f);
            if (g_phys.free_frames > 0) {
                g_phys.free_frames--;
            }
            g_phys.used_frames++;
        }
    }
}

/* Called once at boot, before APs start and before xspin_lock is needed. */
int phys_init_from_handoff(const XeretHandoff *hand) {
    const UINT8 *map;
    UINT64 off, desc_size, map_size, phys_end, frames;
    UINT64 i;

    if (hand == 0 || hand->magic != XERET_HANDOFF_MAGIC) {
        return -1;
    }
    if (hand->mmap_addr == 0 || hand->mmap_size == 0 || hand->mmap_desc_size == 0) {
        return -1;
    }

    phys_end = hand->phys_end;
    if (phys_end == 0) {
        return -1;
    }

    /* Round up to frame boundary */
    frames = (phys_end + XERET_FRAME_SIZE - 1) >> XERET_FRAME_SHIFT;
    if (frames > XERET_BITMAP_MAX_FRAMES) {
        frames = XERET_BITMAP_MAX_FRAMES;
        phys_end = frames << XERET_FRAME_SHIFT;
    }

    g_phys.bits         = g_bitmap_storage;
    g_phys.total_frames = frames;
    g_phys.phys_end     = phys_end;
    g_phys.free_frames  = 0;
    g_phys.used_frames  = frames;
    g_lock              = XSPINLOCK_INIT;

    /* Default: everything used/reserved */
    for (i = 0; i < XERET_BITMAP_BYTES; i++) {
        g_bitmap_storage[i] = 0;
    }

    map       = (const UINT8 *)(UINTN)hand->mmap_addr;
    map_size  = hand->mmap_size;
    desc_size = hand->mmap_desc_size;

    for (off = 0; off + desc_size <= map_size; off += desc_size) {
        const EFI_MEMORY_DESCRIPTOR *d =
            (const EFI_MEMORY_DESCRIPTOR *)(map + off);

        switch (d->Type) {
        case EfiConventionalMemory:
        case EfiBootServicesCode:
        case EfiBootServicesData:
        case EfiLoaderCode:
        case EfiLoaderData:
            /* Free RAM after ExitBootServices */
            mark_range_free(d->PhysicalStart, d->NumberOfPages);
            break;
        default:
            /* Runtime, ACPI NVS, MMIO, reserved, ACPI reclaim: stay used */
            break;
        }
    }

    /* Always reserve the first 1 MiB (IVT/BDA/legacy holes) */
    phys_reserve_range(0, 0x100000);

    /* Reserve framebuffer pages so the console never gets recycled */
    if (hand->fb.base != 0 && hand->fb.height != 0) {
        UINT64 fb_bytes = (UINT64)hand->fb.pixels_per_scanline * 4ULL *
                          (UINT64)hand->fb.height;
        phys_reserve_range(hand->fb.base, fb_bytes);
    }

    return 0;
}

UINT64 phys_alloc_frames(UINT64 n) {
    UINT64 start, run, f, result;

    if (n == 0) {
        return 0;
    }

    xspin_lock(&g_lock);
    if (n > g_phys.free_frames) {
        xspin_unlock(&g_lock);
        return 0;
    }

    run = 0;
    start = 0;
    result = 0;
    for (f = 0; f < g_phys.total_frames; f++) {
        if (bit_test(f)) {
            if (run == 0) {
                start = f;
            }
            run++;
            if (run == n) {
                UINT64 i;
                for (i = start; i < start + n; i++) {
                    bit_clear(i);
                }
                g_phys.free_frames -= n;
                g_phys.used_frames += n;
                result = start << XERET_FRAME_SHIFT;
                break;
            }
        } else {
            run = 0;
        }
    }
    xspin_unlock(&g_lock);
    return result;
}

void phys_free_frames(UINT64 addr, UINT64 n) {
    UINT64 f, i;

    if (n == 0 || (addr & (XERET_FRAME_SIZE - 1)) != 0) {
        return;
    }
    f = addr >> XERET_FRAME_SHIFT;

    xspin_lock(&g_lock);
    if (f + n > g_phys.total_frames) {
        xspin_unlock(&g_lock);
        return;
    }
    for (i = f; i < f + n; i++) {
        if (!bit_test(i)) {
            bit_set(i);
            g_phys.free_frames++;
            if (g_phys.used_frames > 0) {
                g_phys.used_frames--;
            }
        }
    }
    xspin_unlock(&g_lock);
}

UINT64 phys_alloc_huge(void) {
    UINT64 f, run, start, result;

    xspin_lock(&g_lock);
    run = 0;
    start = 0;
    result = 0;
    /* Scan for 512 contiguous free frames at a 2 MiB boundary */
    for (f = 0; f < g_phys.total_frames; f++) {
        if ((f & (XERET_FRAMES_PER_HUGE - 1)) == 0) {
            run = 0;
            start = f;
        }
        if (bit_test(f)) {
            run++;
            if (run == XERET_FRAMES_PER_HUGE) {
                UINT64 i;
                for (i = start; i < start + XERET_FRAMES_PER_HUGE; i++) {
                    bit_clear(i);
                }
                g_phys.free_frames -= XERET_FRAMES_PER_HUGE;
                g_phys.used_frames += XERET_FRAMES_PER_HUGE;
                result = start << XERET_FRAME_SHIFT;
                break;
            }
        } else {
            run = 0;
            /* Next candidate must be huge-aligned */
            if ((f & (XERET_FRAMES_PER_HUGE - 1)) != (XERET_FRAMES_PER_HUGE - 1)) {
                /* Skip to next huge boundary */
                f = (f | (XERET_FRAMES_PER_HUGE - 1));
            }
        }
    }
    xspin_unlock(&g_lock);
    return result;
}

void phys_reserve_range(UINT64 addr, UINT64 size) {
    xspin_lock(&g_lock);
    mark_range_used(addr, size);
    xspin_unlock(&g_lock);
}

UINT64 phys_free_count(void) {
    UINT64 v;
    xspin_lock(&g_lock);
    v = g_phys.free_frames;
    xspin_unlock(&g_lock);
    return v;
}

UINT64 phys_total_count(void) {
    /* total_frames never changes after init: safe to read without the lock. */
    return g_phys.total_frames;
}
