/* Host test for the ring table: ids, per-owner cap, entry limits, ownership,
 * FREE refused while a ring lives, and an id not reused until its ring is
 * released. Runs against the real xring.c, xring_table.c and token ledger. */
#include "../../../source/arch/amd64/kernel/xeret/xring.c"
#include "../../../source/arch/amd64/kernel/xeret/xring_table.c"
#include <stdio.h>
#include <string.h>
#include "host_stubs.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define OWNER 2u
#define OTHER 3u

static BlkDev dev;

/* Address spaces do not exist in this test: revoking is a no-op here, and the
 * mapping path is exercised by the kernel itself. */
int xas_revoke_token(XToken tok) { (void)tok; return 0; }

static int bound[32];
static int bind_fail;
static void (*bound_fn[32])(void *, uint32_t, int32_t);
static void *bound_arg[32];

static int  unbind_calls;
static int  live_at_unbind;
static XToken probe_tok;
static void unbind_hook(uint32_t id) {
    (void)id;
    unbind_calls++;
    bound[id] = 0;
    live_at_unbind = (xtok_ram_info(probe_tok, 0, 0) == 0);
}

/* Asynchronous device: takes every entry and completes none. The ring keeps
 * them in flight, which is what the release-order checks need. */
static int async_submit(void *ctx, const BlkReq *req) {
    (void)ctx; (void)req;
    return 0;
}
static const BlkOps async_ops = { NULL, NULL, NULL, async_submit };

static int bind_hook(uint32_t id, void (*fn)(void *, uint32_t, int32_t), void *arg) {
    if (bind_fail) return -1;
    if (id == 0 || id >= 32 || bound[id]) return -1;
    bound[id] = 1; bound_fn[id] = fn; bound_arg[id] = arg;
    return 0;
}

static void fresh(void) {
    memset(bound, 0, sizeof bound);
    bind_fail = 0;
    xrt_init(unbind_hook, bind_hook);
    unbind_calls = 0;
    live_at_unbind = 0;
    probe_tok = XTOKEN_INVALID;
}

static XqSqe ent(uint64_t cookie) {
    XqSqe e;
    memset(&e, 0, sizeof e);
    e.cookie    = cookie;
    e.blk_token = xtok_alloc_blocks(OWNER, &dev, 0, 8, XTOK_RIGHTS_BLOCKS_DEFAULT);
    e.ram_token = xtok_alloc_ram(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    e.count = 1;
    e.op = XQ_OP_READ;
    return e;
}

static void submit(int id, XqSqe e) {
    KRing *r = xrt_ring((uint32_t)id);
    XqHeader *h = (XqHeader *)r->mem;
    XqSqe *s = (XqSqe *)((uint8_t *)r->mem + xq_sq_off()) + (h->sq_tail & h->mask);
    *s = e;
    h->sq_tail = h->sq_tail + 1u;
}

/* Ends a test cleanly: completes what is in flight, destroys, reaps. */
static void finish(int id, UINT32 owner) {
    KRing *r = xrt_ring((uint32_t)id);
    if (r) {
        for (uint32_t s = 0; s < r->entries; s++)
            if (r->blk[s] != XTOKEN_INVALID) kr_complete(r, s, 0);
    }
    xrt_destroy(owner, xrt_token((uint32_t)id));
    xrt_reap();
}

/* ---- allocation ------------------------------------------------------------ */

static void test_ids_are_distinct_and_start_at_one(void) {
    fresh();
    xrt_set_cap(OWNER, 8);
    XToken t1 = XTOKEN_INVALID, t2 = XTOKEN_INVALID;
    int a = xrt_create(OWNER, 16, &t1);
    int b = xrt_create(OWNER, 16, &t2);
    CHECK(a == 1);                                  /* 0 is the boot ring's id */
    CHECK(b == 2);
    CHECK(t1 != XTOKEN_INVALID && t2 != XTOKEN_INVALID && t1 != t2);
    CHECK(xtok_ram_info(t1, 0, 0) == 0);
    CHECK(xrt_ring((uint32_t)a) != 0 && xrt_ring((uint32_t)b) != 0);
    finish(a, OWNER);
    finish(b, OWNER);
}

static void test_entry_limits(void) {
    fresh();
    xrt_set_cap(OWNER, 8);
    XToken t = XTOKEN_INVALID;
    CHECK(xrt_create(OWNER, 0,   &t) == XRT_E_ARG);  /* zero */
    CHECK(xrt_create(OWNER, 15,  &t) == XRT_E_ARG);  /* below minimum */
    CHECK(xrt_create(OWNER, 17,  &t) == XRT_E_ARG);  /* not a power of two */
    CHECK(xrt_create(OWNER, 512, &t) == XRT_E_ARG);  /* above the 256 limit */
    int big = xrt_create(OWNER, 256, &t);
    CHECK(big > 0);                                  /* the limit itself */
    finish(big, OWNER);
}

/* ---- cap per owner -------------------------------------------------------- */

static void test_cap_is_per_owner(void) {
    fresh();
    xrt_set_cap(OWNER, 2);
    xrt_set_cap(OTHER, 2);
    XToken t[4];
    int a = xrt_create(OWNER, 16, &t[0]);
    int b = xrt_create(OWNER, 16, &t[1]);
    CHECK(a > 0 && b > 0);
    CHECK(xrt_create(OWNER, 16, &t[2]) == XRT_E_CAP);
    CHECK(xrt_owner_count(OWNER) == 2);
    int c = xrt_create(OTHER, 16, &t[3]);            /* other owner unaffected */
    CHECK(c > 0);
    CHECK(xrt_owner_count(OTHER) == 1);
    finish(a, OWNER); finish(b, OWNER); finish(c, OTHER);
}

static void test_zero_cap_by_default(void) {
    fresh();
    XToken t = XTOKEN_INVALID;
    CHECK(xrt_create(OWNER, 16, &t) == XRT_E_CAP);   /* no cap set: no rings */
}

/* ---- ownership ------------------------------------------------------------- */

static void test_foreign_token_is_refused(void) {
    fresh();
    xrt_set_cap(OWNER, 4);
    XToken t = XTOKEN_INVALID;
    int id = xrt_create(OWNER, 16, &t);
    CHECK(id > 0);
    CHECK(xrt_destroy(OTHER, t) == -1);
    CHECK(xrt_ring((uint32_t)id)->closing == 0);
    CHECK(xtok_ram_info(t, 0, 0) == 0);
    finish(id, OWNER);
}

/* ---- FREE refused while the ring lives ------------------------------------- */

static void test_free_refused_while_alive(void) {
    fresh();
    xrt_set_cap(OWNER, 4);
    XToken t = XTOKEN_INVALID;
    int id = xrt_create(OWNER, 16, &t);
    CHECK(id > 0);
    CHECK(xtok_release(t) == XTOK_REL_EBUSY);        /* the table's retention */
    CHECK(xtok_ram_info(t, 0, 0) == 0);
    finish(id, OWNER);
}

/* ---- id reuse and release order -------------------------------------------- */

static void test_id_not_reused_until_released(void) {
    fresh();
    xrt_set_cap(OWNER, 8);
    XToken ta = XTOKEN_INVALID, tb = XTOKEN_INVALID, tc = XTOKEN_INVALID, td = XTOKEN_INVALID;
    int a = xrt_create(OWNER, 16, &ta);
    int b = xrt_create(OWNER, 16, &tb);
    CHECK(a == 1 && b == 2);
    probe_tok = ta;

    /* A has an entry in flight: destroying it must not free id 1. */
    XqSqe e = ent(1);
    submit(a, e);
    CHECK(kr_process(xrt_ring((uint32_t)a)) == 1);
    CHECK(xrt_destroy(OWNER, ta) == 0);
    xrt_reap();
    CHECK(xrt_ring((uint32_t)a) != 0);
    CHECK(unbind_calls == 0);
    int c = xrt_create(OWNER, 16, &tc);
    CHECK(c == 3);                                   /* id 1 is not handed out */

    /* The last completion arrives: the ring is released, id 1 is free. */
    KRing *ra = xrt_ring((uint32_t)a);
    uint32_t slot = 0;
    for (uint32_t s = 0; s < ra->entries; s++)
        if (ra->blk[s] != XTOKEN_INVALID) slot = s;
    CHECK(kr_complete(ra, slot, 0) == 0);
    xrt_reap();
    CHECK(xrt_ring((uint32_t)a) == 0);
    CHECK(unbind_calls == 1);
    CHECK(live_at_unbind == 1);                      /* unbound while the token lived */
    CHECK(xtok_ram_info(ta, 0, 0) != 0);             /* and now it is gone */

    int d = xrt_create(OWNER, 16, &td);
    CHECK(d == 1);                                   /* first free id is reused */
    xtok_release(e.blk_token);
    xtok_release(e.ram_token);
    finish(b, OWNER); finish(c, OWNER); finish(d, OWNER);
}

static void test_cap_returns_on_release(void) {
    fresh();
    xrt_set_cap(OWNER, 1);
    XToken t1 = XTOKEN_INVALID, t2 = XTOKEN_INVALID;
    int a = xrt_create(OWNER, 16, &t1);
    CHECK(a > 0);
    CHECK(xrt_create(OWNER, 16, &t2) == XRT_E_CAP);
    CHECK(xrt_destroy(OWNER, t1) == 0);
    xrt_reap();                                      /* nothing in flight: released */
    CHECK(xrt_owner_count(OWNER) == 0);
    int b = xrt_create(OWNER, 16, &t2);
    CHECK(b > 0);
    finish(b, OWNER);
}

/* ---- lookup: a token names a ring only for its owner ---------------------- */

static void test_lookup_is_owner_checked(void) {
    fresh();
    xrt_set_cap(OWNER, 4);
    XToken t = XTOKEN_INVALID;
    int id = xrt_create(OWNER, 16, &t);
    CHECK(id > 0);
    CHECK(xrt_lookup(OWNER, t) == id);
    CHECK(xrt_lookup(OTHER, t) == -1);                /* foreign owner: same answer as unknown */
    CHECK(xrt_lookup(OWNER, XTOKEN_INVALID) == -1);
    finish(id, OWNER);
    CHECK(xrt_lookup(OWNER, t) == -1);                /* released: no longer names a ring */
}

/* ---- owner teardown: closes the owner's rings, nobody else's -------------- */

static void test_destroy_owner_closes_only_its_rings(void) {
    fresh();
    xrt_set_cap(OWNER, 4);
    xrt_set_cap(OTHER, 4);
    XToken ta = XTOKEN_INVALID, tb = XTOKEN_INVALID, tc = XTOKEN_INVALID;
    int a = xrt_create(OWNER, 16, &ta);
    int b = xrt_create(OWNER, 16, &tb);
    int c = xrt_create(OTHER, 16, &tc);
    CHECK(a > 0 && b > 0 && c > 0);

    /* a has an entry in flight when its owner dies. */
    XqSqe e = ent(5);
    submit(a, e);
    CHECK(kr_process(xrt_ring((uint32_t)a)) == 1);

    CHECK(xrt_destroy_owner(OWNER) == 2);             /* a and b were open */
    CHECK(xrt_ring((uint32_t)c)->closing == 0);       /* other owner untouched */
    CHECK(xrt_destroy_owner(OWNER) == 0);             /* idempotent: nothing new to close */

    xrt_reap();
    CHECK(xrt_ring((uint32_t)b) == 0);                /* nothing in flight: released */
    CHECK(xrt_ring((uint32_t)a) != 0);                /* still in flight: held */

    KRing *ra = xrt_ring((uint32_t)a);
    uint32_t slot = 0;
    for (uint32_t s = 0; s < ra->entries; s++)
        if (ra->blk[s] != XTOKEN_INVALID) slot = s;
    CHECK(kr_complete(ra, slot, 0) == 0);
    xrt_reap();
    CHECK(xrt_ring((uint32_t)a) == 0);                /* last completion: released */
    CHECK(xrt_owner_count(OWNER) == 0);
    CHECK(xrt_ring((uint32_t)c) != 0);                /* other owner still alive */

    xtok_release(e.blk_token);
    xtok_release(e.ram_token);
    finish(c, OTHER);
}

/* Completes the in-flight slot of ring id through its route, as the driver's
 * completion delivery does. */
static void route_complete(uint32_t id) {
    KRing *r = xrt_ring(id);
    uint32_t slot = 0;
    for (uint32_t s2 = 0; s2 < r->entries; s2++)
        if (r->blk[s2] != XTOKEN_INVALID) slot = s2;
    bound_fn[id](bound_arg[id], slot, 0);
}

static void test_route_delivers_to_its_ring(void) {
    fresh();
    xrt_set_cap(OWNER, 4);
    XToken ta = XTOKEN_INVALID;
    int a = xrt_create(OWNER, 16, &ta);
    CHECK(a == 1 && bound[a] == 1);
    CHECK(bound_arg[a] == (void *)xrt_ring((uint32_t)a));
    XqSqe e = ent(9);
    submit(a, e);
    CHECK(kr_process(xrt_ring((uint32_t)a)) == 1);
    route_complete((uint32_t)a);
    CHECK(kr_inflight(xrt_ring((uint32_t)a)) == 0);
    CHECK(((XqHeader *)xrt_ring((uint32_t)a)->mem)->cq_tail == 1);
    xtok_release(e.blk_token);
    xtok_release(e.ram_token);
    finish(a, OWNER);
}

static void test_closed_ring_keeps_route_until_drained(void) {
    fresh();
    xrt_set_cap(OWNER, 4);
    XToken ta = XTOKEN_INVALID;
    int a = xrt_create(OWNER, 16, &ta);
    probe_tok = ta;
    XqSqe e = ent(10);
    submit(a, e);
    CHECK(kr_process(xrt_ring((uint32_t)a)) == 1);
    CHECK(xrt_destroy(OWNER, ta) == 0);
    xrt_reap();
    CHECK(bound[a] == 1 && unbind_calls == 0);      /* a completion can still arrive */
    route_complete((uint32_t)a);
    xrt_reap();
    CHECK(bound[a] == 0 && unbind_calls == 1);      /* unbound once drained */
    CHECK(live_at_unbind == 1);                     /* ...while the token still lived */
    CHECK(xrt_ring((uint32_t)a) == 0);
    xtok_release(e.blk_token);
    xtok_release(e.ram_token);
}

static void test_released_id_binds_again(void) {
    fresh();
    xrt_set_cap(OWNER, 4);
    XToken t1 = XTOKEN_INVALID, t2 = XTOKEN_INVALID;
    int a = xrt_create(OWNER, 16, &t1);
    finish(a, OWNER);
    CHECK(bound[a] == 0);
    int b = xrt_create(OWNER, 16, &t2);
    CHECK(b == a && bound[b] == 1);
    CHECK(bound_arg[b] == (void *)xrt_ring((uint32_t)b));
    finish(b, OWNER);
}

static void test_bind_refusal_leaves_nothing(void) {
    fresh();
    xrt_set_cap(OWNER, 4);
    UINT64 f0 = 0; UINT32 t0 = 0;
    xtok_owner_usage(OWNER, &f0, &t0);
    bind_fail = 1;
    XToken t = XTOKEN_INVALID;
    CHECK(xrt_create(OWNER, 16, &t) == XRT_E_NOMEM);
    CHECK(t == XTOKEN_INVALID);
    CHECK(xrt_owner_count(OWNER) == 0);
    UINT64 f1 = 0; UINT32 t1 = 0;
    xtok_owner_usage(OWNER, &f1, &t1);
    CHECK(f1 == f0 && t1 == t0);                    /* token and frames given back */
    bind_fail = 0;
    int a = xrt_create(OWNER, 16, &t);
    CHECK(a == 1);                                  /* the id was not consumed */
    finish(a, OWNER);
}

int main(void) {
    host_dev_init(&dev, 512, 1024);
    dev.ops = &async_ops;
    xtok_init();
    test_ids_are_distinct_and_start_at_one();
    test_entry_limits();
    test_cap_is_per_owner();
    test_zero_cap_by_default();
    test_foreign_token_is_refused();
    test_free_refused_while_alive();
    test_id_not_reused_until_released();
    test_cap_returns_on_release();
    test_lookup_is_owner_checked();
    test_destroy_owner_closes_only_its_rings();
    test_route_delivers_to_its_ring();
    test_closed_ring_keeps_route_until_drained();
    test_released_id_binds_again();
    test_bind_refusal_leaves_nothing();
    printf("xring_table: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
