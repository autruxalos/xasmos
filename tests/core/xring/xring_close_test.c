/* Host test for deferred release of a closing ring: the route is unbound while
 * the ring token is still live, the ring waits for its in-flight entries, and a
 * token with pins refuses release. Runs against the real xring.c and ledger. */
#include "../../../source/arch/amd64/kernel/xeret/xring.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_stubs.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define OWNER   2u
#define RING_ID 3u

static BlkDev  dev;
static void   *mem;
static KRing   r;
static XToken  ring_tok;
static int     unbind_calls;
static int     token_live_at_unbind;

static void route_unbind_stub(uint32_t ring) {
    (void)ring;
    unbind_calls++;
    token_live_at_unbind = (xtok_ram_info(ring_tok, 0, 0) == 0);
}

static int dev_submit(void *arg, uint32_t slot, const XqSqe *e) {
    (void)arg; (void)slot; (void)e;
    return 0;
}

static void ring_new(void) {
    mem = calloc(1, (size_t)xq_bytes(16));
    xq_init(mem, 16);
    ring_tok = xtok_alloc_ram(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    kr_init(&r, OWNER, mem, 16, dev_submit, 0);
    unbind_calls = 0;
    token_live_at_unbind = 0;
}

static XqSqe *sqe_at(uint32_t s) { return (XqSqe *)((uint8_t *)mem + xq_sq_off()) + s; }
static void submit(XqSqe e) {
    XqHeader *h = mem;
    *sqe_at(h->sq_tail & h->mask) = e;
    h->sq_tail = h->sq_tail + 1u;
}
static XqSqe ent(uint64_t cookie) {
    XqSqe e; memset(&e, 0, sizeof e);
    e.cookie    = cookie;
    e.blk_token = xtok_alloc_blocks(OWNER, &dev, 0, 8, XTOK_RIGHTS_BLOCKS_DEFAULT);
    e.ram_token = xtok_alloc_ram(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    e.count = 1; e.op = XQ_OP_READ;
    return e;
}
static uint32_t the_slot(void) {
    for (uint32_t s = 0; s < 16; s++) if (r.blk[s] != XTOKEN_INVALID) return s;
    return 0;
}

static void test_route_stays_while_inflight(void) {
    ring_new();
    XqSqe a = ent(1);
    submit(a);
    kr_process(&r);
    uint32_t slot = the_slot();

    kr_close(&r);
    CHECK(kr_reap_closed(&r, ring_tok, RING_ID, route_unbind_stub) == 0);
    CHECK(unbind_calls == 0);

    CHECK(kr_complete(&r, slot, 0) == 0);
    CHECK(kr_reap_closed(&r, ring_tok, RING_ID, route_unbind_stub) == 1);
    CHECK(unbind_calls == 1);
    CHECK(token_live_at_unbind == 1);
    CHECK(xtok_ram_info(ring_tok, 0, 0) != 0);
    xtok_release(a.blk_token); xtok_release(a.ram_token);
    free(mem);
}

static void test_ring_token_refuses_while_pinned(void) {
    ring_new();
    XqSqe a = ent(2);
    submit(a);
    kr_process(&r);
    CHECK(xtok_ring_pin(OWNER, a.blk_token, ring_tok, XTOK_BLK_READ, 0, 1) == 0);
    CHECK(xtok_release(ring_tok) == XTOK_REL_EBUSY);
    xtok_ring_unpin(a.blk_token, ring_tok);
    kr_complete(&r, the_slot(), 0);
    xtok_release(a.blk_token); xtok_release(a.ram_token);
    free(mem);
}

static void test_open_ring_is_not_reaped(void) {
    ring_new();
    CHECK(kr_reap_closed(&r, ring_tok, RING_ID, route_unbind_stub) == 0);
    CHECK(unbind_calls == 0);
    CHECK(xtok_ram_info(ring_tok, 0, 0) == 0);
    free(mem);
}

int main(void) {
    host_dev_init(&dev, 512, 1024);
    xtok_init();
    test_route_stays_while_inflight();
    test_ring_token_refuses_while_pinned();
    test_open_ring_is_not_reaped();
    printf("xring_close: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
