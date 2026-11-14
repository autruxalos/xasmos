/* Host test for the NVMe asynchronous adapter: SQE layout, busy when the SQ is
 * full, cid tagging, and drain delivering only ring completions. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvme_xq.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define DEPTH 8u

static uint8_t sq[DEPTH * 64], cq[DEPTH * 16];
static uint32_t db_sq_last, db_cq_last, db_sq_rings, db_cq_rings;

static void ring_sq(void *a, uint32_t t) { (void)a; db_sq_last = t; db_sq_rings++; }
static void ring_cq(void *a, uint32_t h) { (void)a; db_cq_last = h; db_cq_rings++; }

static NvmeXqQueue q;

static void q_init(void) {
    memset(sq, 0, sizeof sq); memset(cq, 0, sizeof cq);
    memset(&q, 0, sizeof q);
    q.sq = sq; q.cq = cq; q.depth = DEPTH; q.cq_phase = 1;
    q.ring_sq = ring_sq; q.ring_cq = ring_cq;
    db_sq_rings = db_cq_rings = 0;
}

/* Plays the controller: writes a CQE for cid with the given status. */
static void fake_cqe(uint32_t cq_slot, uint16_t cid, uint32_t sq_head, uint32_t status, uint32_t phase) {
    uint8_t *e = cq + cq_slot * 16;
    uint32_t dw0 = 0, dw1 = 0;
    uint32_t dw2 = (sq_head & 0xFFFFu) | (1u << 16);   /* SQHD low, SQID 1 high */
    uint32_t dw3 = (uint32_t)cid | (phase << 16) | (status << 17);
    uint32_t dws[4] = { dw0, dw1, dw2, dw3 };
    for (int d = 0; d < 4; d++)
        for (int i = 0; i < 4; i++) e[d*4 + i] = (uint8_t)(dws[d] >> (8*i));
}

static uint32_t get32(const uint8_t *c, uint32_t dw) {
    return (uint32_t)c[dw*4] | ((uint32_t)c[dw*4+1] << 8) |
           ((uint32_t)c[dw*4+2] << 16) | ((uint32_t)c[dw*4+3] << 24);
}

static int got_n; static uint32_t got_slot[64]; static int32_t got_st[64];
static void on_done(void *ctx, uint32_t slot, int32_t st) {
    (void)ctx;
    got_slot[got_n] = slot; got_st[got_n] = st; got_n++;
}

static void test_sqe_layout(void) {
    q_init();
    CHECK(nvme_xq_submit_raw(&q, 5, 0x02, 1, 0x123000ULL, 0x1122334455ULL, 4) == 0);
    const uint8_t *c = sq;
    CHECK(c[0] == 0x02);                                  /* opcode read */
    CHECK((c[2] | (c[3] << 8)) == (NVME_XQ_CID_BASE | 5));/* cid tagged with slot */
    CHECK(get32(c, 1) == 1);                              /* NSID */
    CHECK(get32(c, 6) == 0x123000u && get32(c, 7) == 0);  /* PRP1 */
    CHECK(get32(c, 10) == 0x22334455u && get32(c, 11) == 0x11u); /* SLBA */
    CHECK(get32(c, 12) == 3);                             /* NLB zero-based: 4 blocks */
    CHECK(q.sq_tail == 1 && db_sq_last == 1 && db_sq_rings == 1);
}

static void test_write_opcode(void) {
    q_init();
    CHECK(nvme_xq_submit_raw(&q, 0, 0x01, 1, 0x1000, 0, 1) == 0);
    CHECK(sq[0] == 0x01);
}

static void test_busy_when_full(void) {
    q_init();
    /* depth 8 keeps one entry free: 7 submissions fill it */
    for (uint32_t i = 0; i < DEPTH - 1; i++)
        CHECK(nvme_xq_submit_raw(&q, i, 0x02, 1, 0x1000, i, 1) == 0);
    CHECK(nvme_xq_submit_raw(&q, 7, 0x02, 1, 0x1000, 7, 1) == -1);  /* busy, not written */
    CHECK(q.sq_tail == DEPTH - 1);
    CHECK(db_sq_rings == DEPTH - 1);                      /* no doorbell for the refused one */
}

static void test_drain_delivers_only_ring_cids(void) {
    q_init();
    nvme_xq_submit_raw(&q, 3, 0x02, 1, 0x1000, 0, 1);
    fake_cqe(0, NVME_XQ_CID_BASE | 3, 1, 0, 1);           /* ours, ok */
    fake_cqe(1, 42, 1, 0, 1);                             /* synchronous cid: ignored */
    fake_cqe(2, NVME_XQ_CID_BASE | 6, 1, 0x04, 1);        /* ours, device error 0x04 */
    got_n = 0;
    CHECK(nvme_xq_drain_raw(&q, on_done, NULL) == 2);
    CHECK(got_n == 2);
    CHECK(got_slot[0] == 3 && got_st[0] == XQ_ST_OK);
    CHECK(got_slot[1] == 6 && got_st[1] == -4);
    CHECK(q.cq_head == 3);
    CHECK(db_cq_last == 3);
}

static void test_drain_advances_sq_head(void) {
    q_init();
    for (uint32_t i = 0; i < 4; i++) nvme_xq_submit_raw(&q, i, 0x02, 1, 0x1000, i, 1);
    fake_cqe(0, NVME_XQ_CID_BASE | 0, 1, 0, 1);           /* controller consumed 1 */
    got_n = 0;
    nvme_xq_drain_raw(&q, on_done, NULL);
    CHECK(q.sq_head == 1);                                /* space freed for submits */
}

static void test_phase_and_no_new(void) {
    q_init();
    fake_cqe(0, NVME_XQ_CID_BASE | 1, 0, 0, 0);           /* phase 0: not new yet */
    got_n = 0;
    CHECK(nvme_xq_drain_raw(&q, on_done, NULL) == 0);
    CHECK(got_n == 0 && q.cq_head == 0);
}

static void test_phase_flip_on_wrap(void) {
    q_init();
    for (uint32_t i = 0; i < DEPTH; i++)
        fake_cqe(i, NVME_XQ_CID_BASE | i, 0, 0, 1);
    got_n = 0;
    CHECK(nvme_xq_drain_raw(&q, on_done, NULL) == DEPTH);
    CHECK(q.cq_head == 0 && q.cq_phase == 0);
}

int main(void) {
    test_sqe_layout();
    test_write_opcode();
    test_busy_when_full();
    test_drain_delivers_only_ring_cids();
    test_drain_advances_sq_head();
    test_phase_and_no_new();
    test_phase_flip_on_wrap();
    printf("nvme_xq: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
