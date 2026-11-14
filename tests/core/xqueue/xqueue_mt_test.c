/* Concurrency test for core/xqueue: four threads share one ring.
 *   producer  (program)  writes submissions, advances sq_tail
 *   processor (kernel)   runs xq_process, dispatches accepted entries
 *   completer (device)   calls xq_complete for each dispatched slot
 *   consumer  (program)  reads completions, advances cq_head
 * Invariant: every cookie gets exactly one completion, with the right status.
 * Rejected entries (lba == REJECT_LBA) complete inside xq_process itself, so
 * processor and completer contend for the completion ring under klock. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include "xqueue.h"

#define N           200000u
#define ENTRIES     64u
#define REJECT_LBA  1000u

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void     *mem;
static XqHeader *h;
static uint32_t  count_seen[N + 1];
static int32_t   st_seen[N + 1];

/* dispatched entries waiting for the device: processor -> completer */
typedef struct { uint32_t slot; uint64_t cookie; } Pend;
static Pend pend[N];
static uint32_t pend_head, pend_tail;
static int      process_done;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv = PTHREAD_COND_INITIALIZER;

static XqSqe *sq_base(void) { return (XqSqe *)((uint8_t *)mem + xq_sq_off()); }
static XqCqe *cq_base(void) { return (XqCqe *)((uint8_t *)mem + xq_cq_off(ENTRIES)); }

/* ---- kernel callbacks ------------------------------------------------------ */
static int k_validate(void *ctx, const XqSqe *e) {
    (void)ctx;
    return e->lba == REJECT_LBA ? XQ_ST_INVALID : 0;
}

static int k_dispatch(void *ctx, uint32_t slot, const XqSqe *e) {
    (void)ctx;
    pthread_mutex_lock(&mu);
    pend[pend_tail % N] = (Pend){ .slot = slot, .cookie = e->cookie };
    pend_tail++;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
    return 0;
}

static XqOps ops = { k_validate, k_dispatch, NULL };

/* ---- threads ---------------------------------------------------------------- */
static void *producer(void *arg) {
    (void)arg;
    for (uint32_t i = 1; i <= N; i++) {
        uint32_t tail = h->sq_tail;
        while (tail - __atomic_load_n(&h->sq_head, __ATOMIC_ACQUIRE) >= ENTRIES)
            sched_yield();
        XqSqe e;
        memset(&e, 0, sizeof e);
        e.cookie    = i;
        e.blk_token = 1;
        e.ram_token = 1;
        e.lba       = (i % 7u == 3u) ? REJECT_LBA : (i % 500u);
        e.count     = 1;
        e.op        = XQ_OP_READ;
        sq_base()[tail & h->mask] = e;
        __atomic_store_n(&h->sq_tail, tail + 1u, __ATOMIC_RELEASE);
    }
    return NULL;
}

static void *processor(void *arg) {
    (void)arg;
    uint32_t done = 0;
    while (done < N) {
        uint32_t n = xq_process(mem, &ops);
        done += n;
        if (n == 0) sched_yield();
    }
    pthread_mutex_lock(&mu);
    process_done = 1;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
    return NULL;
}

/* Device status is a function of the cookie, so the checker can predict it. */
static int32_t device_status(uint64_t cookie) {
    return (cookie % 5u == 0u) ? -3 : XQ_ST_OK;
}

static void *completer(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&mu);
        while (pend_head == pend_tail && !process_done)
            pthread_cond_wait(&cv, &mu);
        if (pend_head == pend_tail && process_done) {
            pthread_mutex_unlock(&mu);
            break;
        }
        Pend p = pend[pend_head % N];
        pend_head++;
        pthread_mutex_unlock(&mu);

        while (xq_complete(mem, p.slot, device_status(p.cookie)) != 0)
            sched_yield();              /* completion ring full: retry */
    }
    return NULL;
}

static void *consumer(void *arg) {
    (void)arg;
    uint32_t got = 0;
    while (got < N) {
        uint32_t tail = __atomic_load_n(&h->cq_tail, __ATOMIC_ACQUIRE);
        uint32_t head = h->cq_head;
        if (head == tail) { sched_yield(); continue; }
        while (head != tail) {
            XqCqe c = cq_base()[head & h->mask];
            if (c.cookie >= 1 && c.cookie <= N) {
                count_seen[c.cookie]++;
                st_seen[c.cookie] = c.status;
            }
            head++;
            got++;
        }
        __atomic_store_n(&h->cq_head, head, __ATOMIC_RELEASE);
    }
    return NULL;
}

/* ---- main ------------------------------------------------------------------- */
static void run_once(void) {
    memset(count_seen, 0, sizeof count_seen);
    memset(st_seen, 0, sizeof st_seen);
    pend_head = pend_tail = 0;
    process_done = 0;

    mem = calloc(1, (size_t)xq_bytes(ENTRIES));
    CHECK(mem != NULL);
    CHECK(xq_init(mem, ENTRIES) == 0);
    h = (XqHeader *)mem;

    pthread_t t[4];
    pthread_create(&t[0], NULL, producer, NULL);
    pthread_create(&t[1], NULL, processor, NULL);
    pthread_create(&t[2], NULL, completer, NULL);
    pthread_create(&t[3], NULL, consumer, NULL);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);

    CHECK(h->sq_head == N);
    CHECK(h->cq_tail == N);
    CHECK(h->cq_head == N);

    uint32_t bad_count = 0, bad_status = 0;
    for (uint32_t i = 1; i <= N; i++) {
        int32_t want = (i % 7u == 3u) ? XQ_ST_INVALID : device_status(i);
        if (count_seen[i] != 1) {
            if (bad_count < 3)
                fprintf(stderr, "cookie %u seen %u times\n", i, count_seen[i]);
            bad_count++;
        } else if (st_seen[i] != want) {
            if (bad_status < 3)
                fprintf(stderr, "cookie %u status %d, want %d\n", i, st_seen[i], want);
            bad_status++;
        }
    }
    CHECK(bad_count == 0);
    CHECK(bad_status == 0);
    free(mem);
}

int main(void) {
    for (int round = 0; round < 5; round++) run_once();
    printf("xqueue_mt: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
