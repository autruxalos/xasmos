/* exo_sequence_test: random but reproducible sequences of mechanism operations
 * over several owners. After every operation the global invariants must hold,
 * and the operations that the contract refuses must be refused. Each sequence
 * ends with every owner retired, and the ledger must be empty again (I5). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "capability.h"
#include "blk.h"
#include "owner.h"
#include "endpoint.h"
#include "derive_call.h"
#include "exo_invariants.h"
#include "host_stubs.h"

#define NOWN   4u
#define POOL   64u
#define STEPS  300u
#define KIND_BLK 0
#define KIND_RAM 1
#define GIVEABLE (XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_RELEASE | XTOK_RIGHT_GIVE)

typedef struct { XToken tok; UINT32 owner; int kind; } Pool;

static Pool   pool[POOL];
static UINT32 npool;
static UINT32 owners[NOWN];
static BlkDev dev;
static UINT32 base_live;
static unsigned long long ops_done, refusals_checked;
static int violated;
static unsigned cur_seed, cur_step;

static void violation(const char *what) {
    if (violated) return;
    violated = 1;
    fprintf(stderr, "VIOLATION seed %u step %u: %s\n", cur_seed, cur_step, what);
}

static int find_pool(ExoRng *r, UINT32 owner, int kind) {
    int idx[POOL], n = 0;
    for (UINT32 i = 0; i < npool; i++)
        if ((owner == 0 || pool[i].owner == owner) && (kind < 0 || pool[i].kind == kind))
            idx[n++] = (int)i;
    return n ? idx[exo_pick(r, (uint32_t)n)] : -1;
}

static void pool_add(XToken t, UINT32 owner, int kind) {
    if (t == XTOKEN_INVALID) return;
    if (npool >= POOL) { violation("pool overflow would leak a token"); return; }
    pool[npool].tok = t;
    pool[npool].owner = owner;
    pool[npool].kind = kind;
    npool++;
}

static void pool_del(int i) { pool[i] = pool[--npool]; }

static void op_alloc(ExoRng *r) {
    UINT32 o = owners[exo_pick(r, NOWN)];
    if (npool >= POOL - 1) return;
    if (exo_pick(r, 2)) {
        UINT64 s = exo_pick(r, 56), n = 1 + exo_pick(r, 8);
        pool_add(xtok_alloc_blocks(o, &dev, s, n, GIVEABLE), o, KIND_BLK);
    } else {
        pool_add(xtok_alloc_ram(o, 1 + exo_pick(r, 4), XTOK_RIGHTS_RAM_DEFAULT), o, KIND_RAM);
    }
}

static void op_derive(ExoRng *r) {
    int i = find_pool(r, 0, KIND_BLK);
    if (i < 0 || npool >= POOL) return;
    XToken parent = pool[i].tok;
    UINT32 o = pool[i].owner;
    UINT64 f = exo_pick(r, 16), n = 1 + exo_pick(r, 16);
    UINT32 rights = XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE;   /* children keep RELEASE: see F1 */
    if (exo_pick(r, 2)) rights |= XTOK_RIGHT_WRITE;
    if (exo_pick(r, 2)) rights |= XTOK_RIGHT_GIVE;
    XToken c = XTOKEN_INVALID;
    if (xsys_derive(o, parent, f, n, rights, &c) == XDERIVE_OK) {
        pool_add(c, o, KIND_BLK);
        exo_link_add(c, parent);
    }
}

static void op_release(ExoRng *r) {
    int i = find_pool(r, 0, -1);
    if (i < 0) return;
    if (xtok_release_owned(pool[i].tok, pool[i].owner) == 0) pool_del(i);
}

/* Offer, then either accept or cancel. While the token is frozen the giver must
 * be refused on release and on derivation. */
static void op_transfer(ExoRng *r) {
    int i = find_pool(r, 0, KIND_BLK);
    if (i < 0) return;
    UINT32 g = pool[i].owner;
    UINT32 rv = owners[exo_pick(r, NOWN)];
    XToken tok = pool[i].tok;
    XToken er = XTOKEN_INVALID, es = XTOKEN_INVALID, got = XTOKEN_INVALID, d = XTOKEN_INVALID;

    if (xep_create(rv, &er) != 0) return;
    if (xep_send_cap(rv, er, g, &es) == 0) {
        if (xtok_offer(g, tok, es) == 0) {
            refusals_checked += 2;
            if (xtok_release_owned(tok, g) == 0) violation("frozen token released by giver");
            if (xsys_derive(g, tok, 0, 1, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE, &d) == XDERIVE_OK) {
                violation("frozen token derived by giver");
                xtok_release_owned(d, g);
            }
            if (exo_pick(r, 2)) {
                if (xtok_accept(rv, er, &got) == 0) pool[i].owner = rv;
                else violation("accept failed after a successful offer");
            } else {
                if (xtok_cancel(g, tok) != 0) violation("cancel failed after a successful offer");
            }
        }
        xtok_release_owned(es, g);
    }
    xep_destroy(rv, er);
}

static void op_map(ExoRng *r) {
    int i = find_pool(r, 0, KIND_RAM);
    if (i < 0) return;
    if (xtok_map_acquire(pool[i].tok, pool[i].owner, XTOK_RIGHT_READ, 0, 0) == XTOK_MAP_OK)
        xtok_map_release(pool[i].tok);
}

static void op_pin(ExoRng *r) {
    int b = find_pool(r, 0, KIND_BLK);
    if (b < 0) return;
    int m = find_pool(r, pool[b].owner, KIND_RAM);
    if (m < 0) return;
    if (xtok_ring_pin(pool[b].owner, pool[b].tok, pool[m].tok, XTOK_BLK_READ, 0, 1) == 0)
        xtok_ring_unpin(pool[b].tok, pool[m].tok);
}

static void step(ExoRng *r) {
    switch (exo_pick(r, 7)) {
    case 0: op_alloc(r);    break;
    case 1: op_derive(r);   break;
    case 2: op_release(r);  break;
    case 3: op_transfer(r); break;
    case 4: op_transfer(r); break;   /* transfers weighted: they carry the protocol */
    case 5: op_map(r);      break;
    default: op_pin(r);     break;
    }
    ops_done++;
}

int main(int argc, char **argv) {
    unsigned seeds = argc > 1 ? (unsigned)atoi(argv[1]) : 200u;
    unsigned bad_seeds = 0;
    host_dev_init(&dev, 512, 1024);
    xtok_init();
    owner_init();
    base_live = xtok_live_count();

    for (cur_seed = 1; cur_seed <= seeds && !violated; cur_seed++) {
        ExoRng rng;
        exo_seed(&rng, (uint64_t)cur_seed * 2654435761u + 1u);
        exo_link_reset();
        npool = 0;
        for (UINT32 o = 0; o < NOWN; o++) owners[o] = owner_new();

        for (cur_step = 0; cur_step < STEPS && !violated; cur_step++) {
            step(&rng);
            int c = exo_check_state();
            if (c != EXO_OK) {
                fprintf(stderr, "VIOLATION seed %u step %u: %s\n",
                        cur_seed, cur_step, exo_invariant_name(c));
                violated = 1;
            }
        }

        /* I5: give everything back, retire the owners, the ledger must be empty. */
        for (UINT32 i = npool; i-- > 0; )
            if (xtok_release_owned(pool[i].tok, pool[i].owner) != 0) violation("teardown could not release a token");
        npool = 0;
        for (UINT32 o = 0; o < NOWN; o++)
            if (owner_retire(owners[o]) != 0) violation("owner retired with tokens left");
        if (xtok_live_count() != base_live) {
            fprintf(stderr, "VIOLATION seed %u: I5 leak, %u live, expected %u\n",
                    cur_seed, xtok_live_count(), base_live);
            violated = 1;
        }
        if (violated) bad_seeds++;
    }

    printf("sequences: %u seeds x %u steps, ops %llu, refusals checked %llu, failures %u\n",
           seeds, STEPS, ops_done, refusals_checked, bad_seeds);
    return bad_seeds ? 1 : 0;
}

