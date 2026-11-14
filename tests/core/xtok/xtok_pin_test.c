/* Host test for the ring pins in token_ledger.c: xtok_ring_pin/unpin and how
 * they interact with release and with mapping. */
#include <stdio.h>
#include "capability.h"
#include "blk.h"
#include "host_stubs.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define OWNER 2u
#define OTHER 3u
#define BLK_RO (XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE)
#define RAM_RO (XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE)

static BlkDev dev;

static XToken blk_tok(UINT32 owner, UINT32 rights) {
    return xtok_alloc_blocks(owner, &dev, 0, 100, rights);
}
static XToken ram_tok(UINT32 owner, UINT64 frames, UINT32 rights) {
    return xtok_alloc_ram(owner, frames, rights);
}

static void test_lifecycle(void) {
    XToken b = blk_tok(OWNER, XTOK_RIGHTS_BLOCKS_DEFAULT);
    XToken r = ram_tok(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    CHECK(b != XTOKEN_INVALID && r != XTOKEN_INVALID);
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 0, 8) == 0);
    CHECK(xtok_release(b) == XTOK_REL_EBUSY);   /* pinned: refused */
    CHECK(xtok_release(r) == XTOK_REL_EBUSY);
    xtok_ring_unpin(b, r);
    CHECK(xtok_release(b) == 0);
    CHECK(xtok_release(r) == 0);
}

static void test_direction_and_rights(void) {
    XToken b = blk_tok(OWNER, XTOK_RIGHTS_BLOCKS_DEFAULT);
    XToken r = ram_tok(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    /* write: block needs WRITE, RAM needs READ; read: block READ, RAM WRITE */
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_WRITE, 0, 1) == 0);
    xtok_ring_unpin(b, r);
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 0, 1) == 0);
    xtok_ring_unpin(b, r);

    XToken b_ro = blk_tok(OWNER, BLK_RO);
    XToken r_ro = ram_tok(OWNER, 1, RAM_RO);
    CHECK(xtok_ring_pin(OWNER, b_ro, r, XTOK_BLK_WRITE, 0, 1) == -1);  /* no WRITE on block */
    CHECK(xtok_ring_pin(OWNER, b_ro, r_ro, XTOK_BLK_READ, 0, 1) == -1); /* RAM not writable */
    /* A write needs WRITE on the block and only READ on the RAM frame. */
    XToken b_rw = blk_tok(OWNER, XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_RELEASE);
    CHECK(xtok_ring_pin(OWNER, b_rw, r_ro, XTOK_BLK_WRITE, 0, 1) == 0);
    xtok_ring_unpin(b_rw, r_ro);
    CHECK(xtok_release(b_rw) == 0);

    CHECK(xtok_release(b) == 0 && xtok_release(r) == 0);
    CHECK(xtok_release(b_ro) == 0 && xtok_release(r_ro) == 0);
}

static void test_ranges_and_frames(void) {
    XToken b = blk_tok(OWNER, XTOK_RIGHTS_BLOCKS_DEFAULT);   /* blocks [0,100) */
    XToken r = ram_tok(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);   /* 4096 bytes     */
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 100, 1) == -1);
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 99, 2) == -1);
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 0, 0) == -1);
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 98, 2) == 0);   /* ends exactly at 100 */
    xtok_ring_unpin(b, r);

    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 0, 9) == -1);   /* 9*512 > 4096 */
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 0, 8) == 0);    /* 8*512 == 4096 */
    xtok_ring_unpin(b, r);

    CHECK(xtok_release(b) == 0 && xtok_release(r) == 0);
}

static void test_mapping(void) {
    XToken b = blk_tok(OWNER, XTOK_RIGHTS_BLOCKS_DEFAULT);
    XToken r = ram_tok(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    UINT64 p = 0, f = 0;

    CHECK(xtok_map_acquire(r, OWNER, XTOK_RIGHT_READ, &p, &f) == XTOK_MAP_OK);
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 0, 1) == -1);   /* mapped: refused */
    xtok_map_release(r);

    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 0, 1) == 0);
    CHECK(xtok_map_acquire(r, OWNER, XTOK_RIGHT_READ, &p, &f) == XTOK_MAP_EBUSY); /* pinned: no map */
    xtok_ring_unpin(b, r);

    CHECK(xtok_map_acquire(r, OWNER, XTOK_RIGHT_READ, &p, &f) == XTOK_MAP_OK);
    xtok_map_release(r);
    CHECK(xtok_release(b) == 0 && xtok_release(r) == 0);
}

static void test_owners_and_kinds(void) {
    XToken b = blk_tok(OWNER, XTOK_RIGHTS_BLOCKS_DEFAULT);
    XToken r = ram_tok(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    XToken b_other = blk_tok(OTHER, XTOK_RIGHTS_BLOCKS_DEFAULT);
    XToken r_other = ram_tok(OTHER, 1, XTOK_RIGHTS_RAM_DEFAULT);

    CHECK(xtok_ring_pin(OWNER, b_other, r, XTOK_BLK_READ, 0, 1) == -1);
    CHECK(xtok_ring_pin(OWNER, b, r_other, XTOK_BLK_READ, 0, 1) == -1);
    CHECK(xtok_ring_pin(OTHER, b, r, XTOK_BLK_READ, 0, 1) == -1);
    CHECK(xtok_ring_pin(OWNER, r, b, XTOK_BLK_READ, 0, 1) == -1);   /* kinds swapped */

    CHECK(xtok_release(b) == 0 && xtok_release(r) == 0);
    CHECK(xtok_release(b_other) == 0 && xtok_release(r_other) == 0);
}

static void test_failed_pin_leaves_nothing(void) {
    XToken b = blk_tok(OWNER, XTOK_RIGHTS_BLOCKS_DEFAULT);
    XToken r = ram_tok(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 200, 1) == -1);
    /* a refused pin must not have counted: both tokens release at once */
    CHECK(xtok_release(b) == 0);
    CHECK(xtok_release(r) == 0);
}

static void test_stale_and_double_pin(void) {
    XToken stale = blk_tok(OWNER, XTOK_RIGHTS_BLOCKS_DEFAULT);
    XToken r = ram_tok(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    CHECK(xtok_release(stale) == 0);
    CHECK(xtok_ring_pin(OWNER, stale, r, XTOK_BLK_READ, 0, 1) == -1);

    XToken b = blk_tok(OWNER, XTOK_RIGHTS_BLOCKS_DEFAULT);
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 0, 1) == 0);
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 0, 1) == 0);
    xtok_ring_unpin(b, r);
    CHECK(xtok_release(b) == XTOK_REL_EBUSY);   /* one pin still held */
    xtok_ring_unpin(b, r);
    CHECK(xtok_release(b) == 0);
    CHECK(xtok_release(r) == 0);
}

/* A token that starts past block 0: ring entries still count from its first
 * block, so lba 0 is device block 512 and nothing outside the token pins. */
static void test_relative_lba_on_offset_token(void) {
    XToken b = xtok_alloc_blocks(OWNER, &dev, 512, 8, XTOK_RIGHTS_BLOCKS_DEFAULT);
    XToken r = ram_tok(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    CHECK(b != XTOKEN_INVALID && r != XTOKEN_INVALID);
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 0, 8) == 0);
    xtok_ring_unpin(b, r);
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 0, 9) == -1);
    CHECK(xtok_ring_pin(OWNER, b, r, XTOK_BLK_READ, 8, 1) == -1);
    CHECK(xtok_release(b) == 0 && xtok_release(r) == 0);
}

int main(void) {
    host_dev_init(&dev, 512, 1024);
    xtok_init();
    test_lifecycle();
    test_direction_and_rights();
    test_ranges_and_frames();
    test_mapping();
    test_owners_and_kinds();
    test_failed_pin_leaves_nothing();
    test_relative_lba_on_offset_token();
    test_stale_and_double_pin();
    printf("xtok_pin: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
