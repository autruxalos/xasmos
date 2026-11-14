/* Host test for the in-flight counter of a kernel ring: it rises on dispatch,
 * falls only after the completion and the pins, and refuses a slot that is not
 * in flight. Runs against the real xring.c and token ledger. */
#include "../../../source/arch/amd64/kernel/xeret/xring.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_stubs.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define OWNER 2u

static BlkDev dev;
static void  *mem;
static KRing  r;
static int    dev_rc;
static uint32_t dev_slot;

static int dev_submit(void *arg, uint32_t slot, const XqSqe *e) {
    (void)arg; (void)e;
    dev_slot = slot;
    return dev_rc;
}

static void ring_new(void) {
    mem = calloc(1, (size_t)xq_bytes(16));
    xq_init(mem, 16);
    dev_rc = 0;
    kr_init(&r, OWNER, mem, 16, dev_submit, 0);
}

static void ring_free(void) { free(mem); mem = NULL; }

static XqSqe *sqe_at(uint32_t slot) {
    return (XqSqe *)((uint8_t *)mem + xq_sq_off()) + slot;
}

static void submit(XqSqe e) {
    XqHeader *h = mem;
    *sqe_at(h->sq_tail & h->mask) = e;
    h->sq_tail = h->sq_tail + 1u;
}

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

static void test_rises_and_falls(void) {
    ring_new();
    XqSqe a = ent(1), b = ent(2);
    CHECK(kr_inflight(&r) == 0);

    submit(a);
    CHECK(kr_process(&r) == 1);
    CHECK(kr_inflight(&r) == 1);
    uint32_t slot_a = dev_slot;

    submit(b);
    CHECK(kr_process(&r) == 1);
    CHECK(kr_inflight(&r) == 2);
    uint32_t slot_b = dev_slot;

    CHECK(kr_complete(&r, slot_a, 0) == 0);
    CHECK(kr_inflight(&r) == 1);
    CHECK(xtok_release(a.blk_token) == 0);     /* pins gone before the count fell */
    CHECK(xtok_release(a.ram_token) == 0);

    CHECK(kr_complete(&r, slot_b, 0) == 0);
    CHECK(kr_inflight(&r) == 0);
    CHECK(xtok_release(b.blk_token) == 0);
    CHECK(xtok_release(b.ram_token) == 0);
    ring_free();
}

static void test_not_in_flight_is_refused(void) {
    ring_new();
    CHECK(kr_complete(&r, 5, 0) == -1);        /* never dispatched */
    CHECK(kr_inflight(&r) == 0);
    ring_free();
}

static void test_double_complete_is_refused(void) {
    ring_new();
    XqSqe a = ent(1), b = ent(2);
    submit(a);
    kr_process(&r);
    uint32_t slot_a = dev_slot;
    submit(b);
    kr_process(&r);

    CHECK(kr_complete(&r, slot_a, 0) == 0);
    CHECK(kr_complete(&r, slot_a, 0) == -1);   /* already completed */
    CHECK(kr_inflight(&r) == 1);               /* the second entry still counts */
    ring_free();
}

static void test_refused_by_device_does_not_count(void) {
    ring_new();
    dev_rc = -1;
    XqSqe a = ent(1);
    submit(a);
    CHECK(kr_process(&r) == 1);                /* completed with an error */
    CHECK(kr_inflight(&r) == 0);
    CHECK(xtok_release(a.blk_token) == 0);
    CHECK(xtok_release(a.ram_token) == 0);
    ring_free();
}

static void test_busy_does_not_count(void) {
    ring_new();
    dev_rc = XQ_ST_BUSY;
    XqSqe a = ent(1);
    submit(a);
    CHECK(kr_process(&r) == 0);                /* stays queued */
    CHECK(kr_inflight(&r) == 0);
    CHECK(xtok_release(a.blk_token) == 0);
    CHECK(xtok_release(a.ram_token) == 0);
    ring_free();
}

int main(void) {
    host_dev_init(&dev, 512, 1024);
    xtok_init();
    test_rises_and_falls();
    test_not_in_flight_is_refused();
    test_double_complete_is_refused();
    test_refused_by_device_does_not_count();
    test_busy_does_not_count();
    printf("xring_inflight: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
