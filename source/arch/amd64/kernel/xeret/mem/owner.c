/******************************************************************************
 * XASMOS - per-instance owners
 * Path: source/arch/amd64/kernel/xeret/mem/owner.c
 *
 * An id is live from owner_new until owner_retire. Retire refuses while the
 * ledger still counts tokens or frames for that id, so a retired id has nothing
 * attached and can be handed out again without inheriting anything.
 ******************************************************************************/
#include "owner.h"

static UINT8 g_live[XOWNER_MAX];

void owner_init(void) {
    UINT32 i;
    for (i = 0; i < XOWNER_MAX; i++) g_live[i] = 0;
}

UINT32 owner_new(void) {
    UINT32 i;
    for (i = 0; i < XOWNER_MAX; i++) {
        if (!g_live[i]) {
            g_live[i] = 1;
            return XOWNER_FIRST_INSTANCE + i;
        }
    }
    return 0;
}

int owner_retire(UINT32 id) {
    UINT64 frames = 0;
    UINT32 tokens = 0;
    UINT32 i;
    if (id < XOWNER_FIRST_INSTANCE || id >= XOWNER_FIRST_INSTANCE + XOWNER_MAX) return -1;
    i = id - XOWNER_FIRST_INSTANCE;
    if (!g_live[i]) return -1;
    xtok_owner_usage(id, &frames, &tokens);
    if (frames != 0 || tokens != 0) return -1;
    g_live[i] = 0;
    return 0;
}
