/******************************************************************************
 * XASMOS - kernel side of a submission ring (validation, dispatch, completion)
 * Path: source/arch/amd64/kernel/xeret/xring.c
 *
 * Completion order, fixed on purpose:
 *   1. the CQE is posted (xq_complete); if the ring is full, nothing else moves
 *   2. the pins of the slot are released
 *   3. inflight falls by one
 * Which rings exist is decided by xring_table.c, not here.
 ******************************************************************************/
#include "xring.h"
#include "blk.h"

static int kr_refuse(void *arg, uint32_t slot, const XqSqe *e) {
    (void)arg; (void)slot; (void)e;
    return -1;
}

void kr_init(KRing *r, UINT32 owner, void *mem, UINT32 entries,
             KrSubmitFn submit, void *arg) {
    UINT32 i;
    r->owner      = owner;
    r->entries    = entries;
    r->mem        = mem;
    r->submit     = submit ? submit : kr_refuse;
    r->submit_arg = arg;
    r->inflight   = 0;
    r->closing    = 0;
    for (i = 0; i < XRING_MAX_ENTRIES; i++) {
        r->blk[i] = XTOKEN_INVALID;
        r->ram[i] = XTOKEN_INVALID;
    }
}

UINT32 kr_inflight(const KRing *r) {
    return r->inflight;
}

static int kr_validate(void *ctx, const XqSqe *e) {
    KRing *r = (KRing *)ctx;
    if (xtok_ring_pin(r->owner, e->blk_token, e->ram_token,
                      e->op, e->lba, e->count) != 0) {
        return XQ_ST_INVALID;
    }
    return 0;
}

/* Not taken: the pins go back and inflight does not move. A BUSY entry stays
 * queued and is validated and pinned again on the next pass. */
static int kr_dispatch(void *ctx, uint32_t slot, const XqSqe *e) {
    KRing *r = (KRing *)ctx;
    int rc;
    /* Register before handing over: the device may complete the slot inside
     * submit, and kr_complete must see it in flight. */
    r->blk[slot] = e->blk_token;
    r->ram[slot] = e->ram_token;
    r->inflight++;

    rc = r->submit(r->submit_arg, slot, e);
    if (rc != 0) {
        /* Refused: the device did not take it, so it cannot have completed it.
         * Undo the registration and give the pins back. */
        r->blk[slot] = XTOKEN_INVALID;
        r->ram[slot] = XTOKEN_INVALID;
        r->inflight--;
        xtok_ring_unpin(e->blk_token, e->ram_token);
        return rc == XQ_ST_BUSY ? XQ_ST_BUSY : BLK_E_IO;
    }
    return 0;
}

int kr_complete(KRing *r, uint32_t slot, int32_t status) {
    uint32_t s = slot & (r->entries - 1u);
    if (r->blk[s] == XTOKEN_INVALID) return -1;          /* not in flight */
    if (xq_complete(r->mem, s, status) != 0) return -1;  /* CQE not posted: pins stay */
    xtok_ring_unpin(r->blk[s], r->ram[s]);
    r->blk[s] = XTOKEN_INVALID;
    r->ram[s] = XTOKEN_INVALID;
    r->inflight--;
    return 0;
}

uint32_t kr_process(KRing *r) {
    XqOps ops;
    ops.validate = kr_validate;
    ops.dispatch = kr_dispatch;
    ops.ctx      = r;
    return xq_process(r->mem, &ops);
}

void kr_close(KRing *r) {
    r->closing = 1;
}

/* Order, fixed: unbind the route, then release the ring token. The route goes
 * first so no completion can be routed to a ring that is being released. */
int kr_reap_closed(KRing *r, XToken ring_tok, uint32_t ring_id, KrUnbindFn unbind) {
    if (!r->closing || r->inflight != 0) return 0;
    if (unbind) unbind(ring_id);
    return xtok_release(ring_tok) == 0 ? 1 : -1;
}

/* Device path. The token decides the device; the ring never does. */
int kr_device_submit(void *arg, uint32_t slot, const XqSqe *e) {
    KRing *r = (KRing *)arg;
    void *dev = 0;
    UINT64 start = 0, phys = 0, frames = 0;
    BlkReq req;
    BlkDev *d;
    int rc;

    if (xtok_blk_range(e->blk_token, &dev, &start) != 0) return -1;
    if (xtok_ram_info(e->ram_token, &phys, &frames) != 0) return -1;
    if (!dev) return -1;
    d = (BlkDev *)dev;

    req.lba   = start + e->lba;
    req.count = e->count;
    req.op    = e->op;
    req.phys  = phys;
    req.ring  = r->id;
    req.slot  = slot;

    if (d->ops && d->ops->submit) return d->ops->submit(d->ctx, &req);

    /* Synchronous device: transfer now and complete inside this call. */
    if (req.op == BLK_OP_WRITE)
        rc = blk_write(d, req.lba, req.count, (const void *)(uintptr_t)phys);
    else
        rc = blk_read(d, req.lba, req.count, (void *)(uintptr_t)phys);
    kr_complete(r, slot, rc);
    return 0;
}
