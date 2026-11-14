/* Host test for the asynchronous submission path: nvme_xq_submit must answer
 * XQ_ST_BUSY when the SQ is full, without waiting, and must accept again once a
 * drain has freed a slot. Runs against the real nvme.c and the real ledger. */
#define SPIN_LIMIT 200u
#include "../../../source/arch/amd64/drivers/storage/nvme/nvme.c"

#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define OWNER 2u
#define RING  1u
/* nvme_xq_submit takes the ring id through its void* argument. */
#define RING_PTR(r) ((void *)(uintptr_t)(r))

static uint8_t bar[0x2000] __attribute__((aligned(8)));
static uint8_t sqm[QD * SQE_BYTES] __attribute__((aligned(64)));
static uint8_t cqm[QD * CQE_BYTES] __attribute__((aligned(64)));
static BlkDev  ndev;
static uint32_t routed;

static uint32_t bar32(uint32_t off) {
    uint32_t v;
    memcpy(&v, bar + off, 4);
    return v;
}

static void on_done(void *arg, uint32_t slot, int32_t st) {
    (void)arg; (void)slot; (void)st;
    routed++;
}

static int nd_read(void *c, uint64_t l, uint32_t n, void *b) { (void)c; (void)l; (void)n; (void)b; return BLK_OK; }
static int nd_write(void *c, uint64_t l, uint32_t n, const void *b) { (void)c; (void)l; (void)n; (void)b; return BLK_OK; }
static int nd_flush(void *c) { (void)c; return BLK_OK; }
static const BlkOps nd_ops = { nd_read, nd_write, nd_flush, NULL };

static void reset(void) {
    memset(bar, 0, sizeof bar);
    memset(sqm, 0, sizeof sqm);
    memset(cqm, 0, sizeof cqm);
    memset(&g_nvme, 0, sizeof g_nvme);
    memset(g_route, 0, sizeof g_route);
    g_route_dropped = 0;
    routed = 0;
    g_nvme.regs = bar;
    g_nvme.io.qid = 1;
    g_nvme.io.sq = sqm;
    g_nvme.io.cq = cqm;
    g_nvme.io.phase = 1;
    g_nvme.io.next_cid = 1;
    ndev.ctx = &g_nvme;              /* nvme_xq_submit checks this identity */
    ndev.ops = &nd_ops;
    ndev.sector_size = 512;
    ndev.sector_count = 1024;
    CHECK(nvme_route_bind(RING, on_done, 0) == 0);
}

/* Translates a ring entry into the device request, as kr_device_submit does,
 * then hands it to the NVMe submit path under test. */
static int busy_submit(uint32_t slot, const XqSqe *e) {
    BlkReq req;
    void *dev = 0;
    UINT64 start = 0, phys = 0, frames = 0;
    if (xtok_blk_range(e->blk_token, &dev, &start) != 0) return BLK_E_IO;
    if (xtok_ram_info(e->ram_token, &phys, &frames) != 0) return BLK_E_IO;
    req.lba   = start + e->lba;
    req.count = e->count;
    req.op    = e->op;
    req.phys  = phys;
    req.ring  = RING;
    req.slot  = slot;
    return nvme_blk_submit(&g_nvme, &req);
}

/* A ring entry with its own tokens, so pins never collide between entries. */
static XqSqe entry(uint64_t cookie) {
    XqSqe e;
    memset(&e, 0, sizeof e);
    e.cookie    = cookie;
    e.blk_token = xtok_alloc_blocks(OWNER, &ndev, 0, 8, XTOK_RIGHTS_BLOCKS_DEFAULT);
    e.ram_token = xtok_alloc_ram(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    e.lba = 0;
    e.count = 1;
    e.op = XQ_OP_READ;
    return e;
}

static void test_busy_then_accept_after_drain(void) {
    reset();
    XqSqe e[QD];
    for (uint32_t i = 0; i < QD - 1u; i++) {
        e[i] = entry(i + 1);
        CHECK(busy_submit(i, &e[i]) == 0);
    }
    /* SQ full: refuse at once, write nothing, ring nothing. */
    XqSqe full = entry(999);
    CHECK(busy_submit(QD - 1u, &full) == XQ_ST_BUSY);
    CHECK(g_nvme.io.tail == QD - 1u);
    CHECK(bar32(sq_doorbell(1)) == QD - 1u);

    /* Controller consumes entry 0 and completes it (cid carries ring and slot). */
    uint32_t dw2 = 1u;
    uint32_t dw3 = (NVME_XQ_CID_BASE | (RING << 10) | 0u) | (1u << 16);
    for (int i = 0; i < 4; i++) {
        cqm[8 + i]  = (uint8_t)(dw2 >> (8 * i));
        cqm[12 + i] = (uint8_t)(dw3 >> (8 * i));
    }
    nvme_io_poll();
    CHECK(routed == 1);

    /* After the drain the same submission is accepted, and the index wraps. */
    CHECK(busy_submit(QD - 1u, &full) == 0);
    CHECK(g_nvme.io.tail == 0);
}

static void test_busy_does_not_consume_pins(void) {
    reset();
    XqSqe e[QD];
    for (uint32_t i = 0; i < QD - 1u; i++) {
        e[i] = entry(i + 1);
        CHECK(busy_submit(i, &e[i]) == 0);
    }
    XqSqe refused = entry(777);
    CHECK(busy_submit(QD - 1u, &refused) == XQ_ST_BUSY);
    /* A refused submission took no pin: both tokens release at once. */
    CHECK(xtok_release(refused.blk_token) == 0);
    CHECK(xtok_release(refused.ram_token) == 0);
}

int main(void) {
    xtok_init();
    test_busy_then_accept_after_drain();
    test_busy_does_not_consume_pins();
    printf("nvme_busy: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
