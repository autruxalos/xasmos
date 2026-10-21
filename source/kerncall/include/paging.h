/******************************************************************************
 * XASMOS - xeret page tables and per-owner address spaces
 * Path: source|kerncall|include|paging.h
 *
 * The kernel identity map (supervisor-only) lives in PML4[0] and is shared by
 * every address space. User mappings live at PML4 index >= 1, built per owner
 * from tokens. A mapping's hardware permissions are DERIVED from the token's
 * rights; nothing else can create one.
 ******************************************************************************/
#ifndef XASMOS_PAGING_H
#define XASMOS_PAGING_H

#include "efi_types.h"
#include "capability.h"

#define XPT_PRESENT   (1ULL << 0)
#define XPT_WRITE     (1ULL << 1)
#define XPT_USER      (1ULL << 2)
#define XPT_PWT       (1ULL << 3)
#define XPT_PCD       (1ULL << 4)
#define XPT_HUGE      (1ULL << 7)
#define XPT_GLOBAL    (1ULL << 8)
#define XPT_NX        (1ULL << 63)

#define XPT_KERNEL_RW (XPT_PRESENT | XPT_WRITE)
#define XPT_UC        (XPT_PCD | XPT_PWT)

/* Requested access for xas_map. x86 cannot express write-only or
 * execute-only, so every mapping is readable and also needs the token's READ
 * right. Write+execute together is refused (W^X). */
#define XPERM_R  1u
#define XPERM_W  2u
#define XPERM_X  4u

/* xas_* results (negative = failure) */
#define XAS_OK            0
#define XAS_ERR_ARG      (-1)   /* null/dead aspace, bad permission bits      */
#define XAS_ERR_WX       (-2)   /* W and X together                           */
#define XAS_ERR_RANGE    (-3)   /* unaligned, kernel range, or past user top  */
#define XAS_ERR_TOKEN    (-4)   /* stale/invalid token, or not RAM            */
#define XAS_ERR_OWNER    (-5)   /* token belongs to another owner             */
#define XAS_ERR_RIGHTS   (-6)   /* token lacks MAP or a requested R/W/X       */
#define XAS_ERR_BUSY     (-7)   /* virtual address already mapped             */
#define XAS_ERR_NOMEM    (-8)   /* no frame for a table / no mapping record   */
#define XAS_ERR_NOTMAPPED (-9)

/* User virtual range: [512 GiB, 128 TiB). */
#define XAS_USER_BASE  0x0000008000000000ULL
#define XAS_USER_TOP   0x0000800000000000ULL

/* Hardware protections actually switched on by xpt_init_identity. */
#define XPROT_NX    1u
#define XPROT_WP    2u
#define XPROT_SMEP  4u

typedef struct {
    UINT64 pml4_phys;
    UINT32 owner_id;     /* never XTOK_OWNER_KERNEL */
    UINT32 live;
} XAspace;

/* Enables NX/WP(/SMEP), builds the kernel identity map and loads CR3:
 *   [0, phys_end)                    write-back RAM
 *   [extra_base, extra_base+size)    uncached (GOP framebuffer)
 *   [0xFEC00000, 4 GiB)              uncached (IOAPIC/LAPIC MMIO)
 * Returns 0, or -1 on failure -- including a CPU without NX: no NX, no boot.
 * Everything must fit in PML4[0] (first 512 GiB). */
int    xpt_init_identity(UINT64 phys_end, UINT64 extra_base, UINT64 extra_size);
UINT32 xpt_protections(void);

int    xpt_translate(UINT64 virt, UINT64 *out_phys);
UINT64 xpt_pml4_phys(void);
UINT64 xpt_read_cr3(void);

int    xas_create(XAspace *as, UINT32 owner_id);
void   xas_destroy(XAspace *as);
int    xas_map(XAspace *as, UINT64 virt, XToken tok, UINT32 perms);
int    xas_unmap(XAspace *as, UINT64 virt);
/* Kernel-initiated revocation: removes every mapping of tok from every
 * address space. Returns how many mappings were removed. */
int    xas_revoke_token(XToken tok);

#endif
