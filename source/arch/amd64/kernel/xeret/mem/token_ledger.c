/******************************************************************************
 * XASMOS - Hardware Token ledger (xeret notary)
 * Path: source|arch|amd64|kernel|xeret|mem|token_ledger.c
 *
 * O(1) seal / validate / release over a fixed table.
 * Slot index in high 32 bits of the token; generation in low 32 bits
 * prevents use-after-free of recycled slots.
 *
 * Concurrency: one coarse-grained spinlock guards the whole table. Simplest
 * correct option for SMP -- no per-core sharding, no topology discovery.
 * Portable to any x86-64 CPU with the `lock` prefix (486 onward), Core 2
 * Quad included; nothing here is Zen5-specific.
 ******************************************************************************/
#include "capability.h"
#include "kmemory.h"
#include "spinlock.h"

static XTokenEntry g_table[XTOK_TABLE_SLOTS];
static UINT32      g_live;
static UINT32      g_next_hint; /* round-robin scan start for free slots */
static XSpinlock   g_lock = XSPINLOCK_INIT;

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

/* Called once at boot, before APs start and before xspin_lock is needed. */
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
    g_lock      = XSPINLOCK_INIT;
}

/* Caller already holds g_lock. Never calls into physical.c -- the two
 * structures' locks must not nest, in either direction, anywhere in this
 * file. Returns XTOKEN_INVALID if the table is full; the caller (which
 * already knows phys/n_frames) is responsible for freeing them, outside
 * this lock, in that case. */
static XToken seal_ram_locked(UINT32 owner_id, UINT64 phys, UINT64 n_frames, UINT32 rights) {
    UINT32 slot;
    XTokenEntry *e;

    if (phys == 0 || n_frames == 0) {
        return XTOKEN_INVALID;
    }
    if (claim_slot(&slot) != 0) {
        return XTOKEN_INVALID; /* table full -- caller frees phys/n_frames */
    }

    e = &g_table[slot];
    e->kind     = XTOK_KIND_RAM;
    e->owner_id = owner_id;
    e->rights   = rights;   /* 0 is a valid, deliberately-empty-rights token */
    e->phys     = phys;
    e->n_frames = n_frames;
    e->live     = 1;
    /* generation already set; bumped only on release */
    g_live++;

    return pack_token(slot, e->generation);
}

XToken xtok_alloc_ram(UINT32 owner_id, UINT64 n_frames, UINT32 rights) {
    UINT64 phys;
    XToken tok;

    if (n_frames == 0) {
        return XTOKEN_INVALID;
    }
    phys = phys_alloc_frames(n_frames);   /* locks internally, released before we lock here */
    if (phys == 0) {
        return XTOKEN_INVALID;
    }

    xspin_lock(&g_lock);
    tok = seal_ram_locked(owner_id, phys, n_frames, rights);
    xspin_unlock(&g_lock);

    if (tok == XTOKEN_INVALID) {
        phys_free_frames(phys, n_frames);   /* table was full: hand frames back */
    }
    return tok;
}

XToken xtok_alloc_ram_huge(UINT32 owner_id, UINT32 rights) {
    UINT64 phys;
    XToken tok;

    phys = phys_alloc_huge();
    if (phys == 0) {
        return XTOKEN_INVALID;
    }

    xspin_lock(&g_lock);
    tok = seal_ram_locked(owner_id, phys, XERET_FRAMES_PER_HUGE, rights);
    xspin_unlock(&g_lock);

    if (tok == XTOKEN_INVALID) {
        phys_free_frames(phys, XERET_FRAMES_PER_HUGE);
    }
    return tok;
}

int xtok_validate(XToken tok, UINT32 rights_needed) {
    XTokenEntry *e;
    int ok;

    xspin_lock(&g_lock);
    e = lookup_live(tok);
    ok = e && (e->rights & rights_needed) == rights_needed;
    xspin_unlock(&g_lock);
    return ok;
}

int xtok_ram_info(XToken tok, UINT64 *out_phys, UINT64 *out_frames) {
    XTokenEntry *e;
    int rc;

    xspin_lock(&g_lock);
    e = lookup_live(tok);
    if (!e || e->kind != XTOK_KIND_RAM) {
        rc = -1;
    } else {
        if (out_phys)   *out_phys   = e->phys;
        if (out_frames) *out_frames = e->n_frames;
        rc = 0;
    }
    xspin_unlock(&g_lock);
    return rc;
}

int xtok_release(XToken tok) {
    XTokenEntry *e;
    UINT64 phys = 0, n = 0;
    int rc;

    xspin_lock(&g_lock);
    e = lookup_live(tok);
    if (!e || (e->rights & XTOK_RIGHT_RELEASE) == 0) {
        rc = -1;
    } else {
        phys = e->phys;
        n    = e->n_frames;
        int was_ram = (e->kind == XTOK_KIND_RAM && phys != 0 && n != 0);

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
        rc = was_ram ? 1 : 0;   /* 1 signals "free phys/n after unlocking" */
    }
    xspin_unlock(&g_lock);

    /* phys_free_frames() takes physical.c's own lock -- must not be called
     * while holding g_lock, or a symmetric call elsewhere could deadlock
     * the two structures against each other. */
    if (rc == 1) {
        phys_free_frames(phys, n);
    }
    return (rc < 0) ? -1 : 0;
}

UINT32 xtok_live_count(void) {
    UINT32 v;
    xspin_lock(&g_lock);
    v = g_live;
    xspin_unlock(&g_lock);
    return v;
}
