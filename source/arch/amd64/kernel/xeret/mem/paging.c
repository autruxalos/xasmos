/******************************************************************************
 * XASMOS - Own page tables (amd64 Long Mode, 2 MiB huge pages default)
 * Path: source|arch|amd64|kernel|xeret|mem|paging.c
 *
 * C builds the tables. Inline ASM only loads CR3 (C cannot express that).
 * Replaces firmware page tables after ExitBootServices.
 ******************************************************************************/
#include "kmemory.h"
#include "paging.h"

/* 512 entries per table level */
#define XPT_ENTS 512ULL

/* Physical address of installed PML4 (0 = still on UEFI tables) */
static UINT64 g_pml4_phys;

/* Zero one 4 KiB frame and return its physical address (0 on failure). */
static UINT64 alloc_zero_table(void) {
    UINT64 phys;
    volatile UINT64 *p;
    UINT64 i;

    phys = phys_alloc_frames(1);
    if (phys == 0) {
        return 0;
    }
    /* Identity-mapped under UEFI tables until we switch — safe to write. */
    p = (volatile UINT64 *)(UINTN)phys;
    for (i = 0; i < (XERET_FRAME_SIZE / sizeof(UINT64)); i++) {
        p[i] = 0;
    }
    return phys;
}

static void write_cr3(UINT64 pml4_phys) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

UINT64 xpt_pml4_phys(void) {
    return g_pml4_phys;
}

/*
 * Identity map [0, phys_end) with 2 MiB pages.
 *
 * Hierarchy:
 *   PML4[i] -> PDPT  (1 GiB region each)
 *   PDPT[j] -> PD    (2 MiB * 512 = 1 GiB)
 *   PD[k]   -> 2 MiB page (PS=1), phys = i*512GiB + j*1GiB + k*2MiB
 *
 * We only cover what phys_end requires; no speculative map of the full 256 TiB.
 */
int xpt_init_identity(UINT64 phys_end, UINT64 extra_base, UINT64 extra_size) {
    UINT64 pml4_phys, pdpt_phys, pd_phys;
    volatile UINT64 *pml4, *pdpt, *pd;
    UINT64 mapped_end, addr;
    UINT64 pml4_i, pdpt_i, pd_i;
    UINT64 gb, huge;

    if (phys_end == 0) {
        return -1;
    }

    /* Round up to 2 MiB boundary so the last huge page covers phys_end. */
    mapped_end = (phys_end + (XERET_HUGE_SIZE - 1)) & ~(XERET_HUGE_SIZE - 1);

    pml4_phys = alloc_zero_table();
    if (pml4_phys == 0) {
        return -1;
    }
    pml4 = (volatile UINT64 *)(UINTN)pml4_phys;

    /* Walk every 2 MiB chunk that must be present */
    for (addr = 0; addr < mapped_end; addr += XERET_HUGE_SIZE) {
        pml4_i = (addr >> 39) & 0x1FFULL;
        pdpt_i = (addr >> 30) & 0x1FFULL;
        pd_i   = (addr >> 21) & 0x1FFULL;

        /* Ensure PDPT for this PML4 slot */
        if ((pml4[pml4_i] & XPT_PRESENT) == 0) {
            pdpt_phys = alloc_zero_table();
            if (pdpt_phys == 0) {
                return -1;
            }
            pml4[pml4_i] = pdpt_phys | XPT_KERNEL_RW;
        } else {
            pdpt_phys = pml4[pml4_i] & 0x000FFFFFFFFFF000ULL;
        }
        pdpt = (volatile UINT64 *)(UINTN)pdpt_phys;

        /* Ensure PD for this PDPT slot */
        if ((pdpt[pdpt_i] & XPT_PRESENT) == 0) {
            pd_phys = alloc_zero_table();
            if (pd_phys == 0) {
                return -1;
            }
            pdpt[pdpt_i] = pd_phys | XPT_KERNEL_RW;
        } else {
            pd_phys = pdpt[pdpt_i] & 0x000FFFFFFFFFF000ULL;
        }
        pd = (volatile UINT64 *)(UINTN)pd_phys;

        /* 2 MiB page: phys == virt for identity */
        huge = addr | XPT_KERNEL_RW | XPT_HUGE | XPT_GLOBAL;
        pd[pd_i] = huge;

        (void)gb; /* silence unused if compiler is picky about locals */
    }

    /* Switch: C cannot write CR3 without this single instruction. */
    write_cr3(pml4_phys);
    g_pml4_phys = pml4_phys;
    return 0;
}
