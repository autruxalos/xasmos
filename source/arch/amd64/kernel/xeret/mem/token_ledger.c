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
#include "owner.h"
#include "blk.h"
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
        g_table[i].map_count  = 0;
        g_table[i].parent = XTOKEN_INVALID;
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

/* Recycled frames still hold their previous owner's bytes. An exokernel must
 * never hand one owner another's data, so every frame is cleared BEFORE it is
 * sealed into a token. volatile keeps the optimizer from turning the loop
 * into a memset call (there is no libc to provide one). */
static void zero_frames(UINT64 phys, UINT64 n_frames) {
    volatile UINT64 *p = (volatile UINT64 *)(UINTN)phys;
    UINT64 words = n_frames * (XERET_FRAME_SIZE / sizeof(UINT64));
    for (UINT64 i = 0; i < words; i++) p[i] = 0;
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
    zero_frames(phys, n_frames);

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
    zero_frames(phys, XERET_FRAMES_PER_HUGE);

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

#define REL_CHECK_RIGHT  1u    /* holder-initiated: needs XTOK_RIGHT_RELEASE */
#define REL_CHECK_OWNER  2u    /* the caller must be the token's owner      */

/* Budget helpers. Caller holds g_lock. */
static int has_live_children_locked(XToken tok) {
    UINT32 slot;
    for (slot = 0; slot < XTOK_TABLE_SLOTS; slot++)
        if (g_table[slot].live && g_table[slot].parent == tok) return 1;
    return 0;
}

/* Returns the balance to the budget that paid for e (its parent), if it is live. */
static void refund_locked(const XTokenEntry *e) {
    XTokenEntry *p;
    if (e->parent == XTOKEN_INVALID) return;
    p = lookup_live(e->parent);
    if (p && p->kind == XTOK_KIND_BUDGET) p->n_frames += e->n_frames;
}

/* The one place a token dies. Caller holds g_lock. Returns 1 and the frames to
 * free if the entry held RAM; the caller frees them after unlocking. */
static int retire_locked(XTokenEntry *e, UINT64 *phys, UINT64 *n) {
    int freed = 0;
    if (e->kind == XTOK_KIND_RAM || e->kind == XTOK_KIND_BUDGET) refund_locked(e);
    if (e->kind == XTOK_KIND_RAM && e->phys != 0 && e->n_frames != 0) {
        *phys = e->phys;
        *n    = e->n_frames;
        freed = 1;
    }
    e->live      = 0;
    e->kind      = XTOK_KIND_NONE;
    e->owner_id  = 0;
    e->rights    = 0;
    e->phys      = 0;
    e->n_frames  = 0;
    e->map_count = 0;
    e->dev       = 0;
    e->start     = 0;
    e->io_count  = 0;
    e->hold      = 0;
    e->frozen    = 0;
    e->parent    = XTOKEN_INVALID;
    e->generation++;
    if (e->generation == 0) {
        e->generation = 1;
    }
    if (g_live > 0) {
        g_live--;
    }
    return freed;
}

/* A foreign token reports ESTALE, exactly like a nonexistent one. */
static int release_impl(XToken tok, UINT32 flags, UINT32 owner) {
    XTokenEntry *e;
    UINT64 phys = 0, n = 0;
    int rc = 0, freed = 0;

    xspin_lock(&g_lock);
    e = lookup_live(tok);
    if (!e) {
        rc = XTOK_REL_ESTALE;
    } else if ((flags & REL_CHECK_OWNER) && e->owner_id != owner) {
        rc = XTOK_REL_ESTALE;
    } else if ((flags & REL_CHECK_RIGHT) && (e->rights & XTOK_RIGHT_RELEASE) == 0) {
        rc = XTOK_REL_ERIGHTS;
    } else if (e->map_count != 0 || e->io_count != 0 || e->hold != 0 || e->frozen != 0) {
        rc = XTOK_REL_EBUSY;
    } else if (e->kind == XTOK_KIND_BUDGET && has_live_children_locked(tok)) {
        rc = XTOK_REL_EBUSY;   /* children still hold its balance */
    } else {
        freed = retire_locked(e, &phys, &n);
    }
    xspin_unlock(&g_lock);

    /* phys_free_frames takes physical.c's lock: never while holding g_lock. */
    if (freed) {
        phys_free_frames(phys, n);
    }
    return rc;
}

#define XTOK_REVOKE_MAX 32u

/* Revocation: the subtree of tok is retired as one step. Membership follows
 * parent links, so it reaches delegated descendants whatever their rights.
 * Refused, with nothing changed, if any member is mapped, in I/O, retained or
 * frozen. The root holder needs RELEASE; children do not. */
int xtok_revoke(XToken tok, UINT32 owner) {
    XToken mem[XTOK_REVOKE_MAX];
    UINT64 fp[XTOK_REVOKE_MAX], fn[XTOK_REVOKE_MAX];
    UINT32 nm = 0, nf = 0, slot, gen, i, j;
    XTokenEntry *root, *e;
    int rc = 0, grew;

    xspin_lock(&g_lock);
    root = lookup_live(tok);
    if (!root || root->owner_id != owner) { rc = XTOK_REL_ESTALE; goto out; }
    if ((root->rights & XTOK_RIGHT_RELEASE) == 0) { rc = XTOK_REL_ERIGHTS; goto out; }

    mem[nm++] = tok;
    do {
        grew = 0;
        for (slot = 0; slot < XTOK_TABLE_SLOTS; slot++) {
            XToken t;
            e = &g_table[slot];
            if (!e->live || e->parent == XTOKEN_INVALID) continue;
            for (i = 0; i < nm; i++) if (mem[i] == e->parent) break;
            if (i == nm) continue;                  /* parent is not in the subtree */
            t = pack_token(slot, e->generation);
            for (j = 0; j < nm; j++) if (mem[j] == t) break;
            if (j < nm) continue;                   /* already a member */
            if (nm >= XTOK_REVOKE_MAX) { rc = XTOK_REL_ETOOBIG; goto out; }
            mem[nm++] = t;
            grew = 1;
        }
    } while (grew);

    for (i = 0; i < nm; i++) {
        unpack_token(mem[i], &slot, &gen);
        e = &g_table[slot];
        if (e->map_count != 0 || e->io_count != 0 || e->hold != 0 || e->frozen != 0) {
            rc = XTOK_REL_EBUSY;
            goto out;
        }
    }

    for (i = 0; i < nm; i++) {
        UINT64 p = 0, n = 0;
        unpack_token(mem[i], &slot, &gen);
        if (retire_locked(&g_table[slot], &p, &n)) {
            fp[nf] = p;
            fn[nf] = n;
            nf++;
        }
    }

out:
    xspin_unlock(&g_lock);
    for (i = 0; i < nf; i++) {
        phys_free_frames(fp[i], fn[i]);
    }
    return rc;
}

/* Kernel-side release by anyone holding the right (the original behaviour:
 * stale or right-less -> -1, still mapped -> -2). */
int xtok_release(XToken tok) {
    int r = release_impl(tok, REL_CHECK_RIGHT, 0);
    return (r == XTOK_REL_ERIGHTS) ? -1 : r;
}

int xtok_release_owned(XToken tok, UINT32 owner) {
    return release_impl(tok, REL_CHECK_RIGHT | REL_CHECK_OWNER, owner);
}

int xtok_reclaim(XToken tok) {
    return release_impl(tok, 0, 0);
}

UINT32 xtok_owner_tokens(UINT32 owner, XToken *out, UINT32 max) {
    UINT32 n = 0;
    xspin_lock(&g_lock);
    for (UINT32 slot = 0; slot < XTOK_TABLE_SLOTS && n < max; slot++) {
        if (g_table[slot].live && g_table[slot].owner_id == owner) {
            out[n++] = pack_token(slot, g_table[slot].generation);
        }
    }
    xspin_unlock(&g_lock);
    return n;
}

void xtok_owner_usage(UINT32 owner, UINT64 *frames, UINT32 *tokens) {
    UINT64 f = 0;
    UINT32 c = 0;
    xspin_lock(&g_lock);
    for (UINT32 slot = 0; slot < XTOK_TABLE_SLOTS; slot++) {
        if (g_table[slot].live && g_table[slot].owner_id == owner) {
            /* Memory quota: only RAM tokens hold frames. A block token names
             * blocks of a device; it counts as one token and no frames. */
            if (g_table[slot].kind == XTOK_KIND_RAM) f += g_table[slot].n_frames;
            c++;
        }
    }
    xspin_unlock(&g_lock);
    if (frames) *frames = f;
    if (tokens) *tokens = c;
}

UINT32 xtok_live_count(void) {
    UINT32 v;
    xspin_lock(&g_lock);
    v = g_live;
    xspin_unlock(&g_lock);
    return v;
}

int xtok_map_acquire(XToken tok, UINT32 caller_owner, UINT32 rights_needed,
                     UINT64 *out_phys, UINT64 *out_frames) {
    XTokenEntry *e;
    int rc;

    xspin_lock(&g_lock);
    e = lookup_live(tok);
    if (!e)                                          rc = XTOK_MAP_ESTALE;
    else if (e->kind != XTOK_KIND_RAM)               rc = XTOK_MAP_EKIND;
    else if (e->owner_id != caller_owner)            rc = XTOK_MAP_EOWNER;
    else if ((e->rights & rights_needed) != rights_needed) rc = XTOK_MAP_ERIGHTS;
    else if (e->io_count != 0)                       rc = XTOK_MAP_EBUSY;
    else if (e->frozen != 0)                         rc = XTOK_MAP_EBUSY;
    else if (e->map_count == 0xFFFFu)                rc = XTOK_MAP_EBUSY;
    else {
        e->map_count++;
        if (out_phys)   *out_phys   = e->phys;
        if (out_frames) *out_frames = e->n_frames;
        rc = XTOK_MAP_OK;
    }
    xspin_unlock(&g_lock);
    return rc;
}

void xtok_map_release(XToken tok) {
    XTokenEntry *e;
    xspin_lock(&g_lock);
    e = lookup_live(tok);
    if (e && e->map_count > 0) {
        e->map_count--;
    }
    xspin_unlock(&g_lock);
}

UINT32 xtok_map_count(XToken tok) {
    XTokenEntry *e;
    UINT32 v;
    xspin_lock(&g_lock);
    e = lookup_live(tok);
    v = e ? e->map_count : 0xFFFFFFFFu;
    xspin_unlock(&g_lock);
    return v;
}

/* ---- Block tokens ----------------------------------------------------------
 * A BLOCKS token seals a range of one block device. It allocates nothing:
 * the range already exists on the device. The ledger checks the token, the
 * right and the range under its lock; the device I/O runs after the lock is
 * released, pinned by io_count so the token cannot be freed underneath it. */

XToken xtok_alloc_blocks(UINT32 owner_id, void *dev, UINT64 start, UINT64 n,
                         UINT32 rights) {
    UINT32 slot;
    XTokenEntry *e;
    XToken tok;

    if (dev == 0 || n == 0) {
        return XTOKEN_INVALID;
    }

    xspin_lock(&g_lock);
    if (claim_slot(&slot) != 0) {
        xspin_unlock(&g_lock);
        return XTOKEN_INVALID;
    }
    e = &g_table[slot];
    e->kind     = XTOK_KIND_BLOCKS;
    e->owner_id = owner_id;
    e->rights   = rights;
    e->phys     = 0;
    e->n_frames = n;
    e->dev      = dev;
    e->start    = start;
    e->io_count = 0;
    e->hold     = 0;
    e->frozen = 0;
    e->live     = 1;
    g_live++;
    tok = pack_token(slot, e->generation);
    xspin_unlock(&g_lock);
    return tok;
}

/* Range check without overflow: lba must be inside the range, and count must
 * fit in what remains after lba. */
static int blk_range_ok(const XTokenEntry *e, UINT64 lba, UINT32 count) {
    UINT64 off;
    if (lba < e->start) {
        return 0;
    }
    off = lba - e->start;
    if (off > e->n_frames) {
        return 0;
    }
    return (UINT64)count <= e->n_frames - off;
}

int xtok_blk_io(XToken tok, UINT32 op, UINT64 lba, UINT32 count, void *buf) {
    XTokenEntry *e;
    UINT32 need;
    void *dev;
    int rc;

    if (buf == 0 || count == 0) {
        return BLK_E_ARG;
    }
    need = (op == XTOK_BLK_WRITE) ? XTOK_RIGHT_WRITE : XTOK_RIGHT_READ;

    /* Validate and pin under the lock. Nothing below touches the device
     * until io_count is raised, and the token cannot die while it is. */
    xspin_lock(&g_lock);
    e = lookup_live(tok);
    if (!e || e->kind != XTOK_KIND_BLOCKS ||
        (e->rights & need) != need || !blk_range_ok(e, lba, count)) {
        xspin_unlock(&g_lock);
        return XTOK_BLK_EDENIED;
    }
    dev = e->dev;
    e->io_count++;
    xspin_unlock(&g_lock);

    /* The device call runs without the ledger lock. */
    if (op == XTOK_BLK_WRITE) {
        rc = blk_write((BlkDev *)dev, lba, count, buf);
    } else {
        rc = blk_read((BlkDev *)dev, lba, count, buf);
    }

    xspin_lock(&g_lock);
    e = lookup_live(tok);   /* io_count pinned it, so the slot is still ours */
    if (e && e->io_count > 0) {
        e->io_count--;
    }
    xspin_unlock(&g_lock);
    return rc;
}

/* ---- Device grants ---------------------------------------------------------
 * The active table decides which owner may request block tokens on which
 * device. At boot it holds only the built-in minimal policy: rd0 for the
 * first application (read and write), nothing for nd0. Nothing here reads the
 * disk, so an unreadable or hostile volume cannot widen what is granted. */

static XTokenGrant g_grants[XTOK_GRANT_MAX];
static UINT32      g_grant_count;

static void grant_put(UINT32 idx, const char *dev, UINT32 owner, UINT32 rights) {
    UINT32 k = 0;
    while (dev[k] != '\0' && k < sizeof g_grants[idx].dev - 1) {
        g_grants[idx].dev[k] = dev[k];
        k++;
    }
    g_grants[idx].dev[k] = '\0';
    g_grants[idx].owner_id = owner;
    g_grants[idx].rights   = rights;
    g_grants[idx].first    = 0;
    g_grants[idx].count    = 0;
}

void xtok_grant_init(void) {
    g_grant_count = 0;
    grant_put(g_grant_count++, "rd0", XTOK_OWNER_FIRST_APP,
              XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_RELEASE);
    /* Test range on the system disk: data blocks 4096..4103, read and release.
     * Set by the boot, never by a program. */
    xtok_grant_put_range("nd0", XTOK_OWNER_FIRST_APP,
                         XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE, 4096, 8);
}

static int name_eq(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

/* One grant against one request. A whole-device request (count 0) needs a
 * whole-device grant; a range request must fall inside the granted range. The
 * arithmetic never adds past 64 bits: count is compared first, then the offset. */
static int grant_match(const XTokenGrant *g, const char *dev, UINT32 owner,
                       UINT32 rights, UINT64 lba, UINT64 count) {
    if (g->owner_id != owner || !name_eq(g->dev, dev)) return 0;
    if ((g->rights & rights) != rights) return 0;
    if (count == 0) return g->count == 0;
    if (g->count == 0) return 1;
    if (count > g->count) return 0;
    if (lba < g->first) return 0;
    return (lba - g->first) <= (g->count - count);
}

void xtok_grants_reset(void) {
    g_grant_count = 0;
}

int xtok_grant_put_full(const char *dev, UINT32 owner, UINT32 rights) {
    if (g_grant_count >= XTOK_GRANT_MAX) return -1;
    grant_put(g_grant_count++, dev, owner, rights);
    return 0;
}

int xtok_grant_put_range(const char *dev, UINT32 owner, UINT32 rights,
                         UINT64 first, UINT64 count) {
    if (count == 0 || g_grant_count >= XTOK_GRANT_MAX) return -1;
    grant_put(g_grant_count, dev, owner, rights);
    g_grants[g_grant_count].first = first;
    g_grants[g_grant_count].count = count;
    g_grant_count++;
    return 0;
}

int xtok_grant_check_range(const char *dev, UINT32 owner, UINT32 rights,
                           UINT64 lba, UINT64 count) {
    UINT32 i;
    for (i = 0; i < g_grant_count; i++)
        if (grant_match(&g_grants[i], dev, owner, rights, lba, count)) return 1;
    return 0;
}

/* Whole-device check, kept for callers that do not name a range. */
int xtok_grant_check(const char *dev, UINT32 owner, UINT32 rights) {
    return xtok_grant_check_range(dev, owner, rights, 0, 0);
}

/* ---- Ring pins -------------------------------------------------------------
 * A ring entry touches two tokens: a block range, which the device reads or
 * writes, and a RAM range, which is the DMA buffer. Both are checked and pinned
 * in one locked step, so neither can be released or mapped between the check
 * and the device I/O. Pins are counted in io_count, which release already
 * refuses to drop while non-zero. Any refusal is -1: the caller reports one
 * code, so a program cannot tell which token failed. */

/* Ring entries name blocks relative to their token: lba 0 is the token's first
 * block. The absolute check (blk_range_ok) is for kernel paths that use device
 * coordinates; a ring entry must never be compared against e->start. */
static int blk_rel_ok(const XTokenEntry *e, UINT64 lba, UINT32 count) {
    if (count == 0) return 0;
    if (lba > e->n_frames) return 0;
    if (count > e->n_frames - lba) return 0;
    return 1;
}

int xtok_ring_pin(UINT32 owner, XToken blk, XToken ram, UINT32 op,
                  UINT64 lba, UINT32 count) {
    XTokenEntry *eb, *er;
    UINT32 blk_right, ram_right;
    UINT64 need;
    int rc = -1;

    if (count == 0) return -1;
    /* The device writes RAM on a read and reads RAM on a write. */
    blk_right = (op == XTOK_BLK_WRITE) ? XTOK_RIGHT_WRITE : XTOK_RIGHT_READ;
    ram_right = (op == XTOK_BLK_WRITE) ? XTOK_RIGHT_READ  : XTOK_RIGHT_WRITE;

    xspin_lock(&g_lock);
    eb = lookup_live(blk);
    er = lookup_live(ram);
    if (eb && er &&
        eb->kind == XTOK_KIND_BLOCKS && eb->owner_id == owner && eb->frozen == 0 &&
        (eb->rights & blk_right) == blk_right &&
        blk_rel_ok(eb, lba, count) &&
        eb->io_count != 0xFFFFFFFFu &&
        er->kind == XTOK_KIND_RAM && er->owner_id == owner &&
        (er->rights & ram_right) == ram_right &&
        er->map_count == 0 && er->frozen == 0 && er->phys != 0 &&
        er->io_count != 0xFFFFFFFFu) {
        /* The transfer must fit in the frames the program gave */
        need = (UINT64)count * blk_sector_size((const BlkDev *)eb->dev);
        if (need <= er->n_frames * XERET_FRAME_SIZE) {
            eb->io_count++;
            er->io_count++;
            rc = 0;
        }
    }
    xspin_unlock(&g_lock);
    return rc;
}

/* Undoes one xtok_ring_pin. Tokens with a pin cannot be released, so both
 * lookups succeed while the pin exists. */
void xtok_ring_unpin(XToken blk, XToken ram) {
    XTokenEntry *eb, *er;
    xspin_lock(&g_lock);
    eb = lookup_live(blk);
    er = lookup_live(ram);
    if (eb && eb->io_count != 0) eb->io_count--;
    if (er && er->io_count != 0) er->io_count--;
    xspin_unlock(&g_lock);
}

/* Device and first absolute block of a live block token. For the NVMe adapter,
 * which must turn a ring entry's relative lba into a device lba. */
int xtok_blk_range(XToken tok, void **dev, UINT64 *start) {
    XTokenEntry *e;
    int rc = -1;
    xspin_lock(&g_lock);
    e = lookup_live(tok);
    if (e && e->kind == XTOK_KIND_BLOCKS) {
        if (dev)   *dev   = e->dev;
        if (start) *start = e->start;
        rc = 0;
    }
    xspin_unlock(&g_lock);
    return rc;
}


/* Kernel retention: blocks release while held. Mapping and device I/O are not
 * affected, so the owner can still map its ring while the table keeps it. */
int xtok_hold(XToken tok) {
    XTokenEntry *e;
    int rc = -1;
    xspin_lock(&g_lock);
    e = lookup_live(tok);
    if (e && e->hold != 0xFFFFFFFFu) { e->hold++; rc = 0; }
    xspin_unlock(&g_lock);
    return rc;
}

void xtok_unhold(XToken tok) {
    XTokenEntry *e;
    xspin_lock(&g_lock);
    e = lookup_live(tok);
    if (e && e->hold != 0) e->hold--;
    xspin_unlock(&g_lock);
}

/* ---- Root capabilities ----------------------------------------------------
 * One root per device, minted once at boot for a single holder. The root names
 * the whole device; claim returns a block token over it, only to the holder.
 * No name table, no identity table: the device pointer is the identity. */
#define XTOK_ROOTS_MAX 8
static struct { void *dev; UINT32 holder; int used; } g_root[XTOK_ROOTS_MAX];

void xtok_roots_reset(void) {
    UINT32 i;
    for (i = 0; i < XTOK_ROOTS_MAX; i++) g_root[i].used = 0;
}

static int root_find(void *dev) {
    UINT32 i;
    for (i = 0; i < XTOK_ROOTS_MAX; i++)
        if (g_root[i].used && g_root[i].dev == dev) return (int)i;
    return -1;
}

XToken xtok_root_mint(void *dev, UINT32 holder) {
    UINT32 i;
    BlkDev *d = (BlkDev *)dev;
    if (!d || root_find(dev) >= 0) return XTOKEN_INVALID;
    for (i = 0; i < XTOK_ROOTS_MAX; i++)
        if (!g_root[i].used) {
            g_root[i].used = 1;
            g_root[i].dev = dev;
            g_root[i].holder = holder;
            return (XToken)(i + 1);   /* the root's handle: nonzero on success */
        }
    return XTOKEN_INVALID;
}

UINT32 xtok_root_holder(void *dev) {
    int i = root_find(dev);
    return i < 0 ? 0 : g_root[i].holder;
}

XToken xtok_root_claim(void *dev, UINT32 owner) {
    int i = root_find(dev);
    BlkDev *d = (BlkDev *)dev;
    if (i < 0 || g_root[i].holder != owner || !d) return XTOKEN_INVALID;
    return xtok_alloc_blocks(owner, dev, 0, d->sector_count,
                             XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_RELEASE |
                             XTOK_RIGHT_GIVE);
}

/* Derivation, one lock: the parent is looked up, validated and the child slot is
 * claimed and filled before the lock is released, so no check can go stale. */
XToken xtok_derive_blocks(UINT32 owner, XToken parent, UINT64 first, UINT64 count,
                          UINT32 rights) {
    XTokenEntry *p, *e;
    UINT32 slot;
    XToken tok = XTOKEN_INVALID;

    if (count == 0) return XTOKEN_INVALID;
    if (rights == 0) return XTOKEN_INVALID;

    xspin_lock(&g_lock);
    p = lookup_live(parent);
    if (p && p->frozen) goto out;   /* offered: no derivation until accept/cancel */
    if (!p || p->kind != XTOK_KIND_BLOCKS)                  goto out;
    if (p->owner_id != owner)                               goto out;
    if ((rights & ~p->rights) != 0)                         goto out;   /* no widening of rights */
    if (first > p->n_frames || count > p->n_frames - first) goto out;   /* no widening of range */
    if (claim_slot(&slot) != 0)                             goto out;

    e = &g_table[slot];
    e->kind     = XTOK_KIND_BLOCKS;
    e->owner_id = owner;
    e->rights   = rights;
    e->phys     = 0;
    e->n_frames = count;
    e->dev      = p->dev;
    e->start    = p->start + first;
    e->parent   = parent;
    e->io_count = 0;
    e->hold     = 0;
    e->frozen = 0;
    e->live     = 1;
    g_live++;
    tok = pack_token(slot, e->generation);
out:
    xspin_unlock(&g_lock);
    return tok;
}

/* Transfer of titularity: the token stays the same object, only owner_id moves.
 * The destination's liveness is checked outside g_lock (owner.c has no lock).
 * The per-owner budget of the destination is NOT checked here; see the notes. */
int xtok_give(UINT32 giver, XToken tok, UINT32 dest) {
    XTokenEntry *e;
    int rc = -1;
    if (dest == giver || !owner_is_live(dest)) return -1;
    xspin_lock(&g_lock);
    e = lookup_live(tok);
    if (!e || e->owner_id != giver)                  goto out;
    if ((e->rights & XTOK_RIGHT_GIVE) == 0)          goto out;
    if (e->io_count != 0 || e->map_count != 0)       goto out;
    if (e->hold != 0)                                goto out;
    e->owner_id = dest;
    rc = 0;
out:
    xspin_unlock(&g_lock);
    return rc;
}

/* ---- Endpoint layer primitives ------------------------------------------ */
XToken xtok_alloc_endpoint(UINT32 owner, UINT64 value, UINT32 rights) {
    UINT32 slot;
    XTokenEntry *e;
    XToken tok;
    xspin_lock(&g_lock);
    if (claim_slot(&slot) != 0) {
        xspin_unlock(&g_lock);
        return XTOKEN_INVALID;
    }
    e = &g_table[slot];
    e->kind     = XTOK_KIND_ENDPOINT;
    e->owner_id = owner;
    e->rights   = rights;
    e->phys     = 0;
    e->n_frames = 0;
    e->dev      = 0;
    e->start    = value;       /* endpoint.c stores (generation, slot) here */
    e->io_count = 0;
    e->map_count = 0;
    e->hold     = 0;
    e->frozen   = 0;
    e->live     = 1;
    g_live++;
    tok = pack_token(slot, e->generation);
    xspin_unlock(&g_lock);
    return tok;
}

int xtok_ep_value(XToken tok, UINT32 owner, UINT32 right, UINT64 *value) {
    XTokenEntry *e;
    int rc = -1;
    xspin_lock(&g_lock);
    e = lookup_live(tok);
    if (e && e->kind == XTOK_KIND_ENDPOINT && e->owner_id == owner &&
        (e->rights & right) == right) {
        *value = e->start;
        rc = 0;
    }
    xspin_unlock(&g_lock);
    return rc;
}

/* Freezes a transferable token. Requires GIVE, no pins, no mappings, no hold,
 * and not already frozen. */
int xtok_offer_begin(UINT32 giver, XToken tok) {
    XTokenEntry *e;
    int rc = -1;
    xspin_lock(&g_lock);
    e = lookup_live(tok);
    if (e && e->kind != XTOK_KIND_ENDPOINT && e->owner_id == giver &&
        (e->rights & XTOK_RIGHT_GIVE) != 0 &&
        e->io_count == 0 && e->map_count == 0 && e->hold == 0 && e->frozen == 0) {
        e->frozen = 1;
        rc = 0;
    }
    xspin_unlock(&g_lock);
    return rc;
}

/* Accept: the frozen token changes owner and is thawed. */
int xtok_thaw_to(XToken tok, UINT32 owner) {
    XTokenEntry *e;
    int rc = -1;
    xspin_lock(&g_lock);
    e = lookup_live(tok);
    if (e && e->frozen) {
        e->owner_id = owner;
        e->frozen = 0;
        rc = 0;
    }
    xspin_unlock(&g_lock);
    return rc;
}

/* Cancel: thaw in place, same owner. */
int xtok_thaw(XToken tok) {
    XTokenEntry *e;
    int rc = -1;
    xspin_lock(&g_lock);
    e = lookup_live(tok);
    if (e && e->frozen) {
        e->frozen = 0;
        rc = 0;
    }
    xspin_unlock(&g_lock);
    return rc;
}

/* Read-only snapshot of the live entries, for tests/arch/amd64 invariants. */
UINT32 xtok_inspect_all(XTokenView *out, UINT32 max) {
    UINT32 slot, n = 0;
    xspin_lock(&g_lock);
    for (slot = 0; slot < XTOK_TABLE_SLOTS && n < max; slot++) {
        XTokenEntry *e = &g_table[slot];
        if (!e->live) continue;
        out[n].tok    = pack_token(slot, e->generation);
        out[n].kind   = e->kind;
        out[n].owner  = e->owner_id;
        out[n].rights = e->rights;
        out[n].start  = e->start;
        out[n].count  = e->n_frames;
        out[n].frozen = e->frozen;
        out[n].hold   = e->hold;
        out[n].io_count = e->io_count;
        out[n].map_count = e->map_count;
        out[n].parent = e->parent;
        n++;
    }
    xspin_unlock(&g_lock);
    return n;
}

/* ---- Memory budgets (interface: mem/budget.h) -------------------------- */
static XToken seal_budget_locked(UINT32 owner, UINT64 frames, XToken parent) {
    UINT32 slot;
    XTokenEntry *e;
    if (claim_slot(&slot) != 0) return XTOKEN_INVALID;
    e = &g_table[slot];
    e->kind      = XTOK_KIND_BUDGET;
    e->owner_id  = owner;
    e->rights    = XTOK_RIGHT_RELEASE | XTOK_RIGHT_GIVE;
    e->phys      = 0;
    e->n_frames  = frames;
    e->dev       = 0;
    e->start     = 0;
    e->io_count  = 0;
    e->map_count = 0;
    e->hold      = 0;
    e->frozen    = 0;
    e->parent    = parent;
    e->live      = 1;
    g_live++;
    return pack_token(slot, e->generation);
}

static XTokenEntry *budget_of_locked(XToken tok, UINT32 owner) {
    XTokenEntry *e = lookup_live(tok);
    if (!e || e->kind != XTOK_KIND_BUDGET || e->owner_id != owner) return 0;
    return e;
}

int xtok_budget_mint(UINT32 owner, UINT64 frames, XToken *out) {
    XToken t;
    if (out) *out = XTOKEN_INVALID;
    if (frames == 0) return -1;
    xspin_lock(&g_lock);
    t = seal_budget_locked(owner, frames, XTOKEN_INVALID);
    xspin_unlock(&g_lock);
    if (t == XTOKEN_INVALID) return -1;
    if (out) *out = t;
    return 0;
}

int xtok_budget_derive(UINT32 owner, XToken parent, UINT64 frames, XToken *out) {
    XTokenEntry *p;
    XToken t = XTOKEN_INVALID;
    if (out) *out = XTOKEN_INVALID;
    if (frames == 0) return -1;
    xspin_lock(&g_lock);
    p = budget_of_locked(parent, owner);
    if (p && p->frozen == 0 && p->n_frames >= frames) {
        t = seal_budget_locked(owner, frames, parent);
        if (t != XTOKEN_INVALID) p->n_frames -= frames;
    }
    xspin_unlock(&g_lock);
    if (t == XTOKEN_INVALID) return -1;
    if (out) *out = t;
    return 0;
}

int xtok_budget_balance(XToken bud, UINT32 owner, UINT64 *frames) {
    XTokenEntry *p;
    int rc = -1;
    xspin_lock(&g_lock);
    p = budget_of_locked(bud, owner);
    if (p && frames) { *frames = p->n_frames; rc = 0; }
    xspin_unlock(&g_lock);
    return rc;
}

/* Pays for a RAM token from a budget. Frames are allocated first, outside the
 * lock; if the budget refuses, they go straight back. */
int xtok_alloc_ram_from(UINT32 owner, XToken bud, UINT64 frames, UINT32 rights, XToken *out) {
    UINT64 phys;
    XTokenEntry *p, *e;
    UINT32 slot;
    XToken t = XTOKEN_INVALID;
    if (out) *out = XTOKEN_INVALID;
    if (frames == 0) return -1;
    phys = phys_alloc_frames(frames);
    if (phys == 0) return -1;
    zero_frames(phys, frames);

    xspin_lock(&g_lock);
    p = budget_of_locked(bud, owner);
    if (p && p->frozen == 0 && p->n_frames >= frames && claim_slot(&slot) == 0) {
        p->n_frames -= frames;
        e = &g_table[slot];
        e->kind      = XTOK_KIND_RAM;
        e->owner_id  = owner;
        e->rights    = rights;
        e->phys      = phys;
        e->n_frames  = frames;
        e->dev       = 0;
        e->start     = 0;
        e->io_count  = 0;
        e->map_count = 0;
        e->hold      = 0;
        e->frozen    = 0;
        e->parent    = bud;
        e->live      = 1;
        g_live++;
        t = pack_token(slot, e->generation);
    }
    xspin_unlock(&g_lock);

    if (t == XTOKEN_INVALID) {
        phys_free_frames(phys, frames);
        return -1;
    }
    if (out) *out = t;
    return 0;
}
