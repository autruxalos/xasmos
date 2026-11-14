/******************************************************************************
 * XASMOS - table of program rings
 * Path: source/arch/amd64/kernel/xeret/xring_table.c
 *
 * Creation: id, owner cap, ring memory from a RAM token, retention (hold),
 * then the KRing. Release (xrt_reap, ring 0, no locks): only when closing and
 * nothing in flight. Order: revoke the program's mappings, drop the table's
 * retention, unbind the route, release the token. The id returns to the pool
 * only after the token is gone.
 ******************************************************************************/
#include "xring_table.h"

/* Revokes every mapping of a token in every address space (paging.c). */
int xas_revoke_token(XToken tok);

#define XRT_IDS  32u        /* ids 1..31; 0 is the boot ring */
#define XRT_CAPS 8u         /* owners with a configured cap */

typedef struct {
    int     used;
    UINT32  owner;
    XToken  tok;
    KRing   r;
} XrtEntry;

static XrtEntry g_ent[XRT_IDS];
static struct { int used; UINT32 owner; UINT32 cap; } g_cap[XRT_CAPS];
static KrUnbindFn g_unbind;
static KrBindFn   g_bind;

void xrt_init(KrUnbindFn unbind, KrBindFn bind) {
    uint32_t i;
    for (i = 0; i < XRT_IDS; i++)  g_ent[i].used = 0;
    for (i = 0; i < XRT_CAPS; i++) g_cap[i].used = 0;
    g_unbind = unbind;
    g_bind = bind;
}

static UINT32 cap_of(UINT32 owner) {
    uint32_t i;
    for (i = 0; i < XRT_CAPS; i++)
        if (g_cap[i].used && g_cap[i].owner == owner) return g_cap[i].cap;
    return 0;
}

void xrt_set_cap(UINT32 owner, UINT32 n) {
    uint32_t i;
    for (i = 0; i < XRT_CAPS; i++)
        if (g_cap[i].used && g_cap[i].owner == owner) { g_cap[i].cap = n; return; }
    for (i = 0; i < XRT_CAPS; i++)
        if (!g_cap[i].used) {
            g_cap[i].used = 1; g_cap[i].owner = owner; g_cap[i].cap = n;
            return;
        }
}

UINT32 xrt_owner_count(UINT32 owner) {
    uint32_t i, n = 0;
    for (i = 1; i < XRT_IDS; i++)
        if (g_ent[i].used && g_ent[i].owner == owner) n++;
    return n;
}

/* Route target: a completion for this ring arrives here from the driver. */
static void xrt_route_done(void *arg, uint32_t slot, int32_t st) {
    kr_complete((KRing *)arg, slot, st);
}

int xrt_create(UINT32 owner, UINT32 entries, XToken *out) {
    uint32_t id;
    UINT64 phys = 0, frames = 0, need;
    XToken tok;
    XrtEntry *e;

    if (out) *out = XTOKEN_INVALID;
    if (entries < XQ_MIN_ENTRIES || entries > XRT_MAX_ENTRIES ||
        (entries & (entries - 1u)) != 0) return XRT_E_ARG;
    if (xrt_owner_count(owner) >= cap_of(owner)) return XRT_E_CAP;
    for (id = 1; id < XRT_IDS && g_ent[id].used; id++) { }
    if (id >= XRT_IDS) return XRT_E_NOMEM;

    need = (xq_bytes(entries) + 4095u) / 4096u;
    tok = xtok_alloc_ram(owner, need, XTOK_RIGHTS_RAM_DEFAULT);
    if (tok == XTOKEN_INVALID) return XRT_E_NOMEM;
    if (xtok_ram_info(tok, &phys, &frames) != 0 || xtok_hold(tok) != 0) {
        xtok_release(tok);
        return XRT_E_NOMEM;
    }

    xq_init((void *)(uintptr_t)phys, entries);
    e = &g_ent[id];
    e->owner = owner;
    e->tok   = tok;
    kr_init(&e->r, owner, (void *)(uintptr_t)phys, entries, kr_device_submit, &e->r);
    e->r.id = id;
    /* The id is used only once its route is bound: a refused bind leaves no ring. */
    if (!g_bind || g_bind(id, xrt_route_done, &e->r) != 0) {
        xtok_unhold(tok);
        xtok_release(tok);
        return XRT_E_NOMEM;
    }
    e->used = 1;
    if (out) *out = tok;
    return (int)id;
}

int xrt_destroy(UINT32 owner, XToken tok) {
    uint32_t id;
    for (id = 1; id < XRT_IDS; id++) {
        XrtEntry *e = &g_ent[id];
        if (e->used && e->tok == tok && e->owner == owner) {
            kr_close(&e->r);
            return 0;
        }
    }
    return -1;
}

void xrt_reap(void) {
    uint32_t id;
    for (id = 1; id < XRT_IDS; id++) {
        XrtEntry *e = &g_ent[id];
        if (!e->used || !e->r.closing || e->r.inflight != 0) continue;

        xas_revoke_token(e->tok);       /* the program's view goes first     */
        xtok_unhold(e->tok);            /* the table's retention ends        */
        /* kr_reap_closed unbinds the route, then releases the token. If the
         * release is refused, the entry stays and the next reap retries. */
        if (kr_reap_closed(&e->r, e->tok, id, g_unbind) == 1)
            e->used = 0;
    }
}

KRing *xrt_ring(uint32_t id) {
    if (id < 1 || id >= XRT_IDS || !g_ent[id].used) return 0;
    return &g_ent[id].r;
}

XToken xrt_token(uint32_t id) {
    if (id < 1 || id >= XRT_IDS || !g_ent[id].used) return XTOKEN_INVALID;
    return g_ent[id].tok;
}

int xrt_lookup(UINT32 owner, XToken tok) {
    uint32_t id;
    if (tok == XTOKEN_INVALID) return -1;
    for (id = 1; id < XRT_IDS; id++) {
        XrtEntry *e = &g_ent[id];
        if (e->used && e->owner == owner && e->tok == tok) return (int)id;
    }
    return -1;
}

UINT32 xrt_destroy_owner(UINT32 owner) {
    uint32_t id;
    UINT32 n = 0;
    for (id = 1; id < XRT_IDS; id++) {
        XrtEntry *e = &g_ent[id];
        if (e->used && e->owner == owner && !e->r.closing) {
            kr_close(&e->r);
            n++;
        }
    }
    return n;
}
