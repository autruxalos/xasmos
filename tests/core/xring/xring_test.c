/* Host test for the kernel ring: pin on validation, release on refusal,
 * release on completion, and no loss when the completion ring is full. The
 * device submission is injected; the real NVMe adapter will replace it later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../source/arch/amd64/kernel/xeret/xring.c"
#include "host_stubs.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define OWNER 2u

static BlkDev dev;
static void  *ring_mem;
static KRing  r;

typedef struct {
    int      calls;
    uint32_t slot;
    XqSqe    seen;
    int      rc;               /* 0 accepts, anything else refuses */
} DevFake;

static int dev_submit(void *arg, uint32_t slot, const XqSqe *e) {
    DevFake *d = arg;
    d->calls++;
    d->slot = slot;
    d->seen = *e;
    return d->rc;
}

static void ring_new(uint32_t n, KrSubmitFn fn, void *arg) {
    ring_mem = calloc(1, (size_t)xq_bytes(n));
    xq_init(ring_mem, n);
    kr_init(&r, OWNER, ring_mem, n, fn, arg);
}
static void ring_free(void) { free(ring_mem); ring_mem = NULL; }

static XqSqe *sqe_at(uint32_t slot) {
    return (XqSqe *)((uint8_t *)ring_mem + xq_sq_off()) + slot;
}
static XqCqe *cqe_at(uint32_t slot) {
    XqHeader *h = ring_mem;
    return (XqCqe *)((uint8_t *)ring_mem + xq_cq_off(h->entries)) + slot;
}
static void submit(XqSqe e) {
    XqHeader *h = ring_mem;
    *sqe_at(h->sq_tail & h->mask) = e;
    h->sq_tail = h->sq_tail + 1u;
}
static XqSqe ent(uint64_t cookie, XToken b, XToken m, uint64_t lba, uint32_t count, uint8_t op) {
    XqSqe e; memset(&e, 0, sizeof e);
    e.cookie = cookie; e.blk_token = b; e.ram_token = m;
    e.lba = lba; e.count = count; e.op = op;
    return e;
}
static XToken blk_tok(void)  { return xtok_alloc_blocks(OWNER, &dev, 0, 100, XTOK_RIGHTS_BLOCKS_DEFAULT); }
static XToken ram_tok(void)  { return xtok_alloc_ram(OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT); }

static void test_device_refuses(void) {
    DevFake d = { .rc = -1 };
    ring_new(16, dev_submit, &d);
    XToken b = blk_tok(), m = ram_tok();

    submit(ent(10, b, m, 0, 1, XTOK_BLK_READ));
    CHECK(kr_process(&r) == 1);
    CHECK(d.calls == 1);
    CHECK(cqe_at(0)->cookie == 10 && cqe_at(0)->status == BLK_E_IO);
    CHECK(xtok_release(b) == 0);          /* pins undone at refusal */
    CHECK(xtok_release(m) == 0);
    ring_free();
}

static void test_device_default_refuses(void) {
    ring_new(16, NULL, NULL);             /* no submit given: default refuses */
    XToken b = blk_tok(), m = ram_tok();
    submit(ent(11, b, m, 0, 1, XTOK_BLK_READ));
    CHECK(kr_process(&r) == 1);
    CHECK(cqe_at(0)->cookie == 11 && cqe_at(0)->status == BLK_E_IO);
    CHECK(xtok_release(b) == 0 && xtok_release(m) == 0);
    ring_free();
}

static void test_accept_then_complete(void) {
    DevFake d = { .rc = 0 };
    ring_new(16, dev_submit, &d);
    XToken b = blk_tok(), m = ram_tok();

    submit(ent(20, b, m, 0, 1, XTOK_BLK_READ));
    CHECK(kr_process(&r) == 1);
    CHECK(d.calls == 1 && d.seen.cookie == 20);
    CHECK(((XqHeader *)ring_mem)->cq_tail == 0);        /* not done yet */
    CHECK(xtok_release(b) == XTOK_REL_EBUSY);           /* device holds the pins */
    CHECK(xtok_release(m) == XTOK_REL_EBUSY);

    CHECK(kr_complete(&r, d.slot, 0) == 0);
    CHECK(cqe_at(0)->cookie == 20 && cqe_at(0)->status == 0);
    CHECK(xtok_release(b) == 0 && xtok_release(m) == 0);
    ring_free();
}

static void test_rejections_never_reach_device(void) {
    DevFake d = { .rc = 0 };
    ring_new(16, dev_submit, &d);
    XToken b = blk_tok(), m = ram_tok();

    submit(ent(30, b, m, 500, 1, XTOK_BLK_READ));       /* lba out of range */
    CHECK(kr_process(&r) == 1);
    CHECK(cqe_at(0)->cookie == 30 && cqe_at(0)->status == XQ_ST_INVALID);
    CHECK(d.calls == 0);
    CHECK(xtok_release(b) == 0 && xtok_release(m) == 0);   /* nothing left pinned */
    ring_free();
}

static void test_mapped_frame_rejected(void) {
    DevFake d = { .rc = 0 };
    ring_new(16, dev_submit, &d);
    XToken b = blk_tok(), m = ram_tok();
    UINT64 p = 0, f = 0;

    CHECK(xtok_map_acquire(m, OWNER, XTOK_RIGHT_READ, &p, &f) == XTOK_MAP_OK);
    submit(ent(40, b, m, 0, 1, XTOK_BLK_READ));
    CHECK(kr_process(&r) == 1);
    CHECK(cqe_at(0)->status == XQ_ST_INVALID && d.calls == 0);
    xtok_map_release(m);
    CHECK(xtok_release(b) == 0 && xtok_release(m) == 0);
    ring_free();
}

static void test_full_completion_ring(void) {
    DevFake d = { .rc = 0 };
    ring_new(16, dev_submit, &d);
    XToken b = blk_tok(), m = ram_tok();

    /* 15 rejections fill slots 0..14: the completion ring has one slot left. */
    for (uint32_t i = 0; i < 15; i++) submit(ent(i + 1, XTOKEN_INVALID, m, 0, 1, XTOK_BLK_READ));
    CHECK(kr_process(&r) == 15);

    /* Slot 15 is accepted and goes to the device: it stays in flight. */
    submit(ent(100, b, m, 0, 1, XTOK_BLK_READ));
    CHECK(kr_process(&r) == 1);
    CHECK(d.calls == 1);
    uint32_t slot = d.slot;

    /* One more rejection (slot 0) fills the completion ring. */
    submit(ent(99, XTOKEN_INVALID, m, 0, 1, XTOK_BLK_READ));
    CHECK(kr_process(&r) == 1);
    CHECK(((XqHeader *)ring_mem)->cq_tail == 16);

    /* The device finishes, but there is no room: the pins must stay. */
    CHECK(kr_complete(&r, slot, 0) == -1);
    CHECK(xtok_release(b) == XTOK_REL_EBUSY);
    CHECK(kr_inflight(&r) == 1);

    /* The program consumes one completion; the retry succeeds. */
    ((XqHeader *)ring_mem)->cq_head = 1;
    CHECK(kr_complete(&r, slot, 0) == 0);
    CHECK(cqe_at(0)->cookie == 100 && cqe_at(0)->status == 0);
    CHECK(kr_inflight(&r) == 0);
    CHECK(xtok_release(b) == 0 && xtok_release(m) == 0);
    ring_free();
}

int main(void) {
    host_dev_init(&dev, 512, 1024);
    xtok_init();
    test_device_refuses();
    test_device_default_refuses();
    test_accept_then_complete();
    test_rejections_never_reach_device();
    test_mapped_frame_rejected();
    test_full_completion_ring();
    printf("xring: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
