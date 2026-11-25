/* exokernel_system_test: properties the mechanism layer must keep, checked
 * through the public API only. A program's view: it holds tokens, derives,
 * offers and accepts; it never names a device. Implemented properties are
 * CHECKs. Properties of phases not yet built are PENDING and become CHECKs
 * when their XTOK_HAS_* macro is defined by the phase that adds the API. */
#include <stdio.h>
#include <string.h>
#include "capability.h"
#include "blk.h"
#include "owner.h"
#include "endpoint.h"
#include "derive_call.h"
#include "xring_table.h"
#include "budget.h"
#include "host_stubs.h"

static int checks, failures, pending;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define PENDING(phase, what) do { pending++; \
    printf("PENDING %s: %s\n", phase, what); } while (0)

#define GIVEABLE (XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_RELEASE | XTOK_RIGHT_GIVE)
static BlkDev dev;

/* The only way titularity moves: offer by the giver, accept by the receiver. */
static int transfer(UINT32 from, XToken t, UINT32 to, XToken *got) {
    XToken ep_recv = XTOKEN_INVALID, ep_send = XTOKEN_INVALID;
    int rc = -1;
    *got = XTOKEN_INVALID;
    if (xep_create(to, &ep_recv) != 0) return -1;
    if (xep_send_cap(to, ep_recv, from, &ep_send) == 0) {
        if (xtok_offer(from, t, ep_send) == 0)
            rc = xtok_accept(to, ep_recv, got);
        xtok_release_owned(ep_send, from);
    }
    xep_destroy(to, ep_recv);
    return rc;
}

/* P1: attenuation is exact. A child is granted exactly when its range fits the
 * parent and its rights are a subset of the parent's. Swept over a grid. */
static void p1_attenuation_is_exact(void) {
    UINT32 a = owner_new();
    XToken root = xtok_alloc_blocks(a, &dev, 0, 64, GIVEABLE);
    static const UINT32 masks[] = {
        XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE,
        XTOK_RIGHT_WRITE | XTOK_RIGHT_RELEASE,
        XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_RELEASE,
        XTOK_RIGHT_MAP | XTOK_RIGHT_RELEASE,   /* MAP is not held: refused */
    };
    CHECK(root != XTOKEN_INVALID);
    for (UINT64 f = 0; f <= 70; f += 7)
        for (UINT64 n = 0; n <= 70; n += 5)
            for (unsigned k = 0; k < sizeof masks / sizeof masks[0]; k++) {
                XToken c = XTOKEN_INVALID;
                int want = (n > 0 && f + n <= 64) && ((masks[k] & ~GIVEABLE) == 0);
                int got = xsys_derive(a, root, f, n, masks[k], &c);
                CHECK((got == XDERIVE_OK) == want);
                if (got == XDERIVE_OK) CHECK(xtok_release_owned(c, a) == 0);
            }
    CHECK(xtok_release_owned(root, a) == 0);
    CHECK(owner_retire(a) == 0);
}

/* P2: a token offered for transfer is inert until accept or cancel. */
static void p2_frozen_token_is_inert(void) {
    UINT32 g = owner_new(), r = owner_new();
    XToken t = xtok_alloc_blocks(g, &dev, 0, 8, GIVEABLE), d = XTOKEN_INVALID;
    XToken ram = xtok_alloc_ram(g, 1, XTOK_RIGHTS_RAM_DEFAULT);
    XToken ep_recv = XTOKEN_INVALID, ep_send = XTOKEN_INVALID;
    CHECK(xep_create(r, &ep_recv) == 0);
    CHECK(xep_send_cap(r, ep_recv, g, &ep_send) == 0);
    CHECK(xtok_offer(g, t, ep_send) == 0);
    CHECK(xsys_derive(g, t, 0, 4, XTOK_RIGHT_READ, &d) == XDERIVE_REFUSED);
    CHECK(xtok_release_owned(t, g) == XTOK_REL_EBUSY);
    CHECK(xtok_map_acquire(t, g, XTOK_RIGHT_READ, 0, 0) != XTOK_MAP_OK);
    CHECK(xtok_ring_pin(g, t, ram, XTOK_BLK_READ, 0, 1) == -1);
    CHECK(xtok_offer(g, t, ep_send) == -1);          /* one offer at a time */
    CHECK(xtok_cancel(g, t) == 0);
    CHECK(xtok_release_owned(t, g) == 0);            /* usable again */
    CHECK(xtok_release_owned(ep_send, g) == 0);
    CHECK(xep_destroy(r, ep_recv) == 0);
    CHECK(xtok_release_owned(ram, g) == 0);
    CHECK(owner_retire(g) == 0);
    CHECK(owner_retire(r) == 0);
}

/* P3: no copy. After accept the giver has nothing; the receiver has everything. */
static void p3_transfer_moves_not_copies(void) {
    UINT32 g = owner_new(), r = owner_new(), x = owner_new();
    XToken t = xtok_alloc_blocks(g, &dev, 0, 16, GIVEABLE), got = XTOKEN_INVALID, d = XTOKEN_INVALID;
    CHECK(transfer(g, t, r, &got) == 0 && got == t);
    CHECK(xsys_derive(g, t, 0, 4, XTOK_RIGHT_READ, &d) == XDERIVE_REFUSED);
    CHECK(xtok_release_owned(t, g) == XTOK_REL_ESTALE);
    CHECK(transfer(g, t, x, &got) != 0);             /* giver cannot offer it again */
    CHECK(xsys_derive(r, t, 0, 4, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE, &d) == XDERIVE_OK);
    CHECK(xtok_release_owned(d, r) == 0);
    CHECK(xtok_release_owned(t, r) == 0);
    CHECK(owner_retire(g) == 0);
    CHECK(owner_retire(r) == 0);
    CHECK(owner_retire(x) == 0);
}

/* P4: the unilateral path is closed. No accept without an offer, no offer
 * through a RECV, no offer by a non-holder of the SEND, no offer without GIVE. */
static void p4_no_unilateral_path(void) {
    UINT32 g = owner_new(), r = owner_new(), o = owner_new();
    XToken t = xtok_alloc_blocks(g, &dev, 0, 8, GIVEABLE), x = XTOKEN_INVALID;
    XToken t2 = xtok_alloc_blocks(g, &dev, 0, 8, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE);
    XToken ep_recv = XTOKEN_INVALID, ep_send = XTOKEN_INVALID;
    CHECK(xep_create(r, &ep_recv) == 0);
    CHECK(xep_send_cap(r, ep_recv, g, &ep_send) == 0);
    CHECK(xtok_accept(r, ep_recv, &x) == -1);        /* nothing offered */
    CHECK(xtok_offer(g, t, ep_recv) == -1);          /* RECV is not a SEND */
    CHECK(xtok_offer(o, t, ep_send) == -1);          /* not the SEND's holder */
    CHECK(xtok_offer(g, t2, ep_send) == -1);         /* no GIVE on t2 */
    CHECK(xtok_accept(g, ep_send, &x) == -1);        /* a SEND cannot accept */
    CHECK(xtok_release_owned(t2, g) == 0);
    CHECK(xtok_release_owned(t, g) == 0);
    CHECK(xtok_release_owned(ep_send, g) == 0);
    CHECK(xep_destroy(r, ep_recv) == 0);
    CHECK(owner_retire(g) == 0);
    CHECK(owner_retire(r) == 0);
    CHECK(owner_retire(o) == 0);
}

/* P5: no ambient authority. A third owner gets nothing from a token's mere
 * existence: no derivation, no release, no mapping, no pin. */
static void p5_no_ambient_authority(void) {
    UINT32 g = owner_new(), o = owner_new();
    XToken t = xtok_alloc_blocks(g, &dev, 0, 8, GIVEABLE), d = XTOKEN_INVALID;
    XToken ram = xtok_alloc_ram(o, 1, XTOK_RIGHTS_RAM_DEFAULT);
    CHECK(xsys_derive(o, t, 0, 4, XTOK_RIGHT_READ, &d) == XDERIVE_REFUSED);
    CHECK(xtok_release_owned(t, o) != 0);
    CHECK(xtok_map_acquire(t, o, XTOK_RIGHT_READ, 0, 0) != XTOK_MAP_OK);
    CHECK(xtok_ring_pin(o, t, ram, XTOK_BLK_READ, 0, 1) == -1);
    CHECK(xtok_release_owned(ram, o) == 0);
    CHECK(xtok_release_owned(t, g) == 0);
    CHECK(owner_retire(g) == 0);
    CHECK(owner_retire(o) == 0);
}

/* P6: a delegated token serves the ring path for its new holder, and its range
 * stays bounded. */
static void p6_delegated_token_serves_ring(void) {
    UINT32 g = owner_new(), r = owner_new();
    XToken t = xtok_alloc_blocks(g, &dev, 16, 8, GIVEABLE), got = XTOKEN_INVALID;
    XToken ram = xtok_alloc_ram(r, 1, XTOK_RIGHTS_RAM_DEFAULT);
    CHECK(transfer(g, t, r, &got) == 0);
    CHECK(xtok_ring_pin(r, got, ram, XTOK_BLK_READ, 0, 8) == 0);
    xtok_ring_unpin(got, ram);
    CHECK(xtok_ring_pin(r, got, ram, XTOK_BLK_READ, 0, 9) == -1);
    xtok_ring_unpin(got, ram);
    CHECK(xtok_release_owned(ram, r) == 0);
    CHECK(xtok_release_owned(got, r) == 0);
    CHECK(owner_retire(g) == 0);
    CHECK(owner_retire(r) == 0);
}

/* P8: revocation reaches the whole lineage. A child delegated to another owner,
 * without RELEASE, dies with its root, and no one can use it afterwards. */
static void p8_revocation_reaches_lineage(void) {
    UINT32 a = owner_new(), b = owner_new();
    XToken root = xtok_alloc_blocks(a, &dev, 0, 64, GIVEABLE), c = XTOKEN_INVALID, got = XTOKEN_INVALID;
    CHECK(root != XTOKEN_INVALID);
    CHECK(xsys_derive(a, root, 0, 8, XTOK_RIGHT_READ | XTOK_RIGHT_GIVE, &c) == XDERIVE_OK);
    CHECK(transfer(a, c, b, &got) == 0 && got == c);
    CHECK(xtok_revoke(root, a) == 0);
    CHECK(xtok_release_owned(c, b) == XTOK_REL_ESTALE);
    CHECK(xtok_release_owned(root, a) == XTOK_REL_ESTALE);
    CHECK(owner_retire(a) == 0);
    CHECK(owner_retire(b) == 0);
}

/* P9: memory is paid from a budget the caller holds. A foreign budget, no budget,
 * or more frames than the balance are refused; a paid RAM token refunds on release. */
static void p9_memory_is_paid_from_a_budget(void) {
    UINT32 a = owner_new(), o = owner_new();
    XToken bud = XTOKEN_INVALID, ram = XTOKEN_INVALID, x = XTOKEN_INVALID;
    UINT64 bal = 0;
    CHECK(xtok_budget_mint(a, 4, &bud) == 0);
    CHECK(xtok_alloc_ram_from(o, bud, 1, XTOK_RIGHTS_RAM_DEFAULT, &x) != 0);
    CHECK(x == XTOKEN_INVALID);
    CHECK(xtok_alloc_ram_from(a, XTOKEN_INVALID, 1, XTOK_RIGHTS_RAM_DEFAULT, &x) != 0);
    CHECK(xtok_alloc_ram_from(a, bud, 5, XTOK_RIGHTS_RAM_DEFAULT, &x) != 0);
    CHECK(xtok_budget_balance(bud, a, &bal) == 0 && bal == 4);
    CHECK(xtok_alloc_ram_from(a, bud, 3, XTOK_RIGHTS_RAM_DEFAULT, &ram) == 0);
    CHECK(xtok_budget_balance(bud, a, &bal) == 0 && bal == 1);
    CHECK(xtok_release_owned(ram, a) == 0);
    CHECK(xtok_budget_balance(bud, a, &bal) == 0 && bal == 4);
    CHECK(xtok_release_owned(bud, a) == 0);
    CHECK(owner_retire(a) == 0);
    CHECK(owner_retire(o) == 0);
}

/* P10: a shell's share can be offered only through the shell's SEND, and only by
 * the owner of the token. A program holding a budget and no SEND cannot give it
 * away, and a RECV is not a SEND. */
static void p10_offer_needs_the_send(void) {
    UINT32 sh = owner_new(), in = owner_new(), pr = owner_new();
    XToken rv = XTOKEN_INVALID, sd = XTOKEN_INVALID, b = XTOKEN_INVALID, c = XTOKEN_INVALID;
    UINT64 bal = 0;
    CHECK(xep_create(sh, &rv) == 0);
    CHECK(xep_send_cap(sh, rv, in, &sd) == 0);
    CHECK(xtok_budget_mint(in, 100, &b) == 0);
    CHECK(xtok_budget_derive(in, b, 40, &c) == 0);
    CHECK(xtok_offer(pr, c, sd) == -1);      /* a program without the SEND */
    CHECK(xtok_offer(in, c, rv) == -1);      /* a RECV is not a SEND */
    CHECK(xtok_offer(in, c, sd) == 0);
    CHECK(xtok_cancel(in, c) == 0);
    CHECK(xtok_release_owned(c, in) == 0);   /* the balance returns to b */
    CHECK(xtok_budget_balance(b, in, &bal) == 0 && bal == 100);
    CHECK(xtok_release_owned(sd, in) == 0);
    CHECK(xep_destroy(sh, rv) == 0);
    CHECK(xtok_release_owned(b, in) == 0);
    CHECK(owner_retire(sh) == 0);
    CHECK(owner_retire(in) == 0);
    CHECK(owner_retire(pr) == 0);
}

/* P11: the lifecycle of one run. The shell lends a child budget to an instance
 * owner; the program spends from it; the reap (reclaim until empty) gives every
 * frame back and the shell's balance returns exactly. The owner retires empty. */
static void p11_instance_lifecycle_returns_the_balance(void) {
    UINT32 sh = owner_new(), inst = owner_new();
    XToken shb = XTOKEN_INVALID, child = XTOKEN_INVALID, got = XTOKEN_INVALID, ram = XTOKEN_INVALID;
    XToken list[16];
    UINT64 bal = 0;
    UINT32 n = 0, progress = 0, i;
    CHECK(xtok_budget_mint(sh, 8192, &shb) == 0);
    CHECK(xtok_budget_derive(sh, shb, 1024, &child) == 0);
    CHECK(xep_transfer(sh, child, inst, &got) == 0 && got == child);
    CHECK(xtok_alloc_ram_from(inst, got, 10, XTOK_RIGHTS_RAM_DEFAULT, &ram) == 0);
    CHECK(xtok_budget_balance(shb, sh, &bal) == 0 && bal == 7168);
    do {
        n = xtok_owner_tokens(inst, list, 16);
        progress = 0;
        for (i = 0; i < n; i++)
            if (xtok_reclaim(list[i]) == 0) progress++;
    } while (progress > 0 && n > 0);
    CHECK(xtok_budget_balance(shb, sh, &bal) == 0 && bal == 8192);
    CHECK(xtok_budget_balance(got, inst, &bal) != 0);   /* the child is gone */
    CHECK(owner_retire(inst) == 0);
    CHECK(xtok_release_owned(shb, sh) == 0);
    CHECK(owner_retire(sh) == 0);
}

/* P12: disk access is delegated, never named. The shell derives a lane from its
 * disk root and hands only that lane over. The program reaches its lane and
 * nothing beyond it, it does not hold the root, and the shell loses the lane. */
#define LANE_RIGHTS (XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE | XTOK_RIGHT_GIVE)
static void p12_disk_is_delegated_not_named(void) {
    UINT32 sh = owner_new(), pr = owner_new();
    XToken root = xtok_alloc_blocks(sh, &dev, 0, 64, GIVEABLE);
    XToken lane = XTOKEN_INVALID, got = XTOKEN_INVALID, d1 = XTOKEN_INVALID, d2 = XTOKEN_INVALID;
    CHECK(root != XTOKEN_INVALID);
    CHECK(xsys_derive(sh, root, 0, 8, LANE_RIGHTS, &lane) == XDERIVE_OK);
    CHECK(transfer(sh, lane, pr, &got) == 0 && got == lane);
    CHECK(xsys_derive(pr, got, 0, 8, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE, &d1) == XDERIVE_OK);
    CHECK(xsys_derive(pr, got, 0, 9, XTOK_RIGHT_READ, &d2) == XDERIVE_REFUSED);
    CHECK(xsys_derive(pr, root, 0, 1, XTOK_RIGHT_READ, &d2) == XDERIVE_REFUSED);
    CHECK(xsys_derive(sh, got, 0, 1, XTOK_RIGHT_READ, &d2) == XDERIVE_REFUSED);
    CHECK(xtok_release_owned(d1, pr) == 0);
    CHECK(xtok_release_owned(got, pr) == 0);
    CHECK(xtok_release_owned(root, sh) == 0);
    CHECK(owner_retire(sh) == 0);
    CHECK(owner_retire(pr) == 0);
}

/* P13: a ring costs memory. No budget, no ring. With a budget, the ring's frame is
 * charged, and the balance returns whole when the ring is closed and reaped. */
static void p13_rings_are_paid_from_a_budget(void) {
    UINT32 o = owner_new();
    XToken bud = XTOKEN_INVALID, rt = XTOKEN_INVALID;
    UINT64 bal = 0;
    CHECK(xtok_budget_mint(o, 4, &bud) == 0);
    CHECK(xrt_create(o, XTOKEN_INVALID, 16, &rt) == XRT_E_NOMEM);   /* no budget, no ring */
    CHECK(xrt_create(o, bud, 16, &rt) > 0);
    CHECK(xtok_budget_balance(bud, o, &bal) == 0 && bal < 4);
    CHECK(xrt_destroy_owner(o) >= 1);
    xrt_reap();
    CHECK(xtok_budget_balance(bud, o, &bal) == 0 && bal == 4);      /* whole again */
    CHECK(xtok_release_owned(bud, o) == 0);
    CHECK(owner_retire(o) == 0);
}

/* P13: a ring costs memory. No budget, no ring. With a budget, the ring's frame is
 * charged, and the balance returns whole when the ring is closed and reaped. */
/* Ring-table hooks and a reap, as in exo_full_test.c. */
static int p_bound[32];
static void p_unbind(uint32_t id) { if (id < 32) p_bound[id] = 0; }
static int p_bind(uint32_t id, void (*fn)(void *, uint32_t, int32_t), void *arg) {
    (void)fn; (void)arg;
    if (id == 0 || id >= 32 || p_bound[id]) return -1;
    p_bound[id] = 1;
    return 0;
}
int main(int argc, char **argv) {
    int strict = argc > 1 && strcmp(argv[1], "--strict") == 0;
    host_dev_init(&dev, 512, 1024);
    xtok_init();
    xrt_init(p_unbind, p_bind);
    owner_init();
    UINT32 base = xtok_live_count();

    p1_attenuation_is_exact();
    p2_frozen_token_is_inert();
    p3_transfer_moves_not_copies();
    p4_no_unilateral_path();
    p5_no_ambient_authority();
    p6_delegated_token_serves_ring();
    p8_revocation_reaches_lineage();
    p9_memory_is_paid_from_a_budget();
    p10_offer_needs_the_send();
    p11_instance_lifecycle_returns_the_balance();
    p12_disk_is_delegated_not_named();
    p13_rings_are_paid_from_a_budget();
    CHECK(xtok_live_count() == base);                /* P7: nothing leaked */


    printf("exokernel: %d checks, %d failures, %d pending\n", checks, failures, pending);
    if (failures) return 1;
    if (strict && pending) return 2;
    return 0;
}
