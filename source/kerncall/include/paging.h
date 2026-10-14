/******************************************************************************
 * XASMOS - xeret page-table API (arch-neutral surface)
 * Path: source|kerncall|include|paging.h
 *
 * Policy: huge pages (2 MiB) by default. 4 KiB only when a LibOS requests
 * fine-grained MAP rights later. No VFS, no demand paging policy in xeret.
 ******************************************************************************/
#ifndef XASMOS_PAGING_H
#define XASMOS_PAGING_H

#include "efi_types.h"

/* x86-64 entry flags (shared numeric values; layout is amd64-specific in .c) */
#define XPT_PRESENT   (1ULL << 0)
#define XPT_WRITE     (1ULL << 1)
#define XPT_USER      (1ULL << 2)
#define XPT_PWT       (1ULL << 3)
#define XPT_PCD       (1ULL << 4)
#define XPT_ACCESSED  (1ULL << 5)
#define XPT_DIRTY     (1ULL << 6)
#define XPT_HUGE      (1ULL << 7)   /* PS: 2 MiB in PD, 1 GiB in PDPT */
#define XPT_GLOBAL    (1ULL << 8)
#define XPT_NX        (1ULL << 63)

#define XPT_KERNEL_RW  (XPT_PRESENT | XPT_WRITE)
#define XPT_KERNEL_RX  (XPT_PRESENT)

/* Install identity map covering [0, phys_end) with 2 MiB pages and load CR3.
 * Returns 0 on success. Must run after phys_init + xtok_init. */
int  xpt_init_identity(UINT64 phys_end);

/* Current PML4 physical address (0 if not installed). */
UINT64 xpt_pml4_phys(void);

#endif /* XASMOS_PAGING_H */
