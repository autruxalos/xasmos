/******************************************************************************
 * XASMOS - endpoints (mechanism)
 * Path: source/arch/amd64/kernel/xeret/mem/endpoint.c
 *
 * The endpoint table is the only state here. Each endpoint has one pending
 * offer. A SEND or RECV token carries (generation << 32 | slot); a destroyed
 * endpoint bumps nothing until reused, and its old tokens fail the generation
 * check, so they cannot offer or accept.
 ******************************************************************************/
#include "endpoint.h"

#define XEP_MAX 32u

typedef struct {
    int    used;
    UINT32 gen;
    UINT32 recv;
    XToken pending;
    UINT32 giver;
} Endpoint;

static Endpoint g_ep[XEP_MAX];

static UINT64 pack_ep(UINT32 slot, UINT32 gen) {
    return ((UINT64)gen << 32) | slot;
}

static int ep_slot(XToken tok, UINT32 owner, UINT32 right, UINT32 *slot) {
    UINT64 v = 0;
    UINT32 idx, gen;
    if (xtok_ep_value(tok, owner, right, &v) != 0) return -1;
    idx = (UINT32)(v & 0xFFFFFFFFu);
    gen = (UINT32)(v >> 32);
    if (idx >= XEP_MAX || !g_ep[idx].used || g_ep[idx].gen != gen) return -1;
    *slot = idx;
    return 0;
}

int xep_create(UINT32 recv_owner, XToken *ep_recv) {
    UINT32 i;
    XToken t;
    if (ep_recv) *ep_recv = XTOKEN_INVALID;
    for (i = 0; i < XEP_MAX; i++) if (!g_ep[i].used) break;
    if (i == XEP_MAX) return -1;
    g_ep[i].used = 1;
    g_ep[i].gen++;
    g_ep[i].recv = recv_owner;
    g_ep[i].pending = XTOKEN_INVALID;
    g_ep[i].giver = 0;
    t = xtok_alloc_endpoint(recv_owner, pack_ep(i, g_ep[i].gen),
                            XTOK_RIGHT_RECV | XTOK_RIGHT_RELEASE);
    if (t == XTOKEN_INVALID) { g_ep[i].used = 0; return -1; }
    *ep_recv = t;
    return 0;
}

int xep_send_cap(UINT32 recv_owner, XToken ep_recv, UINT32 holder, XToken *ep_send) {
    UINT32 s;
    XToken t;
    if (ep_send) *ep_send = XTOKEN_INVALID;
    if (ep_slot(ep_recv, recv_owner, XTOK_RIGHT_RECV, &s) != 0) return -1;
    t = xtok_alloc_endpoint(holder, pack_ep(s, g_ep[s].gen),
                            XTOK_RIGHT_SEND | XTOK_RIGHT_RELEASE);
    if (t == XTOKEN_INVALID) return -1;
    *ep_send = t;
    return 0;
}

int xep_destroy(UINT32 recv_owner, XToken ep_recv) {
    UINT32 s;
    if (ep_slot(ep_recv, recv_owner, XTOK_RIGHT_RECV, &s) != 0) return -1;
    if (g_ep[s].pending != XTOKEN_INVALID) {
        xtok_thaw(g_ep[s].pending);
        g_ep[s].pending = XTOKEN_INVALID;
    }
    g_ep[s].used = 0;
    return xtok_release_owned(ep_recv, recv_owner) == 0 ? 0 : -1;
}

int xtok_offer(UINT32 giver, XToken tok, XToken ep_send) {
    UINT32 s;
    if (tok == XTOKEN_INVALID || tok == ep_send) return -1;
    if (ep_slot(ep_send, giver, XTOK_RIGHT_SEND, &s) != 0) return -1;
    if (g_ep[s].pending != XTOKEN_INVALID) return -1;      /* one offer at a time */
    if (xtok_offer_begin(giver, tok) != 0) return -1;      /* freezes tok */
    g_ep[s].pending = tok;
    g_ep[s].giver = giver;
    return 0;
}

int xtok_accept(UINT32 recv_owner, XToken ep_recv, XToken *out) {
    UINT32 s;
    XToken t;
    if (out) *out = XTOKEN_INVALID;
    if (ep_slot(ep_recv, recv_owner, XTOK_RIGHT_RECV, &s) != 0) return -1;
    t = g_ep[s].pending;
    if (t == XTOKEN_INVALID) return -1;
    if (xtok_thaw_to(t, recv_owner) != 0) return -1;
    g_ep[s].pending = XTOKEN_INVALID;
    g_ep[s].giver = 0;
    if (out) *out = t;
    return 0;
}

int xtok_cancel(UINT32 giver, XToken tok) {
    UINT32 i;
    for (i = 0; i < XEP_MAX; i++) {
        if (g_ep[i].used && g_ep[i].pending == tok && g_ep[i].giver == giver) {
            if (xtok_thaw(tok) != 0) return -1;
            g_ep[i].pending = XTOKEN_INVALID;
            g_ep[i].giver = 0;
            return 0;
        }
    }
    return -1;
}
