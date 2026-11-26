/******************************************************************************
 * XASMOS - table of program rings (interface)
 * Path: source/arch/amd64/kernel/xeret/xring_table.h
 *
 * The one source of truth for which rings exist. Ids are 1..31 (0 is the boot
 * ring). A ring is held by the table from creation until it is released; an
 * id is handed out again only after that release.
 ******************************************************************************/
#ifndef XASMOS_XRING_TABLE_H
#define XASMOS_XRING_TABLE_H

#include "xring.h"

#define XRT_E_ARG    (-1)
#define XRT_E_CAP    (-2)
#define XRT_E_NOMEM  (-3)
#define XRT_MAX_ENTRIES 256u

/* Route binding, injected so this file never depends on the driver. Same
 * contract as nvme_route_bind: fails if the id already has a live route. */
typedef int (*KrBindFn)(uint32_t id, void (*fn)(void *arg, uint32_t slot, int32_t st), void *arg);
void     xrt_init(KrUnbindFn unbind, KrBindFn bind);
UINT32   xrt_owner_count(UINT32 owner);
int      xrt_create(UINT32 owner, XToken budget, UINT32 entries, XToken *out);
int      xrt_destroy(UINT32 owner, XToken tok);
void     xrt_reap(void);
/* Id of the ring that this token names for this owner, or -1. A foreign
 * owner gets the same answer as an unknown token. */
int      xrt_lookup(UINT32 owner, XToken tok);
/* Closes every open ring of an owner. Returns how many were newly closed.
 * Released later by xrt_reap, once each ring has nothing in flight. */
UINT32   xrt_destroy_owner(UINT32 owner);
KRing   *xrt_ring(uint32_t id);
XToken   xrt_token(uint32_t id);

#endif /* XASMOS_XRING_TABLE_H */
