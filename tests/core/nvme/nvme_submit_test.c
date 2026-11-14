/* Host test for queue_submit's wait-for-space policy, against the real nvme.c.
 * nvme.c is included so its static functions are testable. The BAR is a plain
 * buffer, so doorbell writes can be read back. SPIN_LIMIT is set low so the
 * timeout case finishes at once. */
#define SPIN_LIMIT 200u
#include "../../../source/arch/amd64/drivers/storage/nvme/nvme.c"

#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint8_t bar[0x2000]  __attribute__((aligned(8)));
static uint8_t sqm[QD * SQE_BYTES] __attribute__((aligned(64)));
static uint8_t cqm[QD * CQE_BYTES] __attribute__((aligned(64)));

static uint32_t async_n;
static uint32_t async_slot[32];
static int32_t  async_st[32];

static void on_async(void *arg, uint32_t slot, int32_t st) {
    (void)arg;
    async_slot[async_n] = slot;
    async_st[async_n]   = st;
    async_n++;
}

static uint32_t bar32(uint32_t off) {
    uint32_t v;
    memcpy(&v, bar + off, 4);
    return v;
}

static void reset(void) {
    memset(bar, 0, sizeof bar);
    memset(sqm, 0, sizeof sqm);
    memset(cqm, 0, sizeof cqm);
    memset(&g_nvme, 0, sizeof g_nvme);
    g_nvme.regs = bar;
    g_nvme.dstrd = 0;
    g_nvme.io.qid = 1;
    g_nvme.io.sq = sqm;
    g_nvme.io.cq = cqm;
    g_nvme.io.phase = 1;
    g_nvme.io.next_cid = 1;
    async_n = 0;
    memset(g_route, 0, sizeof g_route);
    nvme_route_bind(0, on_async, 0);
}

/* Plays the controller: one completion at slot. */
static void post_cqe(uint32_t slot, uint16_t cid, uint32_t sqhd, uint32_t phase, uint32_t status) {
    uint8_t *e = cqm + slot * CQE_BYTES;
    uint32_t dw2 = sqhd & 0xFFFFu;
    uint32_t dw3 = (uint32_t)cid | (phase << 16) | (status << 17);
    for (int i = 0; i < 4; i++) {
        e[8 + i]  = (uint8_t)(dw2 >> (8 * i));
        e[12 + i] = (uint8_t)(dw3 >> (8 * i));
    }
}

static void fill(uint32_t n, uint8_t *cmd) {
    for (uint32_t i = 0; i < n; i++)
        CHECK(queue_submit(&g_nvme.io, cmd) == BLK_OK);
}

static void test_doorbell_and_fill(void) {
    uint8_t cmd[64] = {0};
    reset();
    cmd[0] = 0x02;
    fill(QD - 1u, cmd);                       /* the last slot stays free */
    CHECK(g_nvme.io.tail == QD - 1u);
    CHECK(bar32(sq_doorbell(1)) == QD - 1u);  /* doorbell carries the tail */
    CHECK(sqm[0] == 0x02);
    CHECK(nvme_q_full(&(NvmeQ){ .depth = QD, .tail = g_nvme.io.tail,
                                 .sq_head = g_nvme.io.sq_head }));
}

static void test_full_times_out_without_completions(void) {
    uint8_t cmd[64] = {0};
    reset();
    fill(QD - 1u, cmd);
    CHECK(queue_submit(&g_nvme.io, cmd) == BLK_E_TIMEOUT);
    CHECK(g_nvme.io.tail == QD - 1u);         /* nothing written */
    CHECK(bar32(sq_doorbell(1)) == QD - 1u);  /* no doorbell for the refused one */
    CHECK(async_n == 0);
}

static void test_wait_routes_async_and_frees_slot(void) {
    uint8_t cmd[64] = {0};
    reset();
    cmd[0] = 0x01; cmd[1] = 0x77;
    fill(QD - 1u, cmd);
    /* controller consumed entry 0 and completes the async command in slot 0 */
    post_cqe(0, NVME_XQ_CID_BASE | 0u, 1, 1, 0);
    CHECK(queue_submit(&g_nvme.io, cmd) == BLK_OK);
    CHECK(async_n == 1);
    CHECK(async_slot[0] == 0 && async_st[0] == XQ_ST_OK);
    CHECK(g_nvme.io.sq_head == 1);
    CHECK(g_nvme.io.head == 1);
    CHECK(bar32(cq_doorbell(1)) == 1);        /* completion head published */
    CHECK(g_nvme.io.tail == 0);               /* wrapped */
    CHECK(bar32(sq_doorbell(1)) == 0);
    CHECK(sqm[(QD - 1u) * SQE_BYTES + 1] == 0x77);  /* our SQE, in the last slot */
}

static void test_async_error_status_is_negated(void) {
    uint8_t cmd[64] = {0};
    reset();
    fill(QD - 1u, cmd);
    post_cqe(0, NVME_XQ_CID_BASE | 0u, 1, 1, 0x04);
    CHECK(queue_submit(&g_nvme.io, cmd) == BLK_OK);
    CHECK(async_n == 1 && async_st[0] == -4);
}

static void test_sync_completion_during_wait_is_error(void) {
    uint8_t cmd[64] = {0};
    reset();
    fill(QD - 1u, cmd);
    post_cqe(0, 3, 1, 1, 0);                  /* synchronous cid: protocol error */
    CHECK(queue_submit(&g_nvme.io, cmd) == BLK_E_IO);
    CHECK(async_n == 0);
    CHECK(g_nvme.io.tail == QD - 1u);         /* nothing submitted */
}

static void test_stale_phase_is_not_consumed(void) {
    uint8_t cmd[64] = {0};
    reset();
    fill(QD - 1u, cmd);
    post_cqe(0, NVME_XQ_CID_BASE | 0u, 1, 0, 0);   /* old phase: not new */
    CHECK(queue_submit(&g_nvme.io, cmd) == BLK_E_TIMEOUT);
    CHECK(async_n == 0);
}

static void test_wraparound(void) {
    uint8_t cmd[64] = {0};
    reset();
    cmd[1] = 0x5A;
    g_nvme.io.tail = QD - 1u;
    g_nvme.io.sq_head = 1;
    CHECK(queue_submit(&g_nvme.io, cmd) == BLK_OK);
    CHECK(g_nvme.io.tail == 0);
    CHECK(sqm[(QD - 1u) * SQE_BYTES + 1] == 0x5A);
    CHECK(bar32(sq_doorbell(1)) == 0);
}

static void test_try_submit_never_waits(void) {
    uint8_t cmd[64] = {0};
    reset();
    fill(QD - 1u, cmd);
    CHECK(queue_try_submit(&g_nvme.io, cmd) == BLK_E_BUSY);   /* full: at once */
    CHECK(g_nvme.io.tail == QD - 1u);                         /* nothing written */
    CHECK(bar32(sq_doorbell(1)) == QD - 1u);                  /* no doorbell */
    CHECK(async_n == 0);                                      /* did not reap */
    g_nvme.io.sq_head = 1;                                    /* a slot frees up */
    CHECK(queue_try_submit(&g_nvme.io, cmd) == BLK_OK);
    CHECK(g_nvme.io.tail == 0);
}

int main(void) {
    test_doorbell_and_fill();
    test_full_times_out_without_completions();
    test_wait_routes_async_and_frees_slot();
    test_async_error_status_is_negated();
    test_sync_completion_during_wait_is_error();
    test_stale_phase_is_not_consumed();
    test_wraparound();
    test_try_submit_never_waits();
    printf("nvme_submit: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
