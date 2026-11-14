/******************************************************************************
 * XASMOS - device interrupt vector registry (portable)
 * Path: source/core/irq/irqvec.c
 ******************************************************************************/
#include "irqvec.h"

typedef struct {
    IrqNoticeFn notice;
    void       *ctx;
    uint32_t    pending;
    uint8_t     used;
} IrqSlot;

static IrqSlot g_irq_slots[256];

int irq_vector_alloc(IrqNoticeFn notice, void *ctx) {
    for (uint32_t v = IRQ_DEV_FIRST; v <= IRQ_DEV_LAST; v++) {
        if (!g_irq_slots[v].used) {
            g_irq_slots[v].notice  = notice;
            g_irq_slots[v].ctx     = ctx;
            g_irq_slots[v].pending = 0;
            g_irq_slots[v].used    = 1;
            return (int)v;
        }
    }
    return -1;
}

void irq_vector_free(uint32_t v) {
    if (v < IRQ_DEV_FIRST || v > IRQ_DEV_LAST) return;
    g_irq_slots[v].used    = 0;
    g_irq_slots[v].notice  = 0;
    g_irq_slots[v].ctx     = 0;
    g_irq_slots[v].pending = 0;
}

int irq_vector_notice(uint32_t v) {
    if (v < IRQ_DEV_FIRST || v > IRQ_DEV_LAST) return 0;
    if (!g_irq_slots[v].used) return 0;
    __atomic_fetch_add(&g_irq_slots[v].pending, 1u, __ATOMIC_SEQ_CST);
    if (g_irq_slots[v].notice) g_irq_slots[v].notice(g_irq_slots[v].ctx);
    return 1;
}

uint32_t irq_vector_take(uint32_t v) {
    uint32_t n;
    if (v < IRQ_DEV_FIRST || v > IRQ_DEV_LAST) return 0;
    n = __atomic_load_n(&g_irq_slots[v].pending, __ATOMIC_SEQ_CST);
    while (!__atomic_compare_exchange_n(&g_irq_slots[v].pending, &n, 0,
                                        0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        ;
    return n;
}
