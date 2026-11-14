/* Host test for the device vector registry (core/irq/irqvec.c). */
#include <stdio.h>
#include "irqvec.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int  calls;
static void *last_ctx;
static void notice_fn(void *ctx) { calls++; last_ctx = ctx; }

static void test_alloc_exhaust_and_reuse(void) {
    int got[IRQ_DEV_LAST - IRQ_DEV_FIRST + 1];
    int n = 0, v;
    while ((v = irq_vector_alloc(notice_fn, NULL)) >= 0) {
        CHECK(v >= (int)IRQ_DEV_FIRST && v <= (int)IRQ_DEV_LAST);
        for (int i = 0; i < n; i++) CHECK(got[i] != v);   /* never repeated */
        got[n++] = v;
    }
    CHECK(n == (int)(IRQ_DEV_LAST - IRQ_DEV_FIRST + 1));  /* all 0x41..0xFE */
    CHECK(irq_vector_alloc(notice_fn, NULL) == -1);        /* exhausted */

    irq_vector_free((uint32_t)got[5]);
    CHECK(irq_vector_alloc(notice_fn, NULL) == got[5]);    /* reused */

    for (int i = 0; i < n; i++) irq_vector_free((uint32_t)got[i]);
}

static void test_notice_counts_and_take(void) {
    int v = irq_vector_alloc(notice_fn, (void *)0x1234);
    CHECK(v >= 0);
    calls = 0;

    CHECK(irq_vector_notice((uint32_t)v) == 1);
    CHECK(irq_vector_notice((uint32_t)v) == 1);
    CHECK(calls == 2);
    CHECK(last_ctx == (void *)0x1234);

    CHECK(irq_vector_take((uint32_t)v) == 2);   /* both kept, both taken */
    CHECK(irq_vector_take((uint32_t)v) == 0);   /* nothing left */

    irq_vector_free((uint32_t)v);
}

static void test_notice_on_free_or_reserved(void) {
    calls = 0;
    CHECK(irq_vector_notice(IRQ_DEV_FIRST) == 0);   /* free: ignored */
    CHECK(irq_vector_notice(0x40) == 0);            /* LAPIC timer: not a device */
    CHECK(irq_vector_notice(0xFF) == 0);            /* spurious */
    CHECK(irq_vector_notice(0x10) == 0);            /* exception range */
    CHECK(calls == 0);
}

static void test_free_forgets_pending(void) {
    int v = irq_vector_alloc(notice_fn, NULL);
    CHECK(v >= 0);
    irq_vector_notice((uint32_t)v);
    irq_vector_free((uint32_t)v);
    CHECK(irq_vector_take((uint32_t)v) == 0);       /* freed: count is gone */
    CHECK(irq_vector_notice((uint32_t)v) == 0);     /* and no longer delivered */
}

int main(void) {
    test_alloc_exhaust_and_reuse();
    test_notice_counts_and_take();
    test_notice_on_free_or_reserved();
    test_free_forgets_pending();
    printf("irqvec: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
