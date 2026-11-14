/* Host test for completion routing between rings, against the real nvme.c.
 * Two rings with different owners share one I/O queue. Each completion must
 * reach the ring that submitted it, and only that ring. A completion for a ring
 * with no route is dropped and counted, never delivered elsewhere. */
#define SPIN_LIMIT 200u
#include "../../../source/arch/amd64/drivers/storage/nvme/nvme.c"

#include <stdio.h>
#include <string.h>

#define ROUTE_CID(ring, slot) (NVME_XQ_CID_BASE | ((uint16_t)(ring) << 10) | (uint16_t)(slot))

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint8_t bar[0x2000] __attribute__((aligned(8)));
static uint8_t sqm[QD * SQE_BYTES] __attribute__((aligned(64)));
static uint8_t cqm[QD * CQE_BYTES] __attribute__((aligned(64)));

typedef struct { uint32_t n; uint32_t slot[8]; } Got;
static Got got_a, got_b;

static void on_a(void *arg, uint32_t slot, int32_t st) {
    Got *g = arg; (void)st;
    if (g->n < 8) g->slot[g->n] = slot;
    g->n++;
}
static void on_b(void *arg, uint32_t slot, int32_t st) {
    Got *g = arg; (void)st;
    if (g->n < 8) g->slot[g->n] = slot;
    g->n++;
}

static void reset(void) {
    memset(bar, 0, sizeof bar);
    memset(sqm, 0, sizeof sqm);
    memset(cqm, 0, sizeof cqm);
    memset(&g_nvme, 0, sizeof g_nvme);
    memset(&got_a, 0, sizeof got_a);
    memset(&got_b, 0, sizeof got_b);
    memset(g_route, 0, sizeof g_route);
    g_route_dropped = 0;
    g_nvme.regs = bar;
    g_nvme.io.qid = 1;
    g_nvme.io.sq = sqm;
    g_nvme.io.cq = cqm;
    g_nvme.io.phase = 1;
    g_nvme.io.next_cid = 1;
}

static void submit_cid(uint16_t cid) {
    uint8_t cmd[64] = {0};
    cmd[0] = 0x02;
    cmd[2] = (uint8_t)cid;
    cmd[3] = (uint8_t)(cid >> 8);
    CHECK(queue_submit(&g_nvme.io, cmd) == BLK_OK);
}

/* Plays the controller: a completion at slot carrying cid. */
static void post_cqe(uint32_t slot, uint16_t cid, uint32_t sqhd) {
    uint8_t *e = cqm + slot * CQE_BYTES;
    uint32_t dw2 = sqhd & 0xFFFFu;
    uint32_t dw3 = (uint32_t)cid | (1u << 16);      /* phase 1, status 0 */
    for (int i = 0; i < 4; i++) {
        e[8 + i]  = (uint8_t)(dw2 >> (8 * i));
        e[12 + i] = (uint8_t)(dw3 >> (8 * i));
    }
}

static void test_two_rings_no_cross_delivery(void) {
    reset();
    CHECK(nvme_route_bind(1, on_a, &got_a) == 0);
    CHECK(nvme_route_bind(2, on_b, &got_b) == 0);

    submit_cid(ROUTE_CID(1, 0));                    /* ring A, slot 0 */
    submit_cid(ROUTE_CID(2, 0));                    /* ring B, slot 0: same slot, other ring */
    submit_cid(ROUTE_CID(1, 1));                    /* ring A, slot 1 */

    post_cqe(0, ROUTE_CID(1, 0), 3);
    post_cqe(1, ROUTE_CID(2, 0), 3);
    post_cqe(2, ROUTE_CID(1, 1), 3);
    nvme_io_poll();

    CHECK(got_a.n == 2);
    CHECK(got_a.n >= 1 && got_a.slot[0] == 0);
    CHECK(got_a.n >= 2 && got_a.slot[1] == 1);
    CHECK(got_b.n == 1);
    CHECK(got_b.n >= 1 && got_b.slot[0] == 0);
    CHECK(g_route_dropped == 0);
}

static void test_unbound_ring_is_dropped(void) {
    reset();
    CHECK(nvme_route_bind(1, on_a, &got_a) == 0);
    post_cqe(0, ROUTE_CID(3, 0), 1);                /* ring 3 has no route */
    nvme_io_poll();
    CHECK(got_a.n == 0);
    CHECK(g_route_dropped == 1);
}

static void test_bind_rules(void) {
    reset();
    CHECK(nvme_route_bind(NVME_ROUTES, on_a, &got_a) == -1);   /* out of range */
    CHECK(nvme_route_bind(4, on_a, &got_a) == 0);
    CHECK(nvme_route_bind(4, on_b, &got_b) == -1);             /* never steal a route */
    nvme_route_unbind(4);
    CHECK(nvme_route_bind(4, on_b, &got_b) == 0);              /* free after unbind */
}

int main(void) {
    test_two_rings_no_cross_delivery();
    test_unbound_ring_is_dropped();
    test_bind_rules();
    printf("nvme_route: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
