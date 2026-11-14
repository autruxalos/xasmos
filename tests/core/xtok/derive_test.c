/* Host test for attenuation: a derived block token covers a subset of its
 * parent, never more, and names no device. Runs against the real ledger. */
#include <stdio.h>
#include <string.h>
#include "capability.h"
#include "blk.h"
#include "host_stubs.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define OWNER 2u
#define OTHER 3u
#define RW (XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_RELEASE)

static BlkDev dev;

static void test_subset_only(void) {
    XToken root = xtok_alloc_blocks(OWNER, &dev, 100, 64, RW);   /* device 100..163 */
    XToken r    = xtok_alloc_ram(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    CHECK(root != XTOKEN_INVALID && r != XTOKEN_INVALID);

    /* Child: relative blocks 8..15 of the parent, read only. */
    XToken c = xtok_derive_blocks(OWNER, root, 8, 8, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE);
    CHECK(c != XTOKEN_INVALID);
    /* lba 0 of the child is device block 108, so the pin lands inside. */
    CHECK(xtok_ring_pin(OWNER, c, r, XTOK_BLK_READ, 0, 8) == 0);
    xtok_ring_unpin(c, r);
    CHECK(xtok_ring_pin(OWNER, c, r, XTOK_BLK_READ, 0, 9) == -1);  /* past child */
    CHECK(xtok_ring_pin(OWNER, c, r, XTOK_BLK_READ, 8, 1) == -1);  /* past child */

    CHECK(xtok_release(c) == 0);
    CHECK(xtok_release(root) == 0);
    CHECK(xtok_release(r) == 0);
}

static void test_no_widening(void) {
    XToken root  = xtok_alloc_blocks(OWNER, &dev, 0, 16, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE);
    CHECK(xtok_derive_blocks(OWNER, root, 0, 17, XTOK_RIGHT_READ) == XTOKEN_INVALID); /* more blocks */
    CHECK(xtok_derive_blocks(OWNER, root, 12, 8, XTOK_RIGHT_READ) == XTOKEN_INVALID); /* runs past end */
    CHECK(xtok_derive_blocks(OWNER, root, 0, 4, XTOK_RIGHT_WRITE) == XTOKEN_INVALID); /* right not held */
    CHECK(xtok_derive_blocks(OTHER, root, 0, 4, XTOK_RIGHT_READ) == XTOKEN_INVALID);  /* not the owner */
    CHECK(xtok_derive_blocks(OWNER, root, 0, 0, XTOK_RIGHT_READ) == XTOKEN_INVALID);  /* empty */
    CHECK(xtok_release(root) == 0);
}

static void test_derive_from_ram_refused(void) {
    XToken r = xtok_alloc_ram(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    CHECK(xtok_derive_blocks(OWNER, r, 0, 1, XTOK_RIGHT_READ) == XTOKEN_INVALID);
    CHECK(xtok_release(r) == 0);
}

int main(void) {
    host_dev_init(&dev, 512, 1024);
    xtok_init();
    test_subset_only();
    test_no_widening();
    test_derive_from_ram_refused();
    printf("derive: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
