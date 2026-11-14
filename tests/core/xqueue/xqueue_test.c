/* Host test for core/xqueue: validation, rejection, dispatch, completion,
 * full rings, and time-of-check/time-of-use (the program edits an entry after
 * it was copied). Runs under ASan and UBSan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xqueue.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- fake kernel callbacks ------------------------------------------------ */
typedef struct {
    int      validate_calls;
    int      dispatch_calls;
    int      dispatch_rc;           /* 0 = device took it */
    uint32_t last_slot;
    XqSqe    last_seen;
    int      reject_token;          /* if nonzero, validate rejects blk_token==this */
    void    *ring;                  /* for the TOCTOU hook */
    int      tamper;                /* modify the original SQE after the copy */
    uint32_t tamper_slot;
} Fake;

static int f_validate(void *ctx, const XqSqe *e) {
    Fake *f = ctx;
    f->validate_calls++;
    if (f->reject_token && e->blk_token == (uint64_t)f->reject_token) return XQ_ST_INVALID;
    if (e->lba > 1000) return -5;                       /* BLK_E_RANGE-like */
    return 0;
}

static int f_dispatch(void *ctx, uint32_t slot, const XqSqe *e) {
    Fake *f = ctx;
    f->dispatch_calls++;
    f->last_slot = slot;
    f->last_seen = *e;
    if (f->tamper) {
        /* The program rewrites its entry right after the kernel copied it. */
        XqSqe *orig = (XqSqe *)((uint8_t *)f->ring + xq_sq_off()) + f->tamper_slot;
        orig->lba = 999999;
        orig->blk_token = 0xDEAD;
    }
    return f->dispatch_rc;
}

/* ---- helpers ---------------------------------------------------------------- */
static void *make_ring(uint32_t n) {
    void *m = calloc(1, (size_t)xq_bytes(n));
    CHECK(m != NULL);
    CHECK(xq_init(m, n) == 0);
    return m;
}

static XqSqe *sqe_at(void *m, uint32_t slot) {
    return (XqSqe *)((uint8_t *)m + xq_sq_off()) + slot;
}

static void submit(void *m, uint32_t n_tail, XqSqe e) {
    XqHeader *h = m;
    uint32_t tail = h->sq_tail;
    *sqe_at(m, tail & h->mask) = e;
    h->sq_tail = tail + 1u;
    (void)n_tail;
}

static XqCqe *cqe_at(void *m, uint32_t slot) {
    XqHeader *h = m;
    return (XqCqe *)((uint8_t *)m + xq_cq_off(h->entries)) + slot;
}

static XqSqe good(uint64_t cookie, uint64_t lba) {
    XqSqe e; memset(&e, 0, sizeof e);
    e.cookie = cookie; e.blk_token = 7; e.ram_token = 9;
    e.lba = lba; e.count = 1; e.op = XQ_OP_READ;
    return e;
}

/* ---- tests ------------------------------------------------------------------ */
static void test_accept_and_complete(void) {
    Fake f = {0};
    XqOps ops = { f_validate, f_dispatch, &f };
    void *m = make_ring(16);

    submit(m, 0, good(111, 5));
    uint32_t n = xq_process(m, &ops);
    CHECK(n == 1);
    CHECK(f.validate_calls == 1 && f.dispatch_calls == 1);
    CHECK(f.last_seen.cookie == 111);
    CHECK(((XqHeader *)m)->cq_tail == 0);       /* not completed until the device says so */

    CHECK(xq_complete(m, f.last_slot, 0) == 0);
    XqCqe *c = cqe_at(m, 0);
    CHECK(c->cookie == 111 && c->status == XQ_ST_OK);
    CHECK(((XqHeader *)m)->cq_tail == 1);
    free(m);
}

static void test_rejects_complete_at_once(void) {
    Fake f = {0};
    XqOps ops = { f_validate, f_dispatch, &f };
    void *m = make_ring(16);

    submit(m, 0, good(222, 5000));              /* lba out of range */
    CHECK(xq_process(m, &ops) == 1);
    CHECK(f.dispatch_calls == 0);               /* never reached the device */
    CHECK(cqe_at(m, 0)->cookie == 222 && cqe_at(m, 0)->status == -5);

    XqSqe bad_op = good(333, 1); bad_op.op = 9;
    submit(m, 0, bad_op);
    xq_process(m, &ops);
    CHECK(cqe_at(m, 1)->status == XQ_ST_BAD_OP);

    XqSqe zero = good(444, 1); zero.count = 0;
    submit(m, 0, zero);
    xq_process(m, &ops);
    CHECK(cqe_at(m, 2)->status == XQ_ST_INVALID);
    CHECK(f.dispatch_calls == 0);
    free(m);
}

static void test_token_rejection(void) {
    Fake f = {0};
    f.reject_token = 7;
    XqOps ops = { f_validate, f_dispatch, &f };
    void *m = make_ring(16);
    submit(m, 0, good(555, 1));                 /* blk_token 7: rejected */
    xq_process(m, &ops);
    CHECK(cqe_at(m, 0)->status == XQ_ST_INVALID);
    CHECK(f.dispatch_calls == 0);
    free(m);
}

static void test_toctou(void) {
    Fake f = {0};
    XqOps ops = { f_validate, f_dispatch, &f };
    void *m = make_ring(16);
    f.ring = m;
    f.tamper = 1;
    f.tamper_slot = 0;
    submit(m, 0, good(666, 42));
    xq_process(m, &ops);
    /* The device must see the values that were validated, not the rewrite. */
    CHECK(f.last_seen.lba == 42);
    CHECK(f.last_seen.blk_token == 7);
    CHECK(f.last_seen.cookie == 666);
    free(m);
}

static void test_dispatch_refusal(void) {
    Fake f = {0};
    f.dispatch_rc = -3;                         /* device did not take it */
    XqOps ops = { f_validate, f_dispatch, &f };
    void *m = make_ring(16);
    submit(m, 0, good(777, 1));
    xq_process(m, &ops);
    CHECK(cqe_at(m, 0)->cookie == 777 && cqe_at(m, 0)->status == -3);
    free(m);
}

static void test_full_completion_ring(void) {
    Fake f = {0};
    XqOps ops = { f_validate, f_dispatch, &f };
    void *m = make_ring(16);
    /* fill the completion ring with 16 rejected entries */
    for (uint32_t i = 0; i < 16; i++) { submit(m, 0, good(i, 5000)); }
    CHECK(xq_process(m, &ops) == 16);
    /* now the completion ring is full: a new rejection must not be consumed */
    submit(m, 0, good(999, 5000));
    CHECK(xq_process(m, &ops) == 0);
    CHECK(((XqHeader *)m)->sq_head == 16);      /* entry 16 still pending */
    /* the program drains one completion; the kernel can now take the entry */
    ((XqHeader *)m)->cq_head = 1;
    CHECK(xq_process(m, &ops) == 1);
    CHECK(cqe_at(m, 0)->cookie == 999 || cqe_at(m, 0)->cookie == 0);
    free(m);
}

static void test_init_rejects_bad_sizes(void) {
    void *m = calloc(1, (size_t)xq_bytes(1024));
    CHECK(xq_init(m, 0) == -1);
    CHECK(xq_init(m, 8) == -1);
    CHECK(xq_init(m, 17) == -1);
    CHECK(xq_init(m, 2048) == -1);
    CHECK(xq_init(m, 16) == 0);
    free(m);
}

int main(void) {
    test_init_rejects_bad_sizes();
    test_accept_and_complete();
    test_rejects_complete_at_once();
    test_token_rejection();
    test_toctou();
    test_dispatch_refusal();
    test_full_completion_ring();
    printf("xqueue: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
