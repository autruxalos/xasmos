/* Host test for xtok_give: a holder with GIVE transfers a token to another live
 * owner. Refused while the token is pinned, mapped, or held by the ring table.
 * After the transfer, authority is the destination's: the giver can neither use
 * nor release it. Rights and range do not change. */
#include <stdio.h>
#include "capability.h"
#include "blk.h"
#include "host_stubs.h"
#include "owner.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static BlkDev dev;

static void test_needs_give(void) {
    UINT32 a = owner_new(), b = owner_new();
    XToken t = xtok_alloc_blocks(a, &dev, 0, 8, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE);
    CHECK(xtok_give(a, t, b) == -1);                       /* no GIVE: refused */
    CHECK(xtok_release(t) == 0);
    CHECK(owner_retire(a) == 0);
    CHECK(owner_retire(b) == 0);
}

static void test_destination_must_be_live(void) {
    UINT32 a = owner_new();
    XToken t = xtok_alloc_blocks(a, &dev, 0, 8, XTOK_RIGHTS_BLOCKS_DEFAULT | XTOK_RIGHT_GIVE);
    CHECK(xtok_give(a, t, XOWNER_FIRST_INSTANCE + 60) == -1);   /* id never handed out */
    CHECK(xtok_give(a, t, a) == -1);                            /* self-transfer is not a transfer */
    CHECK(xtok_release(t) == 0);
    CHECK(owner_retire(a) == 0);
}

static void test_pinned_or_mapped_refused(void) {
    UINT32 a = owner_new(), b = owner_new();
    XToken t = xtok_alloc_ram(a, 1, XTOK_RIGHTS_RAM_DEFAULT | XTOK_RIGHT_GIVE);
    CHECK(xtok_map_acquire(t, a, XTOK_RIGHT_READ, 0, 0) == XTOK_MAP_OK);
    CHECK(xtok_give(a, t, b) == -1);                            /* mapped: refused */
    xtok_map_release(t);
    CHECK(xtok_give(a, t, b) == 0);                             /* unmapped: accepted */
    CHECK(xtok_release_owned(t, b) == 0);
    CHECK(owner_retire(a) == 0);
    CHECK(owner_retire(b) == 0);
}

static void test_transfer_moves_authority(void) {
    UINT32 a = owner_new(), b = owner_new();
    XToken t = xtok_alloc_blocks(a, &dev, 0, 8, XTOK_RIGHTS_BLOCKS_DEFAULT | XTOK_RIGHT_GIVE);
    CHECK(xtok_give(a, t, b) == 0);
    CHECK(xtok_release_owned(t, a) == XTOK_REL_ESTALE);         /* giver is out */
    CHECK(xtok_give(a, t, b) == -1);                            /* and cannot give it again */
    UINT64 fa = 0, fb = 0; UINT32 ta = 0, tb = 0;
    xtok_owner_usage(a, &fa, &ta);
    xtok_owner_usage(b, &fb, &tb);
    CHECK(ta == 0 && tb == 1);
    CHECK(xtok_release_owned(t, b) == 0);                       /* destination releases it */
    CHECK(owner_retire(a) == 0);
    CHECK(owner_retire(b) == 0);
}

static void test_rights_and_range_unchanged(void) {
    UINT32 a = owner_new(), b = owner_new();
    XToken t = xtok_alloc_blocks(a, &dev, 16, 4, XTOK_RIGHT_READ | XTOK_RIGHT_GIVE | XTOK_RIGHT_RELEASE);
    XToken r = xtok_alloc_ram(b, 1, XTOK_RIGHTS_RAM_DEFAULT);
    CHECK(xtok_give(a, t, b) == 0);
    void *dv = 0; UINT64 start = 0;
    CHECK(xtok_blk_range(t, &dv, &start) == 0 && dv == &dev && start == 16);
    CHECK(xtok_ring_pin(b, t, r, XTOK_BLK_READ, 0, 4) == 0);   /* granted range still works */
    xtok_ring_unpin(t, r);
    CHECK(xtok_ring_pin(b, t, r, XTOK_BLK_READ, 0, 5) == -1);  /* and does not grow */
    CHECK(xtok_ring_pin(b, t, r, XTOK_BLK_WRITE, 0, 1) == -1); /* WRITE was never granted */
    CHECK(xtok_release_owned(t, b) == 0);
    CHECK(xtok_release_owned(r, b) == 0);
    CHECK(owner_retire(a) == 0);
    CHECK(owner_retire(b) == 0);
}

static void test_ring_held_token_refused(void) {
    UINT32 a = owner_new(), b = owner_new();
    XToken t = xtok_alloc_ram(a, 1, XTOK_RIGHTS_RAM_DEFAULT | XTOK_RIGHT_GIVE);
    CHECK(xtok_hold(t) == 0);
    CHECK(xtok_give(a, t, b) == -1);                            /* the ring table owns it */
    xtok_unhold(t);
    CHECK(xtok_give(a, t, b) == 0);
    CHECK(xtok_release_owned(t, b) == 0);
    CHECK(owner_retire(a) == 0);
    CHECK(owner_retire(b) == 0);
}

/* A block token over a whole disk is not memory: it counts as one token, and
 * no frames of the memory quota. */
static void test_block_token_uses_no_frame_quota(void) {
    UINT32 a = owner_new();
    XToken big = xtok_alloc_blocks(a, &dev, 0, 131072, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE);
    UINT64 f = 0; UINT32 t = 0;
    CHECK(big != XTOKEN_INVALID);
    xtok_owner_usage(a, &f, &t);
    CHECK(f == 0 && t == 1);
    CHECK(xtok_release(big) == 0);
    CHECK(owner_retire(a) == 0);
}

int main(void) {
    host_dev_init(&dev, 512, 1024);
    xtok_init();
    owner_init();
    test_needs_give();
    test_destination_must_be_live();
    test_pinned_or_mapped_refused();
    test_transfer_moves_authority();
    test_rights_and_range_unchanged();
    test_ring_held_token_refused();
    test_block_token_uses_no_frame_quota();
    printf("give: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
