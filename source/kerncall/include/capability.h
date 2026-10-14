/******************************************************************************
 * XASMOS - Hardware Token (capability) definitions
 * Path: source|kerncall|include|capability.h
 *
 * xeret is a notary: physical resources are only usable through 64-bit tokens.
 * Apps never receive raw frame numbers; they receive opaque tokens.
 ******************************************************************************/
#ifndef XASMOS_CAPABILITY_H
#define XASMOS_CAPABILITY_H

#include "efi_types.h"

/* --- Resource kinds --- */
#define XTOK_KIND_NONE   0u
#define XTOK_KIND_RAM    1u   /* contiguous physical frames */
#define XTOK_KIND_MMIO   2u   /* reserved for device BARs later */
#define XTOK_KIND_IRQ    3u   /* reserved */

/* --- Rights bits (OR together) --- */
#define XTOK_RIGHT_READ    (1u << 0)
#define XTOK_RIGHT_WRITE   (1u << 1)
#define XTOK_RIGHT_EXECUTE (1u << 2)
#define XTOK_RIGHT_MAP     (1u << 3)  /* may install into page tables */
#define XTOK_RIGHT_DMA     (1u << 4)  /* may use as DMA buffer */
#define XTOK_RIGHT_RELEASE (1u << 5)  /* holder may return to notary */

#define XTOK_RIGHTS_RAM_DEFAULT \
    (XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_MAP | \
     XTOK_RIGHT_DMA  | XTOK_RIGHT_RELEASE)

/* Opaque 64-bit token presented to LibOS / apps.
 * Layout (private to xeret — do not parse in apps):
 *   bits  0..31  generation (anti-reuse)
 *   bits 32..63  slot index in the ledger table
 */
typedef UINT64 XToken;

#define XTOKEN_INVALID  0ULL

/* Owner IDs: 0 = kernel/notary, 1+ = LibOS instances */
#define XTOK_OWNER_KERNEL  0u
#define XTOK_OWNER_FIRST_APP 1u

/* Internal ledger entry (not visible to apps) */
typedef struct {
    UINT32  generation;
    UINT32  kind;
    UINT32  owner_id;
    UINT32  rights;
    UINT64  phys;        /* base physical address (frame-aligned for RAM) */
    UINT64  n_frames;    /* count of 4 KiB frames (RAM) */
    UINT8   live;        /* 1 = occupied slot */
    UINT8   _pad[7];
} XTokenEntry;

/* Capacity: fixed table, O(1) index. 4096 concurrent tokens. */
#define XTOK_TABLE_SLOTS  4096u

/* --- Notary API --- */

/* Initialise empty ledger. Call once after phys_init_from_handoff. */
void   xtok_init(void);

/* Allocate n 4 KiB frames and seal them into a token for owner_id.
 * Returns XTOKEN_INVALID on failure. */
XToken xtok_alloc_ram(UINT32 owner_id, UINT64 n_frames, UINT32 rights);

/* Allocate one 2 MiB-aligned huge region (512 frames) as a token. */
XToken xtok_alloc_ram_huge(UINT32 owner_id, UINT32 rights);

/* Validate token has at least the requested rights. Returns 1 if ok. */
int    xtok_validate(XToken tok, UINT32 rights_needed);

/* Peek physical base + frame count if token is live RAM with rights.
 * Returns 0 on success, -1 on failure. Does not consume the token. */
int    xtok_ram_info(XToken tok, UINT64 *out_phys, UINT64 *out_frames);

/* Release token: free underlying frames (if RAM) and invalidate slot.
 * Requires XTOK_RIGHT_RELEASE. Returns 0 on success. */
int    xtok_release(XToken tok);

/* Diagnostics */
UINT32 xtok_live_count(void);

#endif /* XASMOS_CAPABILITY_H */
