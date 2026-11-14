/******************************************************************************
 * XASMOS - device interrupt vector registry (portable)
 * Path: source/core/irq/irqvec.h
 *
 * Vectors 0x41..0xFE belong to device drivers. A driver reserves one, and the
 * dispatcher counts each notice on it. A notice that arrives while the driver
 * is busy is kept in the count, so it is never lost. The count is only ever
 * decreased by what the driver took, never reset.
 ******************************************************************************/
#ifndef XASMOS_IRQVEC_H
#define XASMOS_IRQVEC_H

#include <stdint.h>

#define IRQ_DEV_FIRST 0x41u
#define IRQ_DEV_LAST  0xFEu

typedef void (*IrqNoticeFn)(void *ctx);

/* Takes the first free device vector. Returns the vector, or -1 if none. */
int      irq_vector_alloc(IrqNoticeFn notice, void *ctx);

/* Releases a vector and forgets its pending count. */
void     irq_vector_free(uint32_t v);

/* Counts one notice on v and wakes its driver. Returns 1 if v belongs to a
 * driver, 0 otherwise. Safe to call from an interrupt handler. */
int      irq_vector_notice(uint32_t v);

/* Takes the notices the driver has seen. Returns how many were pending. */
uint32_t irq_vector_take(uint32_t v);

#endif /* XASMOS_IRQVEC_H */
