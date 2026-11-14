/******************************************************************************
 * XASMOS - NVMe queue mechanics over plain memory (portable, host-testable)
 * Path: source/arch/amd64/drivers/storage/nvme/nvme_q.h
 *
 * SQ/CQ layout and the head/tail/phase rules. No BAR0, no cids, no routing:
 * the caller rings doorbells through callbacks and decides what a reaped
 * completion means.
 ******************************************************************************/
#ifndef XASMOS_NVME_Q_H
#define XASMOS_NVME_Q_H

#include <stdint.h>

#define NVME_Q_SQE_BYTES 64u
#define NVME_Q_CQE_BYTES 16u

typedef struct {
    volatile uint8_t *sq;
    volatile uint8_t *cq;
    uint32_t depth;
    uint32_t tail;
    uint32_t head;
    uint32_t phase;
    uint32_t sq_head;      /* controller's consumed SQ position */
    void    *db_arg;
    void   (*ring_sq)(void *arg, uint32_t tail);
    void   (*ring_cq)(void *arg, uint32_t head);
} NvmeQ;

int nvme_q_full(const NvmeQ *q);
int nvme_q_submit(NvmeQ *q, const uint8_t *cmd);
int nvme_q_reap(NvmeQ *q, uint16_t *cid, uint32_t *dw3);

#endif
