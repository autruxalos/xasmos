/* exo_invariants.c: the global invariants over the live ledger. Reads only
 * through xtok_inspect_all. A table of derivations, kept by the test, supplies
 * parents until the kernel tracks them (F1). */
#include <string.h>
#include "exo_invariants.h"
#include "endpoint.h"

#define EXO_MAX 512u
static XTokenView g_view[EXO_MAX];

/* Parent links kept by the test. The kernel records no parents yet (F1). */
typedef struct { XToken child; XToken parent; } ExoLink;
#define EXO_LINKS 512u
static ExoLink g_link[EXO_LINKS];
static UINT32  g_nlink;

void exo_link_reset(void) { g_nlink = 0; }
int  exo_link_add(XToken child, XToken parent) {
    if (g_nlink >= EXO_LINKS) return -1;
    g_link[g_nlink].child = child;
    g_link[g_nlink].parent = parent;
    g_nlink++;
    return 0;
}

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

    /* I2: every recorded child fits its parent in range and rights. */
    for (UINT32 l = 0; l < g_nlink; l++) {
        const XTokenView *c = find(n, g_link[l].child);
        const XTokenView *p = find(n, g_link[l].parent);
        if (!c || !p) continue;            /* released: nothing to check */
        if (c->rights & ~p->rights) return EXO_I2_CHILD_WIDER;
        if (c->start < p->start) return EXO_I2_CHILD_WIDER;
        if (c->start - p->start + c->count > p->count) return EXO_I2_CHILD_WIDER;
    }

    /* I4: a token id appears at most once in the snapshot. */
    for (UINT32 i = 0; i < n; i++)
        for (UINT32 j = i + 1; j < n; j++)
            if (g_view[i].tok == g_view[j].tok) return EXO_I4_TWO_HOLDERS;

    return EXO_OK;
}
