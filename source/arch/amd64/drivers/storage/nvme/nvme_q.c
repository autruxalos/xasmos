/******************************************************************************
 * XASMOS - NVMe queue mechanics over plain memory
 * Path: source/arch/amd64/drivers/storage/nvme/nvme_q.c
 ******************************************************************************/
#include "nvme_q.h"

static uint32_t rd32(const volatile uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int nvme_q_full(const NvmeQ *q) {
    return ((q->tail + 1u) % q->depth) == q->sq_head;
}

int nvme_q_submit(NvmeQ *q, const uint8_t *cmd) {
    if (nvme_q_full(q)) return -1;
    volatile uint8_t *dst = q->sq + (uint64_t)q->tail * NVME_Q_SQE_BYTES;
    for (uint32_t i = 0; i < NVME_Q_SQE_BYTES; i++) dst[i] = cmd[i];
    q->tail = (q->tail + 1u) % q->depth;
    q->ring_sq(q->db_arg, q->tail);
    return 0;
}

int nvme_q_reap(NvmeQ *q, uint16_t *cid, uint32_t *dw3_out) {
    const volatile uint8_t *e = q->cq + (uint64_t)q->head * NVME_Q_CQE_BYTES;
    uint32_t dw3 = rd32(e + 12);
    if (((dw3 >> 16) & 1u) != q->phase) return -1;

    q->sq_head = rd32(e + 8) % q->depth;           /* SQHD is DW2 bits 15:0 */
    q->head = (q->head + 1u) % q->depth;
    if (q->head == 0) q->phase ^= 1u;
    q->ring_cq(q->db_arg, q->head);

    *cid = (uint16_t)(dw3 & 0xFFFFu);              /* CID is DW3 bits 15:0 */
    *dw3_out = dw3;
    return 0;
}
