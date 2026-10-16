/******************************************************************************
 * XASMOS - Minimal spinlock (xeret)
 * Path: source|kerncall|include|spinlock.h
 *
 * One `lock`-prefixed instruction per acquire/release, nothing else.
 * Coarse-grained by design: one lock per structure (bitmap, token table),
 * not per-core sharding. Correct uniformly across every x86-64 CPU that
 * has the `lock` prefix -- which is every one since the 486, Core 2 Quad
 * included. No topology discovery, no NUMA awareness, no abstraction
 * beyond the compiler's __atomic builtins mapping straight to hardware.
 ******************************************************************************/
#ifndef XASMOS_SPINLOCK_H
#define XASMOS_SPINLOCK_H

#include "efi_types.h"

typedef volatile UINT32 XSpinlock;

#define XSPINLOCK_INIT 0u

static inline void xspin_lock(XSpinlock *l) {
    while (__atomic_exchange_n((UINT32 *)l, 1u, __ATOMIC_ACQUIRE)) {
        while (*l) {
            __asm__ volatile ("pause");
        }
    }
}

static inline void xspin_unlock(XSpinlock *l) {
    __atomic_store_n((UINT32 *)l, 0u, __ATOMIC_RELEASE);
}

#endif /* XASMOS_SPINLOCK_H */
