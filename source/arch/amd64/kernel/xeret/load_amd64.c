/******************************************************************************
 * XASMOS - loader backend for amd64 (xload_backend.h on the real kernel)
 * Path: source/arch/amd64/kernel/xeret/load_amd64.c
 *
 * Every function here is a thin adapter: the policy (quota, rollback order,
 * permissions) lives in core/exb/load.c and in the ledger and paging code.
 ******************************************************************************/
#include <stdint.h>
#include "efi_types.h"
#include "capability.h"
#include "mem/budget.h"
#include "mem/budget.h"
#include "paging.h"
#include "xload.h"
#include "xload_backend.h"

/* Exported by sys.c: the per-owner limits that the syscall layer enforces. */
/* The backend permission bits must mean the same as the paging ones. */
_Static_assert(XLB_PERM_R == XPERM_R && XLB_PERM_W == XPERM_W && XLB_PERM_X == XPERM_X,
               "XLB_PERM_* must match XPERM_*");

int xlb_quota_ok(uint32_t owner, uint64_t budget, uint64_t frames, uint32_t tokens) {
    UINT64 bal = 0;
    (void)tokens;
    if (xtok_budget_balance((XToken)budget, owner, &bal) != 0) return 0;
    if (frames > bal) return 0;
    return 1;
}

int xlb_alloc(uint32_t owner, uint64_t budget, uint64_t frames, uint32_t rights, uint64_t *token) {
    XToken t = XTOKEN_INVALID;
    if (xtok_alloc_ram_from(owner, (XToken)budget, frames, rights, &t) != 0) return XLOAD_E_NOMEM;   /* frames come zeroed */
    if (t == XTOKEN_INVALID) return XLOAD_E_NOMEM;
    *token = t;
    return 0;
}

/* Valid only for a live token from xlb_alloc: the loader never calls it
 * otherwise. RAM is identity-mapped in the kernel, so the physical address
 * is directly writable. */
void *xlb_kernel_ptr(uint64_t token) {
    UINT64 phys = 0;
    (void)xtok_ram_info(token, &phys, 0);
    return (void *)(UINTN)phys;
}

void xlb_release(uint64_t token) {
    /* The loader unmaps before it releases, so reclaim cannot refuse for
     * mapping reasons. The result is ignored on purpose: a failed release
     * here is a ledger bug, and the ledger already reports it through
     * xtok_live_count() in the tests. */
    (void)xtok_reclaim(token);
}

int xlb_map(XAspace *as, uint64_t vaddr, uint64_t token, uint32_t perms) {
    return xas_map(as, vaddr, token, perms) == XAS_OK ? 0 : XLOAD_E_MAP;
}

int xlb_unmap(XAspace *as, uint64_t vaddr) {
    return xas_unmap(as, vaddr) == XAS_OK ? 0 : -1;
}
