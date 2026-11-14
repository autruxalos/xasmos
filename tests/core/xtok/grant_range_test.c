/* Host test for range-scoped device grants. A grant names a device, an owner,
 * rights, and optionally a block range. A range grant covers only the blocks
 * inside it. A grant with count 0 covers the whole device, as before. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "capability.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define OWNER 1u

static void setup(void) {
    xtok_grants_reset();
}

static void test_whole_device_grant_unchanged(void) {
    setup();
    xtok_grant_put_full("rd0", OWNER, XTOK_RIGHT_READ | XTOK_RIGHT_WRITE);
    CHECK(xtok_grant_check_range("rd0", OWNER, XTOK_RIGHT_READ, 0, 1u << 20) == 1);
    CHECK(xtok_grant_check_range("rd0", OWNER, XTOK_RIGHT_WRITE, 999, 1) == 1);
}

static void test_range_grant_covers_only_its_blocks(void) {
    setup();
    xtok_grant_put_range("nd0", OWNER, XTOK_RIGHT_READ, 4096, 4096);
    CHECK(xtok_grant_check_range("nd0", OWNER, XTOK_RIGHT_READ, 4096, 1) == 1);
    CHECK(xtok_grant_check_range("nd0", OWNER, XTOK_RIGHT_READ, 8191, 1) == 1);
    CHECK(xtok_grant_check_range("nd0", OWNER, XTOK_RIGHT_READ, 8190, 2) == 1);
    CHECK(xtok_grant_check_range("nd0", OWNER, XTOK_RIGHT_READ, 8191, 2) == 0); /* runs past end */
    CHECK(xtok_grant_check_range("nd0", OWNER, XTOK_RIGHT_READ, 4095, 1) == 0); /* before start */
    CHECK(xtok_grant_check_range("nd0", OWNER, XTOK_RIGHT_READ, 0, 1) == 0);
}

static void test_rights_and_owner_still_checked(void) {
    setup();
    xtok_grant_put_range("nd0", OWNER, XTOK_RIGHT_READ, 0, 100);
    CHECK(xtok_grant_check_range("nd0", OWNER, XTOK_RIGHT_WRITE, 0, 1) == 0); /* not granted */
    CHECK(xtok_grant_check_range("nd0", 2u, XTOK_RIGHT_READ, 0, 1) == 0);     /* other owner */
    CHECK(xtok_grant_check_range("sd0", OWNER, XTOK_RIGHT_READ, 0, 1) == 0);  /* no grant */
}

static void test_overflow_is_refused(void) {
    setup();
    xtok_grant_put_range("nd0", OWNER, XTOK_RIGHT_READ, 100, 100);
    CHECK(xtok_grant_check_range("nd0", OWNER, XTOK_RIGHT_READ, UINT64_MAX, 2) == 0);
    CHECK(xtok_grant_check_range("nd0", OWNER, XTOK_RIGHT_READ, 150, UINT64_MAX) == 0);
}

int main(void) {
    test_whole_device_grant_unchanged();
    test_range_grant_covers_only_its_blocks();
    test_rights_and_owner_still_checked();
    test_overflow_is_refused();
    printf("grant_range: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
