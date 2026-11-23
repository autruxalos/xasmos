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
#define XTOK_KIND_BLOCKS 4u   /* a range of blocks on a registered block device */
#define XTOK_KIND_ENDPOINT 5u /* receiver-owned transfer point (mem/endpoint.c) */
#define XTOK_KIND_BUDGET   6u   /* memory budget: n_frames is the balance (mem/budget.h) */
#define XTOK_KIND_MEM      7u   /* fixed frames (e.g. rd0): never returned to the allocator, never released */

#define XTOK_RIGHT_READ    (1u << 0)
#define XTOK_RIGHT_WRITE   (1u << 1)
#define XTOK_RIGHT_EXECUTE (1u << 2)
#define XTOK_RIGHT_MAP     (1u << 3)
#define XTOK_RIGHT_DMA     (1u << 4)
#define XTOK_RIGHT_RELEASE (1u << 5)
#define XTOK_RIGHT_GIVE    (1u << 6)   /* may transfer the token to another owner */
#define XTOK_RIGHT_RECV (1u << 7)   /* endpoint: accept offers */
#define XTOK_RIGHT_SEND (1u << 8)   /* endpoint: offer to it */

#define XTOK_RIGHTS_RAM_DEFAULT \
    (XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_MAP | \
     XTOK_RIGHT_DMA  | XTOK_RIGHT_RELEASE)

/* Block tokens use the same two rights as the device they name. */
#define XTOK_RIGHTS_BLOCKS_DEFAULT \
    (XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_RELEASE)

typedef UINT64 XToken;
#define XTOKEN_INVALID  0ULL

#define XTOK_OWNER_KERNEL  0u
#define XTOK_OWNER_FIRST_APP 1u

/* Device grants: which owner may request block tokens on which device, and
 * with which maximum rights. An empty table grants nothing. */

/* Field meaning depends on kind:
 *   RAM:    phys = physical address, n_frames = frame count
 *   BLOCKS: dev = BlkDev* of the device, start = first block,
 *           n_frames = block count. The ledger never reads phys as memory
 *           for a BLOCKS token. */
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
    void   *dev;         /* BLOCKS only: the block device, NULL otherwise */
    UINT64  start;       /* BLOCKS only: first block of the range */
    UINT32  io_count;    /* BLOCKS only: device operations in flight */
    UINT32  hold;        /* kernel-owned retention: blocks release, not mapping */
    UINT32  frozen;      /* offered for transfer: no use, release or map until accept/cancel */
    XToken  parent;      /* derivation parent, XTOKEN_INVALID for roots */
} XTokenEntry;

#define XTOK_TABLE_SLOTS  4096u

void   xtok_init(void);
XToken xtok_alloc_ram(UINT32 owner_id, UINT64 n_frames, UINT32 rights);
XToken xtok_alloc_ram_huge(UINT32 owner_id, UINT32 rights);
int    xtok_validate(XToken tok, UINT32 rights_needed);
int    xtok_ram_info(XToken tok, UINT64 *out_phys, UINT64 *out_frames);

/* Block tokens. xtok_alloc_blocks seals the range [start, start+n) of dev
 * with the given rights; it allocates nothing. xtok_blk_io checks the token,
 * the right for the operation, and that [lba, lba+count) lies inside the
 * range, then calls blk_read or blk_write. Returns BLK_OK or a BLK_E_* code,
 * or XTOK_BLK_EDENIED if the token is stale, not a block token, or lacks the
 * right. Nothing reaches the driver unless every check passes. */
#define XTOK_BLK_EDENIED (-10)
#define XTOK_BLK_READ    0u
#define XTOK_BLK_WRITE   1u
XToken xtok_alloc_blocks(UINT32 owner_id, void *dev, UINT64 start, UINT64 n,
                         UINT32 rights);
int    xtok_blk_io(XToken tok, UINT32 op, UINT64 lba, UINT32 count, void *buf);

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
#define XTOK_REL_ETOOBIG  (-4)   /* revocation subtree larger than XTOK_REVOKE_MAX */

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
 * caller_owner, and carry rights_needed. On success the mapping count rises
 * and the physical range is returned. Blocks tokens never map. */
#define XTOK_MAP_OK       0
#define XTOK_MAP_ESTALE  (-1)
#define XTOK_MAP_EKIND   (-2)
#define XTOK_MAP_EOWNER  (-3)
#define XTOK_MAP_ERIGHTS (-4)
#define XTOK_MAP_EBUSY   (-5)
int    xtok_map_acquire(XToken tok, UINT32 caller_owner, UINT32 rights_needed,
                        UINT64 *out_phys, UINT64 *out_frames);
void   xtok_map_release(XToken tok);
UINT32 xtok_map_count(XToken tok);

/* Ring pins: check a block token and a RAM token for one ring entry and pin
 * both, or refuse with -1. Unpin when the device has finished or refused. */
int  xtok_ring_pin(UINT32 owner, XToken blk, XToken ram, UINT32 op,
                   UINT64 lba, UINT32 count);
void xtok_ring_unpin(XToken blk, XToken ram);

/* Device and first absolute block of a live block token. */
int  xtok_blk_range(XToken tok, void **dev, UINT64 *start);

/* Device and first absolute block of a live block token. */
int  xtok_blk_range(XToken tok, void **dev, UINT64 *start);

/* Kernel retention of a token: blocks release, never mapping or I/O. */
int  xtok_hold(XToken tok);
void xtok_unhold(XToken tok);

/* Boot-time root capabilities: one per block device, one holder. The kernel
 * mints the root and hands tokens to its holder only; it holds no policy about
 * who the holder should be. */
void   xtok_roots_reset(void);
XToken xtok_root_mint(void *dev, UINT32 holder);
UINT32 xtok_root_holder(void *dev);
XToken xtok_root_claim(void *dev, UINT32 owner);

/* Attenuation: a child block token covers a subset of its parent, never more,
 * with rights the parent holds, and names no device of its own. */
XToken xtok_derive_blocks(UINT32 owner, XToken parent, UINT64 first, UINT64 count,
                          UINT32 rights);

/* Transfer of titularity. The giver must hold GIVE and the token must be free of
 * pins, mappings and ring hold. The destination must be a live owner. Rights and
 * range are unchanged. Returns 0, or -1. */
int  xtok_give(UINT32 giver, XToken tok, UINT32 dest);

/* Endpoint layer primitives (mem/endpoint.c). The ledger only stores and
 * freezes; policy about who may offer or accept is in endpoint.c. */
XToken xtok_alloc_endpoint(UINT32 owner, UINT64 value, UINT32 rights);
int    xtok_ep_value(XToken tok, UINT32 owner, UINT32 right, UINT64 *value);
int    xtok_offer_begin(UINT32 giver, XToken tok);
int    xtok_thaw_to(XToken tok, UINT32 owner);
int    xtok_thaw(XToken tok);

/* Read-only inspection, for the exokernel invariant checks (tests/arch/amd64).
 * Returns the number of live slots visited. Never changes state. */
typedef struct {
    XToken  tok;
    UINT32  kind;
    UINT32  owner;
    UINT32  rights;
    UINT64  start;
    UINT64  count;       /* n_frames for RAM, block count for BLOCKS */
    UINT32  frozen;
    UINT32  hold;
    UINT32  io_count;
    UINT16  map_count;
    XToken  parent;      /* derivation parent, XTOKEN_INVALID if none */
} XTokenView;

UINT32 xtok_inspect_all(XTokenView *out, UINT32 max);

/* Revocation: retires tok and every descendant reached by parent links, as one
 * step. Refused, with nothing changed, if any member is busy. */
int xtok_revoke(XToken tok, UINT32 owner);

/* Memory-backed fixed frames (rd0). Wraps frames that already exist; the
 * ledger never returns them to the physical allocator. */
int    xtok_mem_wrap(UINT32 owner, UINT64 phys, UINT64 n_frames, XToken *out);
UINT64 xtok_mem_free_count(void);

/* Fixed memory from a list of frames (volume of the boot). Frames need not be
 * contiguous. xtok_frame_at walks them; map_acquire gives no contiguous range. */
int    xtok_mem_wrap_list(UINT32 owner, const UINT64 *frames, UINT64 n, XToken *out);
int    xtok_frame_at(XToken tok, UINT64 i, UINT64 *phys);

#endif /* XASMOS_CAPABILITY_H */
