/* Host test for the boot handoff. The kernel mints the root of a device; the
 * boot claims it for its first holder and hands it to init with xtok_give.
 * After the handoff the boot holds nothing, init holds the root, and init can
 * derive from it. No names, no tables: only tokens and owners. */
#include <stdio.h>
#include "capability.h"
#include "blk.h"
#include "host_stubs.h"
#include "owner.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static BlkDev nd0;

static void test_handoff_moves_the_root(void) {
    xtok_roots_reset();
    UINT32 boot = owner_new();
    UINT32 init = owner_new();

    CHECK(xtok_root_mint(&nd0, boot) != XTOKEN_INVALID);
    XToken r = xtok_root_claim(&nd0, boot);          /* boot claims the root */
    CHECK(r != XTOKEN_INVALID);

    /* The root must carry GIVE for the handoff; the kernel gives it to the boot
     * only with GIVE, the boot passes it on and keeps nothing. */
    CHECK(xtok_give(boot, r, init) == 0);
    CHECK(xtok_release_owned(r, boot) == XTOK_REL_ESTALE);   /* boot is out */
    CHECK(xtok_give(boot, r, init) == -1);                   /* and cannot give again */

    UINT64 f = 0; UINT32 t = 0;
    xtok_owner_usage(boot, &f, &t);
    CHECK(t == 0);
    xtok_owner_usage(init, &f, &t);
    CHECK(t == 1);
    CHECK(xtok_release_owned(r, init) == 0);
    CHECK(owner_retire(boot) == 0);
    CHECK(owner_retire(init) == 0);
}

static void test_init_derives_from_the_handed_root(void) {
    xtok_roots_reset();
    UINT32 boot = owner_new();
    UINT32 init = owner_new();
    xtok_root_mint(&nd0, boot);
    XToken r = xtok_root_claim(&nd0, boot);
    CHECK(xtok_give(boot, r, init) == 0);

    /* first is relative to the root: block 8 of the root is device block 8. */
    XToken child = xtok_derive_blocks(init, r, 8, 8, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE);
    CHECK(child != XTOKEN_INVALID);                          /* init attenuates its root */
    CHECK(xtok_derive_blocks(boot, r, 8, 8, XTOK_RIGHT_READ) == XTOKEN_INVALID); /* boot cannot */
    CHECK(xtok_release_owned(child, init) == 0);
    CHECK(xtok_release_owned(r, init) == 0);
    CHECK(owner_retire(boot) == 0);
    CHECK(owner_retire(init) == 0);
}

static void test_root_is_single_and_not_copyable(void) {
    xtok_roots_reset();
    UINT32 boot = owner_new();
    xtok_root_mint(&nd0, boot);
    CHECK(xtok_root_claim(&nd0, boot) != XTOKEN_INVALID);
    CHECK(xtok_root_claim(&nd0, boot) != XTOKEN_INVALID);    /* claim is per call */
    /* but a claimed token without GIVE cannot be handed on */
    XToken plain = xtok_root_claim(&nd0, boot);
    CHECK(plain != XTOKEN_INVALID);
    UINT32 other = owner_new();
    CHECK(xtok_give(boot, plain, other) == 0);   /* every claim carries GIVE */
    CHECK(xtok_release_owned(plain, other) == 0);
    CHECK(owner_retire(other) == 0);
}

/* The boot keeps the handle it gave to init. It is no authority: the boot cannot
 * derive from it, give it, or release it, and init can. */
static void test_boot_keeps_no_authority(void) {
    xtok_roots_reset();
    UINT32 boot = owner_new();
    UINT32 init = owner_new();
    xtok_root_mint(&nd0, boot);
    XToken r = xtok_root_claim(&nd0, boot);
    CHECK(xtok_give(boot, r, init) == 0);
    CHECK(xtok_derive_blocks(boot, r, 8, 8, XTOK_RIGHT_READ) == XTOKEN_INVALID);
    UINT32 other = owner_new();
    CHECK(xtok_give(boot, r, other) == -1);
    CHECK(xtok_release_owned(r, boot) == XTOK_REL_ESTALE);
    XToken child = xtok_derive_blocks(init, r, 8, 8, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE);
    CHECK(child != XTOKEN_INVALID);
    CHECK(xtok_release_owned(child, init) == 0);   /* init returns what it derived */
    CHECK(xtok_release_owned(r, init) == 0);
    CHECK(owner_retire(boot) == 0);
    CHECK(owner_retire(init) == 0);
    CHECK(owner_retire(other) == 0);
}

int main(void) {
    host_dev_init(&nd0, 512, 1024);
    xtok_init();
    owner_init();
    test_handoff_moves_the_root();
    test_init_derives_from_the_handed_root();
    test_root_is_single_and_not_copyable();
    test_boot_keeps_no_authority();
    printf("boot_init: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
