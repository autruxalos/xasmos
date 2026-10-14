/******************************************************************************
 * XASMOS - xeret page-table API
 * Path: source/kerncall/include/paging.h
 ******************************************************************************/
#ifndef XASMOS_PAGING_H
#define XASMOS_PAGING_H

#include "efi_types.h"

#define XPT_PRESENT   (1ULL << 0)
#define XPT_WRITE     (1ULL << 1)
#define XPT_USER      (1ULL << 2)
#define XPT_HUGE      (1ULL << 7)
#define XPT_GLOBAL    (1ULL << 8)

#define XPT_KERNEL_RW  (XPT_PRESENT | XPT_WRITE)

/* Identity-map [0, phys_end) with 2 MiB pages, plus optional extra range (GOP FB).
 * Loads CR3. Returns 0 on success. */
int    xpt_init_identity(UINT64 phys_end, UINT64 extra_base, UINT64 extra_size);

UINT64 xpt_pml4_phys(void);

#endif
