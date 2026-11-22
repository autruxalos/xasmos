/* revoke_test: subtree revocation through the public API. Covers reach through
 * delegated and unreleasable children, refusal on a frozen member with nothing
 * changed, foreign and unknown tokens, a root without RELEASE, and no live
 * lineage left behind. */
#include <stdio.h>
#include <string.h>
#include "capability.h"
#include "blk.h"
#include "owner.h"
#include "endpoint.h"
#include "derive_call.h"
#include "host_stubs.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define GIVEABLE (XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_RELEASE | XTOK_RIGHT_GIVE)
static BlkDev dev;

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

/* R1: reach. A child delegated to another owner, and a grandchild, both die with
 * the root. The first child has no RELEASE, so only the root's holder can end it. */
static void r1_reaches_delegated_and_unreleasable(void) {
    UINT32 a = owner_new(), b = owner_new();
    XToken root = xtok_alloc_blocks(a, &dev, 0, 64, GIVEABLE);
    XToken c1 = XTOKEN_INVALID, c2 = XTOKEN_INVALID, got = XTOKEN_INVALID;
    XToken view_buf_tok;
    XTokenView v[128];
    UINT32 nv;
    CHECK(root != XTOKEN_INVALID);
    CHECK(xsys_derive(a, root, 0, 16, XTOK_RIGHT_READ | XTOK_RIGHT_GIVE, &c1) == XDERIVE_OK);
    CHECK(xsys_derive(a, c1, 0, 4, XTOK_RIGHT_READ, &c2) == XDERIVE_OK);
    CHECK(transfer(a, c1, b, &got) == 0 && got == c1);
    CHECK(xtok_revoke(root, a) == 0);
    CHECK(xtok_release_owned(c1, b) == XTOK_REL_ESTALE);
    CHECK(xtok_release_owned(c2, a) == XTOK_REL_ESTALE);
    CHECK(xtok_release_owned(root, a) == XTOK_REL_ESTALE);
    nv = xtok_inspect_all(v, 128);
    for (UINT32 i = 0; i < nv; i++) {
        CHECK(v[i].parent != root && v[i].parent != c1 && v[i].parent != c2);
    }
    (void)view_buf_tok;
    CHECK(owner_retire(a) == 0);
    CHECK(owner_retire(b) == 0);
}

/* R2: a frozen member blocks the whole revocation. Nothing changes: the root is
 * still usable. After the offer is cancelled, the revocation goes through. */
static void r2_frozen_member_blocks_all(void) {
    UINT32 a = owner_new(), b = owner_new();
    XToken root = xtok_alloc_blocks(a, &dev, 0, 64, GIVEABLE), c1 = XTOKEN_INVALID, d = XTOKEN_INVALID;
    XToken er = XTOKEN_INVALID, es = XTOKEN_INVALID;
    CHECK(xsys_derive(a, root, 0, 8, XTOK_RIGHT_READ | XTOK_RIGHT_GIVE, &c1) == XDERIVE_OK);
    CHECK(xep_create(b, &er) == 0);
    CHECK(xep_send_cap(b, er, a, &es) == 0);
    CHECK(xtok_offer(a, c1, es) == 0);
    CHECK(xtok_revoke(root, a) == XTOK_REL_EBUSY);
    CHECK(xsys_derive(a, root, 0, 1, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE, &d) == XDERIVE_OK); /* root alive */
    CHECK(xtok_release_owned(d, a) == 0);
    CHECK(xtok_cancel(a, c1) == 0);
    CHECK(xtok_revoke(root, a) == 0);
    CHECK(xtok_release_owned(es, a) == 0);
    CHECK(xep_destroy(b, er) == 0);
    CHECK(owner_retire(a) == 0);
    CHECK(owner_retire(b) == 0);
}

/* R3: refusals that name nothing. A foreign token and an invalid one are stale. */
static void r3_foreign_and_invalid_are_stale(void) {
    UINT32 a = owner_new(), b = owner_new();
    XToken root = xtok_alloc_blocks(a, &dev, 0, 8, GIVEABLE);
    CHECK(xtok_revoke(root, b) == XTOK_REL_ESTALE);
    CHECK(xtok_revoke(XTOKEN_INVALID, a) == XTOK_REL_ESTALE);
    CHECK(xtok_release_owned(root, a) == 0);
    CHECK(owner_retire(a) == 0);
    CHECK(owner_retire(b) == 0);
}

/* R4: a root without RELEASE cannot be revoked by its holder. */
static void r4_root_needs_release(void) {
    UINT32 a = owner_new();
    XToken root = xtok_alloc_blocks(a, &dev, 0, 8, XTOK_RIGHT_READ | XTOK_RIGHT_GIVE);
    CHECK(root != XTOKEN_INVALID);
    CHECK(xtok_revoke(root, a) == XTOK_REL_ERIGHTS);
    CHECK(xtok_reclaim(root) == 0);
    CHECK(owner_retire(a) == 0);
}

int main(void) {
    host_dev_init(&dev, 512, 1024);
    xtok_init();
    owner_init();
    UINT32 base = xtok_live_count();

    r1_reaches_delegated_and_unreleasable();
    r2_frozen_member_blocks_all();
    r3_foreign_and_invalid_are_stale();
    r4_root_needs_release();
    CHECK(xtok_live_count() == base);

    printf("revoke: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
