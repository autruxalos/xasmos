/******************************************************************************
 * XASMOS - Hardware Token (capability) definitions
 * Path: source|kerncall|include|capability.h
 ******************************************************************************/
#ifndef XASMOS_CAPABILITY_H
#define XASMOS_CAPABILITY_H

#include "efi_types.h"

#define XTOK_KIND_NONE   0u
#define XTOK_KIND_RAM    1u
#define XTOK_KIND_MMIO   2u
#define XTOK_KIND_IRQ    3u

#define XTOK_RIGHT_READ    (1u << 0)
#define XTOK_RIGHT_WRITE   (1u << 1)
#define XTOK_RIGHT_EXECUTE (1u << 2)
#define XTOK_RIGHT_MAP     (1u << 3)
#define XTOK_RIGHT_DMA     (1u << 4)
#define XTOK_RIGHT_RELEASE (1u << 5)

#define XTOK_RIGHTS_RAM_DEFAULT \
    (XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_MAP | \
     XTOK_RIGHT_DMA  | XTOK_RIGHT_RELEASE)

typedef UINT64 XToken;
#define XTOKEN_INVALID  0ULL

#define XTOK_OWNER_KERNEL  0u
#define XTOK_OWNER_FIRST_APP 1u

typedef struct {
    UINT32  generation;
    UINT32  kind;
    UINT32  owner_id;
    UINT32  rights;
    UINT64  phys;
    UINT64  n_frames;
    UINT8   live;
    UINT8   _pad0;
    UINT16  map_count;   /* live address-space mappings of this token */
    UINT32  _pad1;
} XTokenEntry;

#define XTOK_TABLE_SLOTS  4096u

void   xtok_init(void);
XToken xtok_alloc_ram(UINT32 owner_id, UINT64 n_frames, UINT32 rights);
XToken xtok_alloc_ram_huge(UINT32 owner_id, UINT32 rights);
int    xtok_validate(XToken tok, UINT32 rights_needed);
int    xtok_ram_info(XToken tok, UINT64 *out_phys, UINT64 *out_frames);
/* Fails with -1 if the token is stale or lacks RELEASE, and with -2 if it is
 * still mapped somewhere: freeing a mapped frame would leave a live mapping
 * onto a recycled frame (use-after-free across owners). Revoke first. */
int    xtok_release(XToken tok);
UINT32 xtok_live_count(void);

/* ---- Per-owner operations (syscall layer, process teardown) --------------
 * Release results (negative = failure). A token that belongs to someone else
 * reports ESTALE, indistinguishable from one that does not exist. */
#define XTOK_REL_ESTALE   (-1)
#define XTOK_REL_EBUSY    (-2)   /* still mapped */
#define XTOK_REL_ERIGHTS  (-3)   /* owner's token, but without RELEASE */

/* Holder-initiated free: requires ownership AND the RELEASE right. */
int    xtok_release_owned(XToken tok, UINT32 owner);

/* Kernel-initiated free: ignores rights and owner (abort protocol), but still
 * refuses a token that is mapped -- revoke its mappings first. */
int    xtok_reclaim(XToken tok);

/* Fills out[] with up to max live tokens of owner; returns how many. */
UINT32 xtok_owner_tokens(UINT32 owner, XToken *out, UINT32 max);

/* Frames and tokens an owner holds right now: the single source of truth
 * for quotas (nothing to keep in sync, nothing to forget to decrement). */
void   xtok_owner_usage(UINT32 owner, UINT64 *frames, UINT32 *tokens);

/* ---- Map gate (used by xas_map in paging.c) -------------------------------
 * One atomic check-and-pin: the token must be live, RAM, owned by
 * caller_owner, and carry every bit of rights_needed. On success the mapping
 * count is incremented under the ledger lock, so the token cannot be released
 * between the check and the page-table write. Pair with xtok_map_release(). */
#define XTOK_MAP_OK       0
#define XTOK_MAP_ESTALE  (-1)   /* invalid, stale or released token          */
#define XTOK_MAP_EOWNER  (-2)   /* token belongs to another owner            */
#define XTOK_MAP_ERIGHTS (-3)   /* missing MAP or a requested R/W/X right    */
#define XTOK_MAP_EKIND   (-4)   /* not a RAM token                           */
#define XTOK_MAP_EBUSY   (-5)   /* map_count saturated                       */
int    xtok_map_acquire(XToken tok, UINT32 caller_owner, UINT32 rights_needed,
                        UINT64 *out_phys, UINT64 *out_frames);
void   xtok_map_release(XToken tok);
UINT32 xtok_map_count(XToken tok);   /* 0xFFFFFFFF if the token is stale */

#endif
