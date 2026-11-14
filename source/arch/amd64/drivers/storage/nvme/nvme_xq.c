/******************************************************************************
 * XASMOS - NVMe adapter for core/xqueue (asynchronous I/O)
 * Path: source/arch/amd64/drivers/storage/nvme/nvme_xq.c
 ******************************************************************************/
#include "nvme_xq.h"

static void put32(uint8_t *c, uint32_t dw, uint32_t v) {
    c[dw * 4 + 0] = (uint8_t)v;
    c[dw * 4 + 1] = (uint8_t)(v >> 8);
    c[dw * 4 + 2] = (uint8_t)(v >> 16);
    c[dw * 4 + 3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *c, uint32_t dw, uint64_t v) {
    put32(c, dw, (uint32_t)v);
    put32(c, dw + 1, (uint32_t)(v >> 32));
}

static uint32_t get32(const uint8_t *c, uint32_t dw) {
    return (uint32_t)c[dw * 4] | ((uint32_t)c[dw * 4 + 1] << 8) |
           ((uint32_t)c[dw * 4 + 2] << 16) | ((uint32_t)c[dw * 4 + 3] << 24);
}

int nvme_xq_submit_raw(NvmeXqQueue *q, uint32_t slot, uint8_t opcode,
                       uint32_t nsid, uint64_t prp1, uint64_t slba, uint32_t count) {
    uint32_t next = (q->sq_tail + 1u) % q->depth;
    if (next == q->sq_head) return -1;          /* full: busy, not an error */
    if (count == 0 || count > 8) return -1;

    uint8_t cmd[NVME_XQ_SQE_BYTES];
    for (uint32_t i = 0; i < sizeof cmd; i++) cmd[i] = 0;
    /* slot is a tag: ring in bits 14:10, ring slot in bits 9:0 */
    uint16_t cid = (uint16_t)(NVME_XQ_CID_BASE | (slot & 0x7FFFu));
    cmd[0] = opcode;
    cmd[2] = (uint8_t)cid;
    cmd[3] = (uint8_t)(cid >> 8);
    put32(cmd, 1, nsid);
    put64(cmd, 6, prp1);
    put32(cmd, 10, (uint32_t)slba);
    put32(cmd, 11, (uint32_t)(slba >> 32));
    put32(cmd, 12, count - 1u);

    uint8_t *dst = q->sq + (uint64_t)q->sq_tail * NVME_XQ_SQE_BYTES;
    for (uint32_t i = 0; i < NVME_XQ_SQE_BYTES; i++) dst[i] = cmd[i];
    q->sq_tail = next;
    q->ring_sq(q->db_arg, q->sq_tail);
    return 0;
}

uint32_t nvme_xq_drain_raw(NvmeXqQueue *q, void (*done)(void *ctx, uint32_t slot, int32_t status), void *ctx) {
    uint32_t n = 0;
    for (;;) {
        const uint8_t *e = q->cq + (uint64_t)q->cq_head * NVME_XQ_CQE_BYTES;
        uint32_t dw3 = get32(e, 3);
        if (((dw3 >> 16) & 1u) != q->cq_phase) break;   /* not new */

        uint32_t dw2 = get32(e, 2);
        uint16_t cid = (uint16_t)(dw3 & 0xFFFFu);
        uint16_t sqhd = (uint16_t)(dw2 & 0xFFFFu);
        uint32_t status = (dw3 >> 17) & 0x7FFFu;

        /* The controller tells us how far it has consumed the SQ. */
        q->sq_head = sqhd % q->depth;

        q->cq_head = (q->cq_head + 1u) % q->depth;
        if (q->cq_head == 0) q->cq_phase ^= 1u;
        q->ring_cq(q->db_arg, q->cq_head);

        if ((cid & NVME_XQ_CID_BASE) != 0) {
            int32_t st = status == 0 ? XQ_ST_OK : -(int32_t)status;
            done(ctx, cid & 0x3FFu, st);
            n++;
        }
    }
    return n;
}
