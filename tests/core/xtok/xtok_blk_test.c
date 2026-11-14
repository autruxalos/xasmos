/* Host test for the block-token barrier: xtok_alloc_blocks / xtok_blk_io /
 * release, against the real token_ledger.c and blk.c, with a RAM disk. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "capability.h"
#include "blk.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- RAM-backed block driver --------------------------------------------- */
#define SECT  512u
#define NSECT 1024u
static uint8_t disk[NSECT * SECT];
static int     drv_fail;

static int drv_read(void *ctx, uint64_t lba, uint32_t n, void *buf) {
    (void)ctx;
    if (drv_fail) return BLK_E_IO;
    memcpy(buf, disk + lba * SECT, (size_t)n * SECT);
    return BLK_OK;
}
static int drv_write(void *ctx, uint64_t lba, uint32_t n, const void *buf) {
    (void)ctx;
    if (drv_fail) return BLK_E_IO;
    memcpy(disk + lba * SECT, buf, (size_t)n * SECT);
    return BLK_OK;
}
static int drv_flush(void *ctx) { (void)ctx; return BLK_OK; }

static const BlkOps ops = { drv_read, drv_write, drv_flush, NULL };
static BlkDev dev;

/* ---- frame allocator stub: counts calls so the test can see frees ---------- */
static int frees;
UINT64 phys_alloc_frames(UINT64 n) {
    void *p = aligned_alloc(4096, (size_t)(n * 4096));
    if (!p) return 0;
    memset(p, 0x5A, (size_t)(n * 4096));
    return (UINT64)(uintptr_t)p;
}
void phys_free_frames(UINT64 addr, UINT64 n) {
    (void)n;
    frees++;
    free((void *)(uintptr_t)addr);
}
UINT64 phys_alloc_huge(void) { return phys_alloc_frames(512); }

/* ---- tests ----------------------------------------------------------------- */
static void test_range_and_data(void) {
    static uint8_t big[50 * SECT];
    uint8_t one[SECT], back[SECT];
    memset(one, 0xAB, sizeof one);

    XToken t = xtok_alloc_blocks(2, &dev, 100, 50, XTOK_RIGHTS_BLOCKS_DEFAULT);
    CHECK(t != XTOKEN_INVALID);

    CHECK(xtok_blk_io(t, XTOK_BLK_WRITE, 100, 1, one) == BLK_OK);
    CHECK(xtok_blk_io(t, XTOK_BLK_READ, 100, 1, back) == BLK_OK);
    CHECK(memcmp(back, one, SECT) == 0);

    CHECK(xtok_blk_io(t, XTOK_BLK_READ, 99, 1, back) == XTOK_BLK_EDENIED);
    CHECK(xtok_blk_io(t, XTOK_BLK_READ, 149, 2, back) == XTOK_BLK_EDENIED);
    CHECK(xtok_blk_io(t, XTOK_BLK_READ, 150, 1, back) == XTOK_BLK_EDENIED);
    CHECK(xtok_blk_io(t, XTOK_BLK_READ, 100, 51, big) == XTOK_BLK_EDENIED);

    CHECK(xtok_blk_io(t, XTOK_BLK_READ, 100, 50, big) == BLK_OK);
    CHECK(xtok_blk_io(t, XTOK_BLK_READ, 149, 1, back) == BLK_OK);

    CHECK(xtok_blk_io(t, XTOK_BLK_READ, 100, 0, back) == BLK_E_ARG);
    CHECK(xtok_blk_io(t, XTOK_BLK_READ, 100, 1, NULL) == BLK_E_ARG);

    CHECK(xtok_release(t) == 0);
}

static void test_overflow(void) {
    uint8_t back[SECT];
    XToken t = xtok_alloc_blocks(2, &dev, 0, 8, XTOK_RIGHTS_BLOCKS_DEFAULT);
    CHECK(t != XTOKEN_INVALID);
    CHECK(xtok_blk_io(t, XTOK_BLK_READ, UINT64_MAX - 1, 4, back) == XTOK_BLK_EDENIED);
    CHECK(xtok_blk_io(t, XTOK_BLK_READ, UINT64_MAX, 1, back) == XTOK_BLK_EDENIED);
    CHECK(xtok_release(t) == 0);
}

static void test_rights(void) {
    uint8_t buf[SECT];
    memset(buf, 1, sizeof buf);
    XToken t = xtok_alloc_blocks(2, &dev, 0, 8,
                                 XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE);
    CHECK(t != XTOKEN_INVALID);
    CHECK(xtok_blk_io(t, XTOK_BLK_READ, 0, 1, buf) == BLK_OK);
    CHECK(xtok_blk_io(t, XTOK_BLK_WRITE, 0, 1, buf) == XTOK_BLK_EDENIED);
    CHECK(xtok_release(t) == 0);
}

static void test_wrong_kind(void) {
    uint8_t buf[SECT];
    XToken r = xtok_alloc_ram(2, 1, XTOK_RIGHTS_RAM_DEFAULT);
    CHECK(r != XTOKEN_INVALID);
    CHECK(xtok_blk_io(r, XTOK_BLK_READ, 0, 1, buf) == XTOK_BLK_EDENIED);
    CHECK(xtok_release(r) == 0);
}

static void test_stale_and_no_frame_free(void) {
    uint8_t buf[SECT];
    XToken old = xtok_alloc_blocks(2, &dev, 0, 8, XTOK_RIGHTS_BLOCKS_DEFAULT);
    CHECK(old != XTOKEN_INVALID);

    int before = frees;
    CHECK(xtok_release(old) == 0);
    CHECK(frees == before);            /* a block token frees no frames */

    CHECK(xtok_blk_io(old, XTOK_BLK_READ, 0, 1, buf) == XTOK_BLK_EDENIED);

    XToken fresh = xtok_alloc_blocks(2, &dev, 0, 8, XTOK_RIGHTS_BLOCKS_DEFAULT);
    CHECK(fresh != XTOKEN_INVALID);
    CHECK(fresh != old);               /* generation changed */
    CHECK(xtok_blk_io(old, XTOK_BLK_READ, 0, 1, buf) == XTOK_BLK_EDENIED);
    CHECK(xtok_blk_io(fresh, XTOK_BLK_READ, 0, 1, buf) == BLK_OK);
    CHECK(xtok_release(fresh) == 0);
}

static void test_driver_error_unpins(void) {
    uint8_t buf[SECT];
    XToken t = xtok_alloc_blocks(2, &dev, 0, 8, XTOK_RIGHTS_BLOCKS_DEFAULT);
    CHECK(t != XTOKEN_INVALID);

    drv_fail = 1;
    CHECK(xtok_blk_io(t, XTOK_BLK_READ, 0, 1, buf) == BLK_E_IO);
    drv_fail = 0;

    /* If io_count leaked on the error path, release would refuse with EBUSY. */
    CHECK(xtok_release(t) == 0);
}

int main(void) {
    xtok_init();
    dev.ctx         = NULL;
    dev.ops         = &ops;
    dev.sector_size = SECT;
    dev.sector_count = NSECT;

    test_range_and_data();
    test_overflow();
    test_rights();
    test_wrong_kind();
    test_stale_and_no_frame_free();
    test_driver_error_unpins();

    printf("xtok_blk: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
