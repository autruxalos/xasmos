/* exo_invariants.c: the global invariants over the live ledger. Reads only
 * through xtok_inspect_all. A table of derivations, kept by the test, supplies
 * parents until the kernel tracks them (F1). */
#include <string.h>
#include "exo_invariants.h"
#include "endpoint.h"

#define EXO_MAX 512u
static XTokenView g_view[EXO_MAX];

static const XTokenView *find(UINT32 n, XToken t) {
    for (UINT32 i = 0; i < n; i++) if (g_view[i].tok == t) return &g_view[i];
    return 0;
}

const char *exo_invariant_name(int code) {
    switch (code) {
    case EXO_OK:              return "ok";
    case EXO_I1_ORPHAN_TOKEN: return "I1: live token without a holder";
    case EXO_I2_CHILD_WIDER:  return "I2: child wider than its parent";
    case EXO_I3_FROZEN_USED:  return "I3: frozen token was usable";
    case EXO_I4_TWO_HOLDERS:  return "I4: token with two holders";
    case EXO_I5_LEAK:         return "I5: tokens left after retirement";
    default:                  return "unknown";
    }
}

int exo_check_state(void) {
    UINT32 n = xtok_inspect_all(g_view, EXO_MAX);

    for (UINT32 i = 0; i < n; i++) {
        const XTokenView *v = &g_view[i];

        /* I1: every live token has an owner id that is not zero. A token
         * minted by the kernel always carries its holder. */
        if (v->owner == 0) return EXO_I1_ORPHAN_TOKEN;

        /* I3: a frozen token must not be in use: no pins, no mappings. */
        if (v->frozen && (v->io_count != 0 || v->map_count != 0)) return EXO_I3_FROZEN_USED;
    }

    /* I2: a live child fits its live parent in range and rights. A released
     * parent leaves its children valid; they simply have no live parent. */
    for (UINT32 i = 0; i < n; i++) {
        const XTokenView *c = &g_view[i];
        if (c->parent == XTOKEN_INVALID || c->kind != XTOK_KIND_BLOCKS) continue;
        const XTokenView *p2 = find(n, c->parent);
        if (!p2 || p2->kind != XTOK_KIND_BLOCKS) continue;
        if (c->rights & ~p2->rights) return EXO_I2_CHILD_WIDER;
        if (c->start < p2->start) return EXO_I2_CHILD_WIDER;
        if (c->start - p2->start + c->count > p2->count) return EXO_I2_CHILD_WIDER;
    }

    /* I4: a token id appears at most once in the snapshot. */
    for (UINT32 i = 0; i < n; i++)
        for (UINT32 j = i + 1; j < n; j++)
            if (g_view[i].tok == g_view[j].tok) return EXO_I4_TWO_HOLDERS;

    return EXO_OK;
}
