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
#include "paging.h"
#include "xload.h"
#include "xload_backend.h"

/* Exported by sys.c: the per-owner limits that the syscall layer enforces. */
extern void xproc_quota_get(UINT32 owner, UINT64 *max_frames, UINT32 *max_tokens);

/* The backend permission bits must mean the same as the paging ones. */
_Static_assert(XLB_PERM_R == XPERM_R && XLB_PERM_W == XPERM_W && XLB_PERM_X == XPERM_X,
               "XLB_PERM_* must match XPERM_*");

int xlb_quota_ok(uint32_t owner, uint64_t frames, uint32_t tokens) {
    UINT64 max_frames, used_frames;
    UINT32 max_tokens, used_tokens;

    xproc_quota_get(owner, &max_frames, &max_tokens);
    xtok_owner_usage(owner, &used_frames, &used_tokens);

    if (used_frames > max_frames || used_tokens > max_tokens) return 0;
    if (frames > max_frames - used_frames) return 0;   /* no overflow: used <= max */
    if (tokens > max_tokens - used_tokens) return 0;
    return 1;
}

int xlb_alloc(uint32_t owner, uint64_t frames, uint32_t rights, uint64_t *token) {
    XToken t = xtok_alloc_ram(owner, frames, rights);   /* frames come zeroed */
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
