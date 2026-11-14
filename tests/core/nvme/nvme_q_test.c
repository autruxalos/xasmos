/* Host test for the NVMe queue mechanics: full detection, submit into a full
 * SQ, a reap that frees a slot, and phase handling. The wait-for-space policy
 * in nvme.c is built on exactly these primitives. */
#include <stdio.h>
#include <string.h>
#include "nvme_q.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define DEPTH 8u
static uint8_t sq_mem[DEPTH * 64], cq_mem[DEPTH * 16];
static uint32_t sq_db, cq_db, sq_rings, cq_rings;
static void ring_sq(void *a, uint32_t t) { (void)a; sq_db = t; sq_rings++; }
static void ring_cq(void *a, uint32_t h) { (void)a; cq_db = h; cq_rings++; }

static NvmeQ q;

static void q_init(void) {
    memset(sq_mem, 0, sizeof sq_mem);
    memset(cq_mem, 0, sizeof cq_mem);
    memset(&q, 0, sizeof q);
    q.sq = sq_mem; q.cq = cq_mem; q.depth = DEPTH; q.phase = 1;
    q.ring_sq = ring_sq; q.ring_cq = ring_cq;
    sq_rings = cq_rings = 0;
}

/* Plays the controller: posts a CQE at slot with cid, sq_head and status. */
static void post_cqe(uint32_t slot, uint16_t cid, uint32_t sqhd, uint32_t phase) {
    uint8_t *e = cq_mem + slot * 16;
    uint32_t dw2 = sqhd & 0xFFFFu;
    uint32_t dw3 = (uint32_t)cid | (phase << 16);
    for (int i = 0; i < 16; i++) e[i] = 0;
    for (int i = 0; i < 4; i++) {
        e[8 + i]  = (uint8_t)(dw2 >> (8 * i));
        e[12 + i] = (uint8_t)(dw3 >> (8 * i));
    }
}

static uint8_t cmd[64];

static void test_fill_and_full(void) {
    q_init();
    for (uint32_t i = 0; i < DEPTH - 1; i++)
        CHECK(nvme_q_submit(&q, cmd) == 0);
    CHECK(nvme_q_full(&q));
    CHECK(nvme_q_submit(&q, cmd) == -1);           /* full: refused, no doorbell */
    CHECK(sq_rings == DEPTH - 1);
    CHECK(sq_db == DEPTH - 1);                    /* doorbell carries the tail */
}

static void test_reap_frees_a_slot(void) {
    q_init();
    for (uint32_t i = 0; i < DEPTH - 1; i++) nvme_q_submit(&q, cmd);
    /* controller consumed entry 0 and completes cid 0x8000 */
    post_cqe(0, 0x8000u, 1, 1);
    uint16_t cid = 0; uint32_t dw3 = 0;
    CHECK(nvme_q_reap(&q, &cid, &dw3) == 0);
    CHECK(cid == 0x8000u);
    CHECK(q.sq_head == 1);
    CHECK(!nvme_q_full(&q));
    CHECK(nvme_q_submit(&q, cmd) == 0);            /* the freed slot is usable */
    CHECK(cq_db == 1);
}

static void test_reap_nothing_new(void) {
    q_init();
    uint16_t cid = 0; uint32_t dw3 = 0;
    CHECK(nvme_q_reap(&q, &cid, &dw3) == -1);      /* phase 0 entry: not new */
    CHECK(q.head == 0 && cq_rings == 0);
}

static void test_sync_cid_is_returned_to_caller(void) {
    /* The primitive does not judge the cid: the caller (nvme.c) decides whether
     * a synchronous completion during a wait is a protocol error. */
    q_init();
    nvme_q_submit(&q, cmd);
    post_cqe(0, 3, 1, 1);
    uint16_t cid = 0; uint32_t dw3 = 0;
    CHECK(nvme_q_reap(&q, &cid, &dw3) == 0);
    CHECK(cid == 3);
}

static void test_phase_flip_on_wrap(void) {
    q_init();
    for (uint32_t i = 0; i < DEPTH; i++) post_cqe(i, (uint16_t)(0x8000u | i), 0, 1);
    uint16_t cid = 0; uint32_t dw3 = 0;
    for (uint32_t i = 0; i < DEPTH; i++) CHECK(nvme_q_reap(&q, &cid, &dw3) == 0);
    CHECK(q.head == 0 && q.phase == 0);
    CHECK(nvme_q_reap(&q, &cid, &dw3) == -1);      /* old-phase entries are stale */
}

int main(void) {
    test_fill_and_full();
    test_reap_frees_a_slot();
    test_reap_nothing_new();
    test_sync_cid_is_returned_to_caller();
    test_phase_flip_on_wrap();
    printf("nvme_q: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
