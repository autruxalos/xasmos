/******************************************************************************
 * XASMOS - submission/completion rings (portable)
 * Path: source/core/xqueue/xqueue.c
 *
 * Ownership of the indices:
 *   sq_tail  program writes (release), kernel reads (acquire)
 *   sq_head  kernel writes, program never reads it
 *   cq_head  program writes, kernel reads
 *   cq_tail  kernel writes under klock, program reads (acquire)
 *
 * busy[slot] is written by the kernel in xq_process (set before dispatch,
 * cleared on refusal) and by xq_complete (cleared after the CQE is posted).
 ******************************************************************************/
#include "xqueue.h"

_Static_assert(sizeof(XqSqe) == XQ_SQE_BYTES, "XqSqe must be one cache line");
_Static_assert(sizeof(XqCqe) == XQ_CQE_BYTES, "XqCqe must be 16 bytes");

static XqSqe *sq_at(void *m, uint32_t slot) {
    return (XqSqe *)((uint8_t *)m + xq_sq_off()) + slot;
}
static XqCqe *cq_at(void *m, uint32_t n, uint32_t slot) {
    return (XqCqe *)((uint8_t *)m + xq_cq_off(n)) + slot;
}
static uint64_t *inflight_at(void *m, uint32_t n, uint32_t slot) {
    return (uint64_t *)((uint8_t *)m + xq_inflight_off(n)) + slot;
}
static uint64_t *busy_at(void *m, uint32_t n, uint32_t slot) {
    return (uint64_t *)((uint8_t *)m + xq_busy_off(n)) + slot;
}

static void klock_acquire(XqHeader *h) {
    while (__atomic_exchange_n(&h->klock, 1u, __ATOMIC_ACQUIRE)) {
        /* spin: the critical section is one CQE write */
    }
}

static void klock_release(XqHeader *h) {
    __atomic_store_n(&h->klock, 0u, __ATOMIC_RELEASE);
}

/* Writes one completion. Caller holds klock. Returns 0, or -1 if full. */
static int post_locked(void *m, XqHeader *h, uint64_t cookie, int32_t status) {
    uint32_t t  = __atomic_load_n(&h->cq_tail, __ATOMIC_RELAXED);
    uint32_t hd = __atomic_load_n(&h->cq_head, __ATOMIC_ACQUIRE);
    if (t - hd >= h->entries) return -1;

    XqCqe *c = cq_at(m, h->entries, t & h->mask);
    c->cookie   = cookie;
    c->status   = status;
    c->reserved = 0;

    __atomic_store_n(&h->cq_tail, t + 1u, __ATOMIC_RELEASE);
    return 0;
}

/* Copies a submission out of program-writable memory, byte by byte. */
static void copy_sqe(XqSqe *dst, const XqSqe *src) {
    const volatile uint8_t *s = (const volatile uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < XQ_SQE_BYTES; i++) d[i] = s[i];
}

int xq_init(void *mem, uint32_t n) {
    if (mem == 0) return -1;
    if (n < XQ_MIN_ENTRIES || n > XQ_MAX_ENTRIES) return -1;
    if (n & (n - 1u)) return -1;

    volatile uint8_t *b = (volatile uint8_t *)mem;
    uint64_t bytes = xq_bytes(n);
    for (uint64_t i = 0; i < bytes; i++) b[i] = 0;

    XqHeader *h = (XqHeader *)mem;
    h->entries = n;
    h->mask    = n - 1u;
    return 0;
}

uint32_t xq_process(void *mem, const XqOps *ops) {
    XqHeader *h = (XqHeader *)mem;
    uint32_t done = 0;

    while (done < XQ_BATCH) {
        uint32_t head = __atomic_load_n(&h->sq_head, __ATOMIC_RELAXED);
        uint32_t tail = __atomic_load_n(&h->sq_tail, __ATOMIC_ACQUIRE);
        if (head == tail) break;
        if (tail - head > h->entries) break;   /* corrupted indices: trust nothing past here */

        uint32_t slot = head & h->mask;
        /* A busy slot holds an in-flight entry. The head waits here, before
         * validation, so no pin is taken for an entry that cannot proceed. */
        if (__atomic_load_n(busy_at(mem, h->entries, slot), __ATOMIC_ACQUIRE)) break;
        XqSqe copy;
        copy_sqe(&copy, sq_at(mem, slot));

        int rc;
        if (copy.op > XQ_OP_WRITE) {
            rc = XQ_ST_BAD_OP;
        } else if (copy.count == 0 || copy.count > XQ_MAX_COUNT) {
            rc = XQ_ST_INVALID;
        } else {
            rc = ops->validate(ops->ctx, &copy);
        }

        if (rc != 0) {
            /* Rejected: complete now. If the completion ring is full, leave the
             * entry unconsumed so the next pass retries it. */
            klock_acquire(h);
            int posted = post_locked(mem, h, copy.cookie, rc);
            klock_release(h);
            if (posted != 0) break;
        } else {
            uint64_t *busy = busy_at(mem, h->entries, slot);

            *inflight_at(mem, h->entries, slot) = copy.cookie;
            __atomic_store_n(busy, 1, __ATOMIC_RELEASE);

            int drc = ops->dispatch(ops->ctx, slot, &copy);
            if (drc != 0) {
                /* The device did not take it: the slot is free again. */
                __atomic_store_n(busy, 0, __ATOMIC_RELEASE);
                if (drc == XQ_ST_BUSY) break;   /* no CQE: retry next pass */

                klock_acquire(h);
                int posted = post_locked(mem, h, copy.cookie, drc);
                klock_release(h);
                if (posted != 0) break;         /* retry next pass */
            }
            /* drc == 0: the device owns the slot until xq_complete. */
        }

        __atomic_store_n(&h->sq_head, head + 1u, __ATOMIC_RELEASE);
        done++;
    }
    return done;
}

int xq_complete(void *mem, uint32_t slot, int32_t status) {
    XqHeader *h = (XqHeader *)mem;
    uint32_t s = slot & h->mask;
    uint64_t cookie = *inflight_at(mem, h->entries, s);

    klock_acquire(h);
    int rc = post_locked(mem, h, cookie, status);
    klock_release(h);
    if (rc != 0) return -1;                       /* ring full: slot stays busy, retry */

    /* Only now may the slot be dispatched again. */
    __atomic_store_n(busy_at(mem, h->entries, s), 0, __ATOMIC_RELEASE);
    return 0;
}
