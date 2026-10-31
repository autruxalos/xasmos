/******************************************************************************
 * XASMOS - loader backend contract (what an architecture must provide)
 * Path: source/kerncall/include/xload_backend.h
 *
 * load.c is portable and calls only these functions. Each architecture
 * implements them in arch/<name>/. The host test implements them with fakes.
 *
 * Contract rules every implementation must keep:
 *   - xlb_alloc returns zeroed frames. Recycled frames are never visible.
 *   - xlb_map is all-or-nothing: on failure, nothing is mapped.
 *   - xlb_release must not be called on a token that is still mapped; the
 *     loader always unmaps before it releases.
 ******************************************************************************/
#ifndef XASMOS_XLOAD_BACKEND_H
#define XASMOS_XLOAD_BACKEND_H

#include <stdint.h>
#include "xload.h"

#define XLB_PAGE      4096ULL
#define XLB_USER_TOP  0x0000800000000000ULL   /* end of user space; the top page is a guard */

#define XLB_PERM_R    1u
#define XLB_PERM_W    2u
#define XLB_PERM_X    4u

/* Pure check, must not allocate: may `owner` take `frames` frames and `tokens`
 * tokens right now? Returns 1 if yes, 0 if no. */
int   xlb_quota_ok(uint32_t owner, uint64_t frames, uint32_t tokens);

/* Allocates `frames` zeroed frames as one token for `owner`. Returns 0 and
 * sets *token, or XLOAD_E_NOMEM. */
int   xlb_alloc(uint32_t owner, uint64_t frames, uint32_t rights, uint64_t *token);

/* Kernel-writable pointer to the first byte of the token's frames. */
void *xlb_kernel_ptr(uint64_t token);

/* Frees a token that is not mapped, together with its frames. */
void  xlb_release(uint64_t token);

/* Maps the whole token at vaddr in `as` with XLB_PERM_* bits. All-or-nothing.
 * Returns 0 or XLOAD_E_MAP. */
int   xlb_map(XAspace *as, uint64_t vaddr, uint64_t token, uint32_t perms);

/* Removes the mapping created at vaddr. Returns 0, or -1 if none existed. */
int   xlb_unmap(XAspace *as, uint64_t vaddr);

#endif /* XASMOS_XLOAD_BACKEND_H */
