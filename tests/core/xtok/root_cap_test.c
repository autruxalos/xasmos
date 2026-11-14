/* Host test for the boot-time root capability. The kernel mints one root per
 * device; the boot hands it to exactly one holder. No name table, no identity
 * table: the holder receives a token, nothing else. */
#include <stdio.h>
#include <string.h>
#include "capability.h"
#include "blk.h"
#include "host_stubs.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define HOLDER 2u
#define OTHER  3u

static BlkDev dev;

static void test_root_minted_once_per_device(void) {
    xtok_roots_reset();
    XToken r1 = xtok_root_mint(&dev, HOLDER);
    CHECK(r1 != XTOKEN_INVALID);   /* the root exists */
    CHECK(xtok_root_mint(&dev, OTHER) == XTOKEN_INVALID);  /* already held */
    CHECK(xtok_root_holder(&dev) == HOLDER);
}

static void test_only_the_holder_gets_tokens(void) {
    void *dv = 0;
    UINT64 start = 1;
    xtok_roots_reset();
    xtok_root_mint(&dev, HOLDER);
    CHECK(xtok_root_claim(&dev, OTHER) == XTOKEN_INVALID);          /* not the holder */
    XToken t = xtok_root_claim(&dev, HOLDER);
    CHECK(t != XTOKEN_INVALID);
    CHECK(xtok_blk_range(t, &dv, &start) == 0);                     /* a live block token */
    CHECK(dv == &dev && start == 0);                                /* over the whole device */
    CHECK(xtok_release(t) == 0);
}

static void test_no_root_no_token(void) {
    xtok_roots_reset();
    CHECK(xtok_root_claim(&dev, HOLDER) == XTOKEN_INVALID);         /* never minted */
}

int main(void) {
    host_dev_init(&dev, 512, 1024);
    xtok_init();
    test_root_minted_once_per_device();
    test_only_the_holder_gets_tokens();
    test_no_root_no_token();
    printf("root_cap: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
