/******************************************************************************
 * XASMOS - submission/completion rings (portable)
 * Path: source/core/xqueue/xqueue.h
 *
 * A ring lives in RAM owned by the program. The program writes submissions and
 * advances sq_tail. The kernel copies and validates each new submission, then
 * either completes it at once (rejected) or hands it to the device (dispatch).
 * The device completes it later through xq_complete.
 *
 * A slot is the identity of an in-flight entry: busy[slot] is set from dispatch
 * until its completion is posted. A new entry for a busy slot waits in the
 * submission ring, so an in-flight cookie is never overwritten.
 ******************************************************************************/
#ifndef XASMOS_XQUEUE_H
#define XASMOS_XQUEUE_H

#include <stdint.h>

#define XQ_SQE_BYTES   64u
#define XQ_CQE_BYTES   16u
#define XQ_MAX_COUNT   8u
#define XQ_MIN_ENTRIES 16u
#define XQ_MAX_ENTRIES 1024u
#define XQ_BATCH       64u

#define XQ_OP_READ     0u
#define XQ_OP_WRITE    1u

#define XQ_ST_OK       0
#define XQ_ST_INVALID (-20)
#define XQ_ST_BAD_OP  (-21)
#define XQ_ST_FULL    (-22)
#define XQ_ST_BUSY    (-23)   /* not taken: retry on a later pass, no CQE */

typedef struct {
    uint64_t cookie;
    uint64_t blk_token;
    uint64_t ram_token;
    uint64_t lba;
    uint32_t count;
    uint8_t  op;
    uint8_t  reserved[27];
} XqSqe;

typedef struct {
    uint64_t cookie;
    int32_t  status;
    uint32_t reserved;
} XqCqe;

typedef struct {
    volatile uint32_t sq_tail;  uint8_t _p0[60];
    volatile uint32_t sq_head;  uint8_t _p1[60];
    volatile uint32_t cq_head;  uint8_t _p2[60];
    volatile uint32_t cq_tail;  uint8_t _p3[60];
    uint32_t entries;
    uint32_t mask;
    uint32_t klock;
} XqHeader;

/* Layout: header | submissions | completions | in-flight cookies | busy flags */
static inline uint64_t xq_sq_off(void) {
    return (sizeof(XqHeader) + 63u) & ~(uint64_t)63u;
}
static inline uint64_t xq_cq_off(uint32_t n) {
    return xq_sq_off() + (uint64_t)n * XQ_SQE_BYTES;
}
static inline uint64_t xq_inflight_off(uint32_t n) {
    return xq_cq_off(n) + (uint64_t)n * XQ_CQE_BYTES;
}
static inline uint64_t xq_busy_off(uint32_t n) {
    return xq_inflight_off(n) + (uint64_t)n * 8u;
}
static inline uint64_t xq_bytes(uint32_t n) {
    return xq_busy_off(n) + (uint64_t)n * 8u;
}

typedef struct {
    int (*validate)(void *ctx, const XqSqe *e);
    int (*dispatch)(void *ctx, uint32_t slot, const XqSqe *e);
    void *ctx;
} XqOps;

int      xq_init(void *mem, uint32_t n);
uint32_t xq_process(void *mem, const XqOps *ops);
int      xq_complete(void *mem, uint32_t slot, int32_t status);

#endif /* XASMOS_XQUEUE_H */
