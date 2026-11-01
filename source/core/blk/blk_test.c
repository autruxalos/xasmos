/* Host test for blk.c with an in-memory fake device. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "blk.h"

typedef struct { unsigned char *mem; uint64_t sectors; uint32_t ss; int fail_next; } Fake;

static int passes, fails;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

static int f_read(void *c, uint64_t lba, uint32_t n, void *buf) {
    Fake *f = c;
    if (f->fail_next) { f->fail_next = 0; return BLK_E_IO; }
    memcpy(buf, f->mem + lba * f->ss, (size_t)n * f->ss);
    return BLK_OK;
}

static int f_write(void *c, uint64_t lba, uint32_t n, const void *buf) {
    Fake *f = c;
    if (f->fail_next) { f->fail_next = 0; return BLK_E_IO; }
    memcpy(f->mem + lba * f->ss, buf, (size_t)n * f->ss);
    return BLK_OK;
}

static const BlkOps ops = { f_read, f_write, 0 };

int main(void) {
    Fake f = { calloc(16, 512), 16, 512, 0 };
    BlkDev d = { &f, &ops, 512, 16 };
    unsigned char buf[512 * 4], out[512 * 4];

    CHECK(blk_sector_size(&d) == 512 && blk_sector_count(&d) == 16, "geometry reported");
    memset(buf, 0xAB, sizeof buf);
    CHECK(blk_write(&d, 4, 4, buf) == BLK_OK, "write 4 sectors at lba 4");
    CHECK(blk_read(&d, 4, 4, out) == BLK_OK && memcmp(out, buf, sizeof buf) == 0, "read back matches byte for byte");
    CHECK(blk_read(&d, 15, 1, out) == BLK_OK, "last sector is reachable");
    CHECK(blk_read(&d, 16, 1, out) == BLK_E_RANGE, "lba == count is out of range");
    CHECK(blk_read(&d, 15, 2, out) == BLK_E_RANGE, "run past the end is refused");
    CHECK(blk_write(&d, 0, 17, buf) == BLK_E_RANGE, "count larger than the device is refused");
    CHECK(blk_read(&d, UINT64_MAX, 1, out) == BLK_E_RANGE, "huge lba cannot wrap around");
    CHECK(blk_read(&d, 0, 0, out) == BLK_E_ARG, "zero count is refused");
    CHECK(blk_read(&d, 0, 1, NULL) == BLK_E_ARG, "null buffer is refused");
    f.fail_next = 1;
    CHECK(blk_write(&d, 0, 1, buf) == BLK_E_IO, "driver error reaches the caller");
    CHECK(blk_flush(&d) == BLK_OK, "flush without a driver hook succeeds");

    free(f.mem);
    printf("blk: %d passed, %d failed\n", passes, fails);
    return fails != 0;
}
