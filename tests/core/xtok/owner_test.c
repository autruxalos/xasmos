/* Host test for per-instance owners. The kernel hands out one owner per program
 * execution. Two instances of the same program are two owners: reclaiming one
 * never touches the other's tokens. An owner retires only with nothing left,
 * and its id comes back only after that. */
#include <stdio.h>
#include <string.h>
#include "capability.h"
#include "owner.h"
#include "host_stubs.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void test_ids_are_fresh_and_unfixed(void) {
    UINT32 a = owner_new(), b = owner_new();
    CHECK(a != 0 && b != 0 && a != b);
    CHECK(a >= XOWNER_FIRST_INSTANCE && b >= XOWNER_FIRST_INSTANCE);
    CHECK(a != XTOK_OWNER_FIRST_APP && a != XTOK_OWNER_KERNEL);
    owner_retire(a);
    owner_retire(b);
}

static void test_reclaim_is_per_instance(void) {
    UINT32 a = owner_new(), b = owner_new();     /* same program, two runs */
    XToken ta = xtok_alloc_ram(a, 1, XTOK_RIGHTS_RAM_DEFAULT);
    XToken tb = xtok_alloc_ram(b, 1, XTOK_RIGHTS_RAM_DEFAULT);
    CHECK(ta != XTOKEN_INVALID && tb != XTOKEN_INVALID);

    XToken list[8];
    UINT32 n = xtok_owner_tokens(b, list, 8);
    CHECK(n == 1 && list[0] == tb);              /* b sees only its own */
    CHECK(xtok_reclaim(tb) == 0);
    CHECK(xtok_ram_info(ta, 0, 0) == 0);         /* a's token untouched */

    CHECK(owner_retire(b) == 0);                 /* b is empty now */
    CHECK(owner_retire(a) == -1);                /* a still holds ta */
    CHECK(xtok_reclaim(ta) == 0);
    CHECK(owner_retire(a) == 0);
}

static void test_retired_id_comes_back_only_after_retire(void) {
    UINT32 a = owner_new();
    XToken t = xtok_alloc_ram(a, 1, XTOK_RIGHTS_RAM_DEFAULT);
    CHECK(t != XTOKEN_INVALID);
    UINT32 other = owner_new();
    CHECK(other != a);                           /* a is live: not handed out */
    CHECK(xtok_reclaim(t) == 0);
    CHECK(owner_retire(a) == 0);
    CHECK(owner_new() == a);                     /* now it may be reused */
    owner_retire(a);
    owner_retire(other);
}

static void test_unknown_or_double_retire_refused(void) {
    UINT32 a = owner_new();
    CHECK(owner_retire(a) == 0);
    CHECK(owner_retire(a) == -1);                /* already retired */
    CHECK(owner_retire(XOWNER_FIRST_INSTANCE + 60) == -1); /* never live */
}

int main(void) {
    xtok_init();
    owner_init();
    test_ids_are_fresh_and_unfixed();
    test_reclaim_is_per_instance();
    test_retired_id_comes_back_only_after_retire();
    test_unknown_or_double_retire_refused();
    printf("owner: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
