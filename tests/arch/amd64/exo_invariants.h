/* exo_invariants.h: the global invariants of the mechanism layer, checked
 * against the observable state of the ledger after every operation. A property
 * that holds for every reachable state is what makes the kernel an exokernel:
 * no authority appears without a holder, nothing widens, nothing is copied. */
#ifndef XASMOS_EXO_INVARIANTS_H
#define XASMOS_EXO_INVARIANTS_H

#include <stdint.h>
#include "capability.h"

/* Reproducible pseudo-random source. The seed is printed on failure. */
typedef struct { uint64_t s; } ExoRng;

static inline void exo_seed(ExoRng *r, uint64_t seed) { r->s = seed ? seed : 1; }
static inline uint64_t exo_next(ExoRng *r) {
    r->s ^= r->s << 13; r->s ^= r->s >> 7; r->s ^= r->s << 17;
    return r->s;
}
static inline uint32_t exo_pick(ExoRng *r, uint32_t n) {
    return n ? (uint32_t)(exo_next(r) % n) : 0;
}

/* Result of checking one state. Each invariant has its own number so a failure
 * names the property that broke. */
enum {
    EXO_OK = 0,
    EXO_I1_ORPHAN_TOKEN,      /* a live token with no holder */
    EXO_I2_CHILD_WIDER,       /* a child with more range or rights than its parent */
    EXO_I3_FROZEN_USED,       /* a frozen token was usable */
    EXO_I4_TWO_HOLDERS,       /* a token claimed by two owners */
    EXO_I5_LEAK,              /* tokens left after every owner retired */
};

const char *exo_invariant_name(int code);

/* Checks every live token in the ledger. Returns EXO_OK or the first broken
 * invariant. Reads the ledger only through its public inspection calls. */
int exo_check_state(void);


#endif
