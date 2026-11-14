/******************************************************************************
 * XASMOS - NVMe adapter for core/xqueue (asynchronous I/O)
 * Path: source/arch/amd64/drivers/storage/nvme/nvme_xq.h
 *
 * Commands submitted from a ring carry cid = XQ_CID_BASE | slot. Synchronous
 * commands (io_exec) keep cid in 0x0001..0x7FFF, so the two never collide and
 * one completion is always attributable to one owner.
 ******************************************************************************/
#ifndef XASMOS_NVME_XQ_H
#define XASMOS_NVME_XQ_H

#include <stdint.h>
#include "xqueue.h"

#define NVME_XQ_CID_BASE 0x8000u
#define NVME_XQ_SQE_BYTES 64u
#define NVME_XQ_CQE_BYTES 16u

/* The part of a queue the adapter needs, as plain memory: the SQ and CQ the
 * controller reads and writes, the indices, and the doorbell callback. */
typedef struct {
    uint8_t  *sq;
    uint8_t  *cq;
    uint32_t  depth;          /* QD */
    uint32_t  sq_tail;
    uint32_t  sq_head;        /* advanced by drain via the controller's CQE sq_head field */
    uint32_t  cq_head;
    uint32_t  cq_phase;
    void     *db_arg;
    void    (*ring_sq)(void *arg, uint32_t tail);
    void    (*ring_cq)(void *arg, uint32_t head);
} NvmeXqQueue;

/* Builds one read or write SQE for the given slot. prp1 is the physical address
 * of the frame that the ring entry's RAM token names. Returns 0, or -1 if the
 * submission queue has no free entry (the caller returns XQ_ST_BUSY). */
int  nvme_xq_submit_raw(NvmeXqQueue *q, uint32_t slot, uint8_t opcode,
                        uint32_t nsid, uint64_t prp1, uint64_t slba, uint32_t count);

/* Reaps every new completion. For each one carrying the adapter's cid base,
 * calls done(slot, status). Completions with other cids are left for the
 * synchronous path and are skipped here. Returns how many were reaped. */
uint32_t nvme_xq_drain_raw(NvmeXqQueue *q, void (*done)(void *ctx, uint32_t slot, int32_t status), void *ctx);

#endif
