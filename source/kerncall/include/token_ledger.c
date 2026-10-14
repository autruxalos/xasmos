/******************************************************************************
 * XASMOS - Hardware Token ledger (xeret notary)
 * Path: source|arch|amd64|kernel|xeret|mem|token_ledger.c
 *
 * O(1) seal / validate / release over a fixed table.
 * Slot index in high 32 bits of the token; generation in low 32 bits
 * prevents use-after-free of recycled slots.
 ******************************************************************************/
#include "../../../../kernel/include/capability.h"
#include "../../../../kernel/include/kmemory.h"

static XTokenEntry g_table[XTOK_TABLE_SLOTS];
static UINT32      g_live;
static UINT32      g_next_hint; /* round-robin scan start for free slots */

static XToken pack_token(UINT32 slot, UINT32 gen) {
    return ((UINT64)slot << 32) | (UINT64)gen;
}

static void unpack_token(XToken tok, UINT32 *slot, UINT32 *gen) {
    *slot = (UINT32)(tok >> 32);
    *gen  = (UINT32)(tok & 0xFFFFFFFFu);
}

static XTokenEntry *lookup_live(XToken tok) {
    UINT32 slot, gen;

    if (tok == XTOKEN_INVALID) {
        return 0;
    }
    unpack_token(tok, &slot, &gen);
    if (slot >= XTOK_TABLE_SLOTS) {
        return 0;
    }
    if (!g_table[slot].live) {
        return 0;
    }
    if (g_table[slot].generation != gen) {
        return 0; /* recycled slot — stale token */
    }
    return &g_table[slot];
}

static int claim_slot(UINT32 *out_slot) {
    UINT32 i, idx;

    for (i = 0; i < XTOK_TABLE_SLOTS; i++) {
        idx = (g_next_hint + i) % XTOK_TABLE_SLOTS;
        if (!g_table[idx].live) {
            *out_slot = idx;
            g_next_hint = (idx + 1) % XTOK_TABLE_SLOTS;
            return 0;
        }
    }
    return -1; /* table full */
}

void xtok_init(void) {
    UINT32 i;
    for (i = 0; i < XTOK_TABLE_SLOTS; i++) {
        g_table[i].live       = 0;
        g_table[i].generation = 1; /* 0 reserved so pack never yields all-zero by accident */
        g_table[i].kind       = XTOK_KIND_NONE;
        g_table[i].owner_id   = 0;
        g_table[i].rights     = 0;
        g_table[i].phys       = 0;
        g_table[i].n_frames   = 0;
    }
    g_live      = 0;
    g_next_hint = 0;
}

static XToken seal_ram(UINT32 owner_id, UINT64 phys, UINT64 n_frames, UINT32 rights) {
    UINT32 slot;
    XTokenEntry *e;

    if (phys == 0 || n_frames == 0) {
        return XTOKEN_INVALID;
    }
    if (claim_slot(&slot) != 0) {
        /* No free token slot — return frames to bitmap */
        phys_free_frames(phys, n_frames);
        return XTOKEN_INVALID;
    }

    e = &g_table[slot];
    e->kind     = XTOK_KIND_RAM;
    e->owner_id = owner_id;
    e->rights   = rights ? rights : XTOK_RIGHTS_RAM_DEFAULT;
    e->phys     = phys;
    e->n_frames = n_frames;
    e->live     = 1;
    /* generation already set; bumped only on release */
    g_live++;

    return pack_token(slot, e->generation);
}

XToken xtok_alloc_ram(UINT32 owner_id, UINT64 n_frames, UINT32 rights) {
    UINT64 phys;

    if (n_frames == 0) {
        return XTOKEN_INVALID;
    }
    phys = phys_alloc_frames(n_frames);
    if (phys == 0) {
        return XTOKEN_INVALID;
    }
    return seal_ram(owner_id, phys, n_frames, rights);
}

XToken xtok_alloc_ram_huge(UINT32 owner_id, UINT32 rights) {
    UINT64 phys;

    phys = phys_alloc_huge();
    if (phys == 0) {
        return XTOKEN_INVALID;
    }
    return seal_ram(owner_id, phys, XERET_FRAMES_PER_HUGE, rights);
}

int xtok_validate(XToken tok, UINT32 rights_needed) {
    XTokenEntry *e = lookup_live(tok);
    if (!e) {
        return 0;
    }
    if ((e->rights & rights_needed) != rights_needed) {
        return 0;
    }
    return 1;
}

int xtok_ram_info(XToken tok, UINT64 *out_phys, UINT64 *out_frames) {
    XTokenEntry *e = lookup_live(tok);
    if (!e || e->kind != XTOK_KIND_RAM) {
        return -1;
    }
    if (out_phys) {
        *out_phys = e->phys;
    }
    if (out_frames) {
        *out_frames = e->n_frames;
    }
    return 0;
}

int xtok_release(XToken tok) {
    XTokenEntry *e = lookup_live(tok);
    UINT64 phys, n;

    if (!e) {
        return -1;
    }
    if ((e->rights & XTOK_RIGHT_RELEASE) == 0) {
        return -1;
    }

    phys = e->phys;
    n    = e->n_frames;

    if (e->kind == XTOK_KIND_RAM && phys != 0 && n != 0) {
        phys_free_frames(phys, n);
    }

    e->live     = 0;
    e->kind     = XTOK_KIND_NONE;
    e->owner_id = 0;
    e->rights   = 0;
    e->phys     = 0;
    e->n_frames = 0;
    /* Bump generation so old tokens pointing at this slot fail lookup */
    e->generation++;
    if (e->generation == 0) {
        e->generation = 1;
    }

    if (g_live > 0) {
        g_live--;
    }
    return 0;
}

UINT32 xtok_live_count(void) {
    return g_live;
}
