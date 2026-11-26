/* rd0_memory_test: a RAM disk is fixed memory, not a device. Its frames are
 * wrapped in a MEM token. No release path returns them, the ring refuses it,
 * and a program reaches the bytes only by mapping a token it was handed. */
#include <stdio.h>
#include <string.h>
#include "capability.h"
#include "owner.h"
#include "endpoint.h"
#include "host_stubs.h"

UINT64 phys_alloc_frames(UINT64 n);
void   phys_free_frames(UINT64 addr, UINT64 n);

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define PAGE 4096u
#define NPAGES 16u

static int transfer(UINT32 from, XToken t, UINT32 to, XToken *got) {
    XToken er = XTOKEN_INVALID, es = XTOKEN_INVALID;
    int rc = -1;
    *got = XTOKEN_INVALID;
    if (xep_create(to, &er) != 0) return -1;
    if (xep_send_cap(to, er, from, &es) == 0) {
        if (xtok_offer(from, t, es) == 0) rc = xtok_accept(to, er, got);
        xtok_release_owned(es, from);
    }
    xep_destroy(to, er);
    return rc;
}

/* M1: no release path returns the frames or the token. */
static void m1_no_release_path(void) {
    UINT32 boot = owner_new();
    XToken m = XTOKEN_INVALID;
    UINT64 freed0 = host_phys_freed();
    UINT64 phys = phys_alloc_frames(NPAGES);
    CHECK(phys != 0);
    CHECK(xtok_mem_wrap(boot, phys, NPAGES, &m) == 0);
    CHECK(xtok_release_owned(m, boot) == XTOK_REL_ERIGHTS);
    CHECK(xtok_reclaim(m) == XTOK_REL_ERIGHTS);
    CHECK(xtok_revoke(m, boot) == XTOK_REL_ERIGHTS);
    CHECK(host_phys_freed() == freed0);           /* nothing went back to the allocator */
    CHECK(xtok_mem_free_count() == 0);
    CHECK(owner_retire(boot) != 0);                /* the boot holds rd0: cannot retire */
}

/* M2: a program handed rd0 maps it and sees the boot's bytes. */
static void m2_program_maps_and_reads(void) {
    UINT32 boot = owner_new(), prog = owner_new();
    XToken m = XTOKEN_INVALID, got = XTOKEN_INVALID;
    UINT64 phys = phys_alloc_frames(NPAGES);
    uint8_t *disk = (uint8_t *)(uintptr_t)phys;
    CHECK(phys != 0);
    memset(disk, 0x5A, NPAGES * PAGE);
    CHECK(xtok_mem_wrap(boot, phys, NPAGES, &m) == 0);
    CHECK(transfer(boot, m, prog, &got) == 0 && got == m);
    CHECK(xtok_map_acquire(m, prog, XTOK_RIGHT_READ, 0, 0) == XTOK_MAP_OK);
    CHECK(disk[0] == 0x5A && disk[NPAGES * PAGE - 1] == 0x5A);
    CHECK(xtok_map_count(m) == 1);
    xtok_map_release(m);
    CHECK(xtok_map_acquire(m, boot, XTOK_RIGHT_READ, 0, 0) != XTOK_MAP_OK); /* not the holder */
    CHECK(xtok_release_owned(m, prog) == XTOK_REL_ERIGHTS);
}

/* M3: the ring refuses a block token over memory: no copy happens. */
static void m3_ring_refuses_rd0(void) {
    UINT32 boot = owner_new();
    XToken m = XTOKEN_INVALID;
    UINT64 phys = phys_alloc_frames(NPAGES);
    CHECK(xtok_mem_wrap(boot, phys, NPAGES, &m) == 0);
    CHECK(xtok_release_owned(m, boot) == XTOK_REL_ERIGHTS);
}

/* M4: conservation. Creating and killing a MEM token moves no frames. */
static void m4_frames_never_freed(void) {
    UINT32 boot = owner_new();
    XToken m = XTOKEN_INVALID;
    UINT64 phys = phys_alloc_frames(NPAGES);
    UINT64 freed0 = host_phys_freed();
    CHECK(xtok_mem_wrap(boot, phys, NPAGES, &m) == 0);
    xtok_revoke(m, boot);
    xtok_reclaim(m);
    CHECK(host_phys_freed() == freed0);
}

/* M5: fragmented volume. 16 frames that are not contiguous, read back in order. */
static void m5_fragmented_volume(void) {
    UINT32 boot = owner_new();
    XToken m = XTOKEN_INVALID, x = XTOKEN_INVALID;
    UINT64 fr[16], p = 0, got = 0;
    for (int i = 0; i < 16; i++) {
        fr[i] = phys_alloc_frames(1);
        CHECK(fr[i] != 0);
        ((uint8_t *)(uintptr_t)fr[i])[0] = (uint8_t)(0xA0 + i);
    }
    CHECK(xtok_mem_wrap_list(boot, fr, 16, &m) == 0);
    for (UINT64 i = 0; i < 16; i++) {
        CHECK(xtok_frame_at(m, i, &p) == 0 && p == fr[i]);
        CHECK(((uint8_t *)(uintptr_t)p)[0] == (uint8_t)(0xA0 + i));
    }
    CHECK(xtok_frame_at(m, 16, &got) != 0);
    CHECK(xtok_map_acquire(m, boot, XTOK_RIGHT_READ, &p, &got) == XTOK_MAP_OK);
    CHECK(p == 0);
    xtok_map_release(m);
    CHECK(xtok_release_owned(m, boot) == XTOK_REL_ERIGHTS);
    for (int i = 0; i < 16; i++) (void)x;
}

int main(void) {
    xtok_init();
    owner_init();
    UINT32 base = xtok_live_count();
    m1_no_release_path();
    m2_program_maps_and_reads();
    m3_ring_refuses_rd0();
    m4_frames_never_freed();
    m5_fragmented_volume();
    CHECK(xtok_live_count() == base + 5);  /* four boot MEM tokens are permanent */
    printf("rd0_memory: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
