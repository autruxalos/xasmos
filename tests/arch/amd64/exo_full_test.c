/* exo_full_test: the mechanism layer integrated. Boot mints and hands out the
 * roots; init lends the shell three lanes (budget, disk, volume); the shell lends
 * instances budgets, disk lanes and rings; instances try what they were not given;
 * everything is reaped or revoked; the global invariants hold after every step;
 * the ledger ends with exactly the permanent MEM volume. */
#include <stdio.h>
#include <string.h>
#include "capability.h"
#include "blk.h"
#include "owner.h"
#include "endpoint.h"
#include "derive_call.h"
#include "budget.h"
#include "exo_invariants.h"
#include "xring_table.h"
#include "host_stubs.h"
/* The ring layer is one translation unit with the test, as in xring_table_test. */
#include "xring.c"
#include "xring_table.c"

static int checks, failures, violations;

#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define STEP(name) do { int _c = exo_check_state(); if (_c != EXO_OK) { \
    violations++; fprintf(stderr, "VIOLATION after %s: %s\n", name, \
    exo_invariant_name(_c)); } } while (0)

#define LANE_RIGHTS (XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE | XTOK_RIGHT_GIVE)
#define RR          (XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE)

static BlkDev disk;

/* Ring bind hooks: same contract as xring_table_test.c. Copy its versions if the
 * signatures there differ. */
static int bound_ids[32];
static void unbind_hook(uint32_t id) { if (id < 32) bound_ids[id] = 0; }
static int bind_hook(uint32_t id, void (*fn)(void *, uint32_t, int32_t), void *arg) {
    (void)fn; (void)arg;
    if (id == 0 || id >= 32 || bound_ids[id]) return -1;
    bound_ids[id] = 1;
    return 0;
}

static UINT64 bal(XToken b, UINT32 o) {
    UINT64 v = 0;
    xtok_budget_balance(b, o, &v);
    return v;
}

/* Reap an instance: close its rings, then reclaim until nothing is left. Children
 * go before parents because a parent with live children refuses to die. */
static UINT32 reap_owner(UINT32 o) {
    XToken list[64];
    UINT32 n, k, progress, total = 0;
    xrt_destroy_owner(o);
    xrt_reap();
    for (;;) {
        n = xtok_owner_tokens(o, list, 64);
        if (n == 0) break;
        progress = 0;
        for (k = 0; k < n; k++) {
            xas_revoke_token(list[k]);
            if (xtok_reclaim(list[k]) == 0) progress++;
        }
        if (progress == 0) break;
        total += progress;
    }
    return total;
}

int main(void) {
    UINT64 p = 0, n = 0, v = 0;
    UINT32 boot, init, shell;
    XToken root_b, bud, vol, r_init, b_init, v_init;
    XToken recv[3], send[3];
    XToken b_sh_off, lane_off, extra, x = XTOKEN_INVALID;
    XToken b_sh, lane_sh, vol_sh;
    XToken lsub, lsx, rt = XTOKEN_INVALID;
    UINT64 fr[8];
    UINT32 base, ex;
    int i;

    host_dev_init(&disk, 512, 1024);
    xtok_init();
    owner_init();
    xtok_roots_reset();
    xrt_init(unbind_hook, bind_hook);
    base = xtok_live_count();

    /* 1. Boot: mint the disk root, a budget and a fragmented volume. */
    boot = owner_new(); init = owner_new(); shell = owner_new();
    CHECK(xtok_root_mint(&disk, boot) != XTOKEN_INVALID);
    root_b = xtok_root_claim(&disk, boot);
    CHECK(root_b != XTOKEN_INVALID);
    CHECK(xtok_budget_mint(boot, 4096, &bud) == 0);
    for (i = 0; i < 8; i++) { fr[i] = phys_alloc_frames(1); CHECK(fr[i] != 0); }
    CHECK(xtok_mem_wrap_list(boot, fr, 8, &vol) == 0);

    /* 2. Boot hands everything to init by offer/accept. */
    CHECK(xep_transfer(boot, root_b, init, &r_init) == 0);
    CHECK(xep_transfer(boot, bud, init, &b_init) == 0);
    CHECK(xep_transfer(boot, vol, init, &v_init) == 0);
    STEP("boot hands out");
    CHECK(xtok_release_owned(root_b, boot) == XTOK_REL_ESTALE);  /* boot keeps nothing */
    CHECK(xtok_budget_balance(b_init, init, &v) == 0 && v == 4096);

    /* 3. Init opens the shell's three lanes: one SEND per lane, minted for init. */
    for (i = 0; i < 3; i++) {
        CHECK(xep_create(shell, &recv[i]) == 0);
        CHECK(xep_send_cap(shell, recv[i], init, &send[i]) == 0);
    }
    STEP("shell endpoints");

    /* 4. Init offers one token per lane. */
    b_sh_off = XTOKEN_INVALID;
    CHECK(xtok_budget_derive(init, b_init, 2048, &b_sh_off) == 0);
    CHECK(xtok_offer(init, b_sh_off, send[0]) == 0);
    lane_off = xtok_derive_blocks(init, r_init, 0, 32, LANE_RIGHTS);
    CHECK(lane_off != XTOKEN_INVALID);
    CHECK(xtok_offer(init, lane_off, send[1]) == 0);
    CHECK(xtok_offer(init, v_init, send[2]) == 0);
    STEP("offers pending");

    /* Negatives while the offers are pending. */
    extra = xtok_derive_blocks(init, r_init, 40, 8, LANE_RIGHTS);
    CHECK(extra != XTOKEN_INVALID);
    CHECK(xtok_offer(init, extra, send[1]) == -1);          /* endpoint holds one offer */
    CHECK(xtok_release_owned(extra, init) == 0);
    CHECK(xtok_offer(init, r_init, recv[0]) == -1);         /* a RECV is not a SEND */
    CHECK(xtok_accept(init, recv[0], &x) == -1);            /* init does not hold the RECV */
    CHECK(xtok_release_owned(v_init, init) == XTOK_REL_ERIGHTS); /* MEM has no release path */
    CHECK(xtok_release_owned(lane_off, init) == XTOK_REL_EBUSY); /* frozen while offered */

    /* 5. The shell accepts each lane. */
    CHECK(xtok_accept(shell, recv[0], &b_sh) == 0);
    CHECK(xtok_accept(shell, recv[1], &lane_sh) == 0);
    CHECK(xtok_accept(shell, recv[2], &vol_sh) == 0);
    CHECK(xtok_accept(shell, recv[0], &x) == -1);           /* nothing pending any more */
    CHECK(bal(b_sh, shell) == 2048);
    CHECK(bal(b_init, init) == 2048);
    STEP("accepted");
    /* Moved, not copied: the giver's handles are now the shell's. */
    CHECK(xtok_release_owned(lane_sh, init) == XTOK_REL_ESTALE);
    CHECK(xtok_derive_blocks(init, lane_sh, 0, 1, RR) == XTOKEN_INVALID);

    /* 6. Three instances. Each gets a budget child and a disk lane child. */
    for (i = 0; i < 3; i++) {
        XToken cb = XTOKEN_INVALID, ci = XTOKEN_INVALID, cl, cli = XTOKEN_INVALID;
        XToken ram = XTOKEN_INVALID, far = XTOKEN_INVALID, d = XTOKEN_INVALID;
        UINT32 inst = owner_new();

        CHECK(xtok_budget_derive(shell, b_sh, 256, &cb) == 0);
        CHECK(xep_transfer(shell, cb, inst, &ci) == 0);
        cl = xtok_derive_blocks(shell, lane_sh, (UINT64)i * 8, 8, LANE_RIGHTS);
        CHECK(cl != XTOKEN_INVALID);
        CHECK(xep_transfer(shell, cl, inst, &cli) == 0);
        STEP("instance lent");

        /* It spends from its own child, and only that. */
        CHECK(xtok_alloc_ram_from(inst, ci, 16, XTOK_RIGHTS_RAM_DEFAULT, &ram) == 0);
        CHECK(xtok_alloc_ram_from(inst, ci, 300, XTOK_RIGHTS_RAM_DEFAULT, &far) != 0);
        CHECK(far == XTOKEN_INVALID);
        CHECK(bal(ci, inst) == 240);

        /* The ring is paid from the child budget. */
        rt = XTOKEN_INVALID;
        CHECK(xrt_create(inst, ci, 16, &rt) > 0);
        CHECK(bal(ci, inst) < 240);
        STEP("ring paid");

        /* Negatives: the shell's volume, the shell's lane, and a foreign derivation. */
        CHECK(xtok_map_acquire(vol_sh, inst, XTOK_RIGHT_READ, &p, &n) == XTOK_MAP_EOWNER);
        CHECK(xsys_derive(inst, lane_sh, 0, 4, RR, &d) == XDERIVE_REFUSED);
        CHECK(xsys_derive(inst, cli, 0, 4, RR, &d) == XDERIVE_OK);   /* its own lane: allowed */
        CHECK(xtok_release_owned(d, inst) == 0);

        /* Its lane is usable through a pin, and only inside its range. */
        CHECK(xtok_ring_pin(inst, cli, ram, XTOK_BLK_READ, 0, 8) == 0);
        xtok_ring_unpin(cli, ram);
        CHECK(xtok_ring_pin(inst, cli, ram, XTOK_BLK_READ, 0, 9) == -1);

        /* Reap: the ring, the RAM and the budget child all come back. */
        CHECK(reap_owner(inst) > 0);
        CHECK(owner_retire(inst) == 0);
        CHECK(bal(b_sh, shell) == 2048);
        STEP("instance reaped");
    }

    /* 7. Revocation: the shell derives a sub-lane, lends it, then revokes the lane. */
    lsub = xtok_derive_blocks(shell, lane_sh, 0, 4, LANE_RIGHTS);
    CHECK(lsub != XTOKEN_INVALID);
    ex = owner_new();
    CHECK(xep_transfer(shell, lsub, ex, &lsx) == 0);
    CHECK(xtok_revoke(lane_sh, shell) == 0);
    CHECK(xtok_release_owned(lsx, ex) == XTOK_REL_ESTALE);  /* revoked down the lineage */
    CHECK(xtok_release_owned(lane_sh, shell) == XTOK_REL_ESTALE);
    CHECK(owner_retire(ex) == 0);
    STEP("revoked");

    /* 8. The volume is the shell's: it maps and reads frames in order. */
    CHECK(xtok_map_acquire(vol_sh, shell, XTOK_RIGHT_READ, &p, &n) == XTOK_MAP_OK);
    CHECK(p == 0 && n == 8);                                  /* MEM never has one range */
    xtok_map_release(vol_sh);
    CHECK(xtok_frame_at(vol_sh, 7, &p) == 0 && p == fr[7]);

    /* 9. Teardown. Everything not permanent goes back. */
    CHECK(xtok_release_owned(b_sh, shell) == 0);
    for (i = 0; i < 3; i++) {
        CHECK(xep_destroy(shell, recv[i]) == 0);
        CHECK(xtok_release_owned(send[i], init) == 0);
    }
    CHECK(bal(b_init, init) == 4096);                         /* the child came back */
    CHECK(xtok_release_owned(b_init, init) == 0);
    CHECK(xtok_release_owned(r_init, init) == 0);
    CHECK(xtok_release_owned(vol_sh, shell) == XTOK_REL_ERIGHTS); /* MEM is permanent */
    STEP("teardown");
    CHECK(xtok_live_count() == base + 1);                     /* only the MEM volume */
    CHECK(owner_retire(init) == 0);
    CHECK(owner_retire(boot) == 0);
    CHECK(owner_retire(shell) == -1);                         /* the shell still holds the MEM */

    printf("exo-full: %d checks, %d failures, %d invariant violations\n",
           checks, failures, violations);
    printf("PENDING F3: the boot still mounts rd0 as a block device (exfs)\n");
    printf("PENDING F5: the boot is still the program that decides owners\n");
    return (failures || violations) ? 1 : 0;
}
