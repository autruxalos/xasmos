/* Host test for re-entrancy of kr_process. The device submit may complete the
 * very slot it was handed (a synchronous device), or call kr_process again on
 * the same ring. Neither may lose a completion, leak a pin, or dispatch twice.
 * Runs against the real xring.c, xqueue.c and token ledger. */
#include "../../../source/arch/amd64/kernel/xeret/xring.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_stubs.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define OWNER 2u
#define N     16u

static BlkDev dev;
static KRing  r;
static void  *mem;

/* Scenario switch: 1 = complete the slot inside submit; 2 = nested kr_process. */
static int mode;
static int submit_calls;
static int inner_rc;

static int dev_submit(void *arg, uint32_t slot, const XqSqe *e) {
    KRing *k = (KRing *)arg;
    (void)e;
    submit_calls++;
    if (mode == 1) {
        inner_rc = kr_complete(k, slot, 0);          /* device answers at once */
    } else if (mode == 2 && submit_calls == 1) {
        inner_rc = (int)kr_process(k);               /* re-enter the same ring */
    }
    return 0;
}

static void ring_new(void) {
    mem = calloc(1, (size_t)xq_bytes(N));
    xq_init(mem, N);
    kr_init(&r, OWNER, mem, N, dev_submit, &r);
    submit_calls = 0;
    inner_rc = -99;
}

static void ring_free(void) { free(mem); mem = NULL; }

static XqSqe ent(uint64_t cookie) {
    XqSqe e;
    memset(&e, 0, sizeof e);
    e.cookie    = cookie;
    e.blk_token = xtok_alloc_blocks(OWNER, &dev, 0, 8, XTOK_RIGHTS_BLOCKS_DEFAULT);
    e.ram_token = xtok_alloc_ram(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    e.count = 1;
    e.op = XQ_OP_READ;
    return e;
}

static void submit_entry(XqSqe e) {
    XqHeader *h = (XqHeader *)mem;
    XqSqe *s = (XqSqe *)((uint8_t *)mem + xq_sq_off()) + (h->sq_tail & h->mask);
    *s = e;
    h->sq_tail = h->sq_tail + 1u;
}

static XqCqe *cqe_at(uint32_t slot) {
    XqHeader *h = (XqHeader *)mem;
    return (XqCqe *)((uint8_t *)mem + xq_cq_off(h->entries)) + slot;
}

/* A device that answers inside submit must end with a posted completion, no
 * inflight entry, and both pins released. */
static void test_complete_inside_submit(void) {
    ring_new();
    mode = 1;
    XqSqe a = ent(42);
    submit_entry(a);

    CHECK(kr_process(&r) == 1);
    CHECK(submit_calls == 1);
    CHECK(inner_rc == 0);                                   /* completion accepted */
    CHECK(((XqHeader *)mem)->cq_tail == 1);                 /* CQE posted */
    CHECK(cqe_at(0)->cookie == 42 && cqe_at(0)->status == 0);
    CHECK(kr_inflight(&r) == 0);                            /* nothing left in flight */
    CHECK(xtok_release(a.blk_token) == 0);                  /* pins gone */
    CHECK(xtok_release(a.ram_token) == 0);
    ring_free();
}

/* A nested kr_process on the same ring must not dispatch the same slot again. */
static void test_nested_process_does_not_double_dispatch(void) {
    ring_new();
    mode = 2;
    XqSqe a = ent(7);
    submit_entry(a);

    CHECK(kr_process(&r) == 1);
    CHECK(submit_calls == 1);                               /* dispatched once */
    CHECK(inner_rc == 0);                                   /* nested pass saw a busy slot */
    CHECK(((XqHeader *)mem)->sq_head == 1);
    CHECK(kr_inflight(&r) == 1);                            /* still owned by the device */
    CHECK(kr_complete(&r, 0, 0) == 0);
    CHECK(kr_inflight(&r) == 0);
    CHECK(xtok_release(a.blk_token) == 0);
    CHECK(xtok_release(a.ram_token) == 0);
    ring_free();
}

int main(void) {
    host_dev_init(&dev, 512, 1024);
    xtok_init();
    test_complete_inside_submit();
    test_nested_process_does_not_double_dispatch();
    printf("xring_reentry: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
