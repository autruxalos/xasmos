/* Host test for the group map. Each group is 512 blocks, so the 64 MiB disk
 * has 32 groups and the 8-group cache is always under pressure. A RAM model
 * of the bitmap is the reference. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "gmap.h"

#define SS      512u
#define SECTORS (64u * 1024u * 1024u / SS)          /* 64 MiB = 16384 blocks */
#define TOTAL   (SECTORS * SS / EXFS_BLOCK)
#define GB      512u
#define NG      (TOTAL / GB)

static uint8_t disk[SECTORS * SS];
static uint8_t disk_backup[sizeof disk];
static int     writes_seen;
static int     fail_write_at = -1;

static int d_read(void *c, uint64_t lba, uint32_t n, void *buf) {
    (void)c;
    if (lba + n > SECTORS) return BLK_E_RANGE;
    memcpy(buf, disk + lba * SS, (size_t)n * SS);
    return BLK_OK;
}
static int d_write(void *c, uint64_t lba, uint32_t n, const void *buf) {
    (void)c;
    if (lba + n > SECTORS) return BLK_E_RANGE;
    int k = writes_seen++;
    if (k == fail_write_at) return BLK_E_IO;
    memcpy(disk + lba * SS, buf, (size_t)n * SS);
    return BLK_OK;
}
static int d_flush(void *c) { (void)c; return BLK_OK; }
static const BlkOps d_ops = { d_read, d_write, d_flush };
static BlkDev dev = { 0, &d_ops, SS, SECTORS };

static int passes, fails;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

static uint8_t model[TOTAL];

static void model_format(void) {
    memset(model, 0, sizeof model);
    for (uint32_t i = 0; i < EXFS_DATA_START + 2; i++) model[i] = 1;
    for (uint32_t g = 1; g < NG; g++) { model[g * GB] = 1; model[g * GB + 1] = 1; }
}

static uint64_t model_free(void) {
    uint64_t n = 0;
    for (uint64_t b = 0; b < TOTAL; b++) if (!model[b]) n++;
    return n;
}

static int matches(GMap *m) {
    for (uint64_t b = 0; b < TOTAL; b++) {
        int u = gmap_is_used(m, b);
        if (u < 0 || u != model[b]) return 0;
    }
    return gmap_free_total(m) == model_free();
}

/* A committed snapshot: the superblock's slot and free tables, plus the disk. */
static uint8_t  snap_slot[NG];
static uint32_t snap_free[NG];
static void snapshot(GMap *m) {
    gmap_export(m, snap_slot, snap_free);
}

/* The filesystem's commit sequence, minus the superblock write itself. */
static int commit(GMap *m) {
    if (gmap_flush(m) != GMAP_OK) return 0;
    snapshot(m);
    gmap_commit(m);
    return 1;
}

/* Extents currently held by the model, so releases always target used blocks. */
#define MAXHELD 256
static uint64_t held_s[MAXHELD], held_l[MAXHELD];
static int nheld;

static uint32_t rs = 2463534242u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

/* The transaction: 30 operations over the held extents and fresh takes.
 * Returns 0 if every operation succeeded, 1 if one failed (a cut). */
static int run_tx(GMap *mm, uint8_t *mdl, const uint64_t *hs0, const uint64_t *hl0, int nh0) {
    uint64_t hs[MAXHELD], hl[MAXHELD];
    int nh = nh0;
    for (int i = 0; i < nh0; i++) { hs[i] = hs0[i]; hl[i] = hl0[i]; }
    uint32_t r = 777;
    for (int i = 0; i < 30; i++) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        if (i % 3 == 2 && nh > 0) {
            int k = (int)(r % (uint32_t)nh);
            if (gmap_release(mm, hs[k], hl[k]) != GMAP_OK) return 1;
            for (uint64_t b = hs[k]; b < hs[k] + hl[k]; b++) mdl[b] = 0;
            hs[k] = hs[nh - 1];
            hl[k] = hl[nh - 1];
            nh--;
        } else {
            uint64_t s = 0, l = 0;
            if (gmap_take(mm, (uint64_t)i * 700, 1 + r % 40, &s, &l) != GMAP_OK) return 1;
            for (uint64_t b = s; b < s + l; b++) mdl[b] = 1;
        }
    }
    return 0;
}

int main(void) {
    GMap m;
    CHECK(gmap_init(&m, &dev, TOTAL, GB) == GMAP_OK, "init the group map");
    CHECK(gmap_init(&m, &dev, TOTAL, 100) != GMAP_OK, "a group size that is not a power of two is refused");
    CHECK(gmap_init(&m, &dev, TOTAL, GB) == GMAP_OK, "rebind for the test");

    /* 1. format: reserved blocks taken, free count right, reads back exactly */
    memset(disk, 0, sizeof disk);
    CHECK(gmap_format(&m) == GMAP_OK, "format the map");
    model_format();
    CHECK(matches(&m), "a fresh map matches the model");
    commit(&m);

    /* 2. random allocation and release, committed every 50 operations */
    int ok = 1;
    nheld = 0;
    for (int op = 0; op < 2000 && ok; op++) {
        if (rnd() % 10 < 6 || nheld == 0) {
            uint64_t hint = rnd() % TOTAL;
            uint64_t need = 1 + rnd() % 64, s = 0, l = 0;
            int rc = gmap_take(&m, hint, need, &s, &l);
            if (rc == GMAP_ENOSPC) continue;
            if (rc != GMAP_OK || l == 0 || l > need) { ok = 0; break; }
            for (uint64_t b = s; b < s + l; b++) {
                if (model[b]) { ok = 0; break; }
                model[b] = 1;
            }
            if (nheld < MAXHELD) { held_s[nheld] = s; held_l[nheld] = l; nheld++; }
        } else {
            uint32_t k = rnd() % (uint32_t)nheld;
            if (gmap_release(&m, held_s[k], held_l[k]) != GMAP_OK) { ok = 0; break; }
            for (uint64_t b = held_s[k]; b < held_s[k] + held_l[k]; b++) model[b] = 0;
            held_s[k] = held_s[nheld - 1];
            held_l[k] = held_l[nheld - 1];
            nheld--;
        }
        if (op % 50 == 49) {
            if (!commit(&m) || !matches(&m)) ok = 0;
        }
    }
    CHECK(ok, "2000 random take and release operations agree with the model");
    CHECK(commit(&m) && matches(&m), "the last commit agrees with the model");

    /* 3. persistence: a new map adopts the committed tables and reads the disk */
    GMap m2;
    gmap_init(&m2, &dev, TOTAL, GB);
    gmap_import(&m2, snap_slot, snap_free);
    CHECK(matches(&m2), "a remounted map reads back the committed state");

    /* 4. double release is refused and changes nothing */
    uint64_t fb = gmap_free_total(&m2);
    gmap_import(&m2, snap_slot, snap_free);
    int freed_block = -1;
    for (uint64_t b = 0; b < TOTAL; b++) if (!model[b]) { freed_block = (int)b; break; }
    CHECK(freed_block >= 0 && gmap_release(&m2, (uint64_t)freed_block, 1) == GMAP_EUSED,
          "releasing a free block is refused");
    CHECK(gmap_free_total(&m2) == fb, "the refused release changed nothing");

    /* 5. crash sweep: every write of a transaction that touches several groups,
     * including the evictions the cache makes on the way. */
    memcpy(disk_backup, disk, sizeof disk);
    uint8_t  old_slot[NG];  uint32_t old_free[NG];
    memcpy(old_slot, snap_slot, sizeof old_slot);
    memcpy(old_free, snap_free, sizeof old_free);
    uint8_t  model_old[TOTAL];
    memcpy(model_old, model, sizeof model);

    /* the reference run: no failures, then flush */
    GMap ref;
    gmap_init(&ref, &dev, TOTAL, GB);
    gmap_import(&ref, old_slot, old_free);
    uint8_t model_new[TOTAL];
    memcpy(model_new, model_old, sizeof model_new);
    writes_seen = 0; fail_write_at = -1;
    CHECK(run_tx(&ref, model_new, held_s, held_l, nheld) == 0, "reference transaction runs");
    CHECK(gmap_flush(&ref) == GMAP_OK, "reference flush");
    int W = writes_seen;
    CHECK(W > 0, "the transaction writes the map");

    int sweep_ok = 1;
    for (int k = 0; k < W; k++) {
        memcpy(disk, disk_backup, sizeof disk);
        GMap t;
        gmap_init(&t, &dev, TOTAL, GB);
        gmap_import(&t, old_slot, old_free);
        uint8_t mt[TOTAL];
        memcpy(mt, model_old, sizeof mt);
        writes_seen = 0;
        fail_write_at = k;
        int failed = run_tx(&t, mt, held_s, held_l, nheld);
        if (!failed) failed = gmap_flush(&t) != GMAP_OK;
        fail_write_at = -1;
        if (!failed || writes_seen <= k) sweep_ok = 0;   /* the cut must have happened */
        /* crash: no superblock was written, so the committed tables are the old ones */
        GMap after;
        gmap_init(&after, &dev, TOTAL, GB);
        gmap_import(&after, old_slot, old_free);
        for (uint64_t b = 0; b < TOTAL && sweep_ok; b++) {
            int u = gmap_is_used(&after, b);
            if (u < 0 || u != model_old[b]) sweep_ok = 0;
        }
    }
    CHECK(sweep_ok, "a cut at every write of the transaction leaves the old committed state");

    /* the complete transaction, committed: the new state is what comes back */
    memcpy(disk, disk_backup, sizeof disk);
    GMap full;
    gmap_init(&full, &dev, TOTAL, GB);
    gmap_import(&full, old_slot, old_free);
    uint8_t mf[TOTAL];
    memcpy(mf, model_old, sizeof mf);
    writes_seen = 0; fail_write_at = -1;
    CHECK(run_tx(&full, mf, held_s, held_l, nheld) == 0, "the complete transaction runs");
    CHECK(commit(&full), "the complete transaction commits");
    GMap after_commit;
    gmap_init(&after_commit, &dev, TOTAL, GB);
    gmap_import(&after_commit, snap_slot, snap_free);
    int new_ok = 1;
    for (uint64_t b = 0; b < TOTAL; b++) {
        int u = gmap_is_used(&after_commit, b);
        if (u < 0 || u != mf[b]) { new_ok = 0; break; }
    }
    CHECK(new_ok, "after the commit, the new state is what a remount reads");

    printf("gmap: %d passed, %d failed\n", passes, fails);
    return fails != 0;
}
