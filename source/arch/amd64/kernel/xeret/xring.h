/******************************************************************************
 * XASMOS - kernel side of a submission ring (interface)
 * Path: source/arch/amd64/kernel/xeret/xring.h
 *
 * inflight counts the entries the device holds. Pins are per slot and sized
 * for the largest program ring (256 entries), a fixed static cost per ring.
 * Which rings exist, and when they are released, is decided by xring_table.c.
 ******************************************************************************/
#ifndef XASMOS_XRING_H
#define XASMOS_XRING_H

#include "xqueue.h"
#include "capability.h"

#define XRING_MAX_ENTRIES 256u

/* Device submission contract. The ring registers the slot before calling
 * this, so the device may complete it from inside the call. Returning
 * nonzero means the device did NOT take the entry: the ring undoes the
 * registration and returns the pins. A device must never complete a slot
 * and then return an error. XQ_ST_BUSY means no room, retry later. */
typedef int (*KrSubmitFn)(void *arg, uint32_t slot, const XqSqe *e);
typedef void (*KrUnbindFn)(uint32_t ring_id);

typedef struct {
    UINT32     owner;
    UINT32     entries;
    void      *mem;
    KrSubmitFn submit;
    void      *submit_arg;
    UINT32     inflight;                 /* entries the device holds now */
    uint32_t   id;                       /* route id, set by the table */
    UINT32     closing;                  /* the program asked to destroy it */
    XToken     blk[XRING_MAX_ENTRIES];   /* pinned block token, per slot */
    XToken     ram[XRING_MAX_ENTRIES];   /* pinned RAM token, per slot   */
} KRing;

void     kr_init(KRing *r, UINT32 owner, void *mem, UINT32 entries,
                 KrSubmitFn submit, void *arg);
int      kr_complete(KRing *r, uint32_t slot, int32_t status);
uint32_t kr_process(KRing *r);
UINT32   kr_inflight(const KRing *r);
void     kr_close(KRing *r);
int      kr_reap_closed(KRing *r, XToken ring_tok, uint32_t ring_id, KrUnbindFn unbind);

/* Device path: resolves the entry's tokens and hands the request to the
 * device that backs the block token. arg is the KRing. */
int      kr_device_submit(void *arg, uint32_t slot, const XqSqe *e);

int kr_boot_selftest_nvme(uint8_t *first_byte);

#endif /* XASMOS_XRING_H */
