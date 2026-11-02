/* Host test for filesystem v2. A RAM disk counts and can fail any block write.
 * fsck checks that the free map matches the reachable blocks exactly after
 * each commit and each mount. The crash sweep cuts every write of a commit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "fsv2.h"

#define SS      512u
#define SECTORS (64u * 1024u * 1024u / SS)            /* 64 MiB = 16384 blocks */
static uint8_t disk[SECTORS * SS];
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

static uint8_t A[16u << 20], B[16u << 20];
static uint8_t reach[FS_MAX_BLOCKS / 8];

static void pattern(uint8_t *p, uint64_t n, uint32_t seed) {
    for (uint64_t i = 0; i < n; i++) p[i] = (uint8_t)((i * 131u + seed * 17u + (i >> 12)) & 0xFF);
}

static void reach_mark(uint64_t b, int *dup) {
    if (reach[b >> 3] & (1u << (b & 7))) *dup = 1;
    reach[b >> 3] |= (uint8_t)(1u << (b & 7));
}

/* fsck: free map == reachable blocks, exactly. No leak, no double use. */
static int fsck(void) {
    uint64_t total = fs_total_blocks();
    memset(reach, 0, sizeof reach);
    int dup = 0;
    for (uint64_t b = 0; b < FS_DATA_START; b++) reach_mark(b, &dup);   /* superblocks + both table areas */
    uint64_t used = FS_DATA_START;
    for (uint32_t i = 0; i < FS_INODES; i++) {
        const FsFile *f = fs_file(i);
        if (!f->used) continue;
        for (uint32_t k = 0; k < f->nchain; k++) { reach_mark(f->chain[k], &dup); used++; }
        for (uint32_t e = 0; e < f->n; e++) {
            for (uint32_t k = 0; k < f->ext[e].len; k++) { reach_mark(f->ext[e].start + k, &dup); used++; }
        }
    }
    if (dup) return 0;
    const uint8_t *bm = fs_bitmap();
    for (uint64_t b = 0; b < total; b++) {
        int in_map = (bm[b >> 3] >> (b & 7)) & 1;
        int reachable = (reach[b >> 3] >> (b & 7)) & 1;
        if (in_map != reachable) return 0;
    }
    return fs_free_blocks() == total - used;
}

static void fresh(void) {
    memset(disk, 0, sizeof disk);
    writes_seen = 0;
    fail_write_at = -1;
}

static int read_eq(const char *name, const uint8_t *want, uint64_t n) {
    static uint8_t got[16u << 20];
    int64_t r = fs_read(name, got, n);
    return r == (int64_t)n && memcmp(got, want, (size_t)n) == 0;
}

/* Leaves 90 free blocks as 30 holes of 3 blocks each, and nothing else free
 * before them. Each hole is separated by a file that stays in use. */
static void fragment_volume(void) {
    char name[32];
    for (int i = 0; i < 60; i++) {
        snprintf(name, sizeof name, "|bin|f%d", i);
        fs_create(name);
        fs_write(name, A, 3 * 4096);
    }
    fs_commit();
    for (int i = 1; i < 60; i += 2) {
        snprintf(name, sizeof name, "|bin|f%d", i);
        fs_delete(name);
    }
    fs_commit();
    const uint64_t holes = 30 * 3;
    for (int n = 0; fs_free_blocks() > holes; n++) {
        uint64_t want = fs_free_blocks() - holes;
        if (want > 2048) want = 2048;
        snprintf(name, sizeof name, "|boot|fill%d", n);
        fs_create(name);
        fs_write(name, A, want * 4096);
    }
    fs_commit();
}

int main(void) {
    /* 1. blank disk: not a volume; format makes one */
    fresh();
    CHECK(fs_mount(&dev) == FS_ENOFS, "blank disk: mount refuses");
    CHECK(fs_format(&dev) == FS_OK, "format");
    CHECK(fs_generation() == 1, "format commits generation 1");
    CHECK(fsck(), "fsck after format");

    /* 2. a 100 KiB file survives a remount, byte for byte */
    pattern(A, 100 * 1024, 1);
    CHECK(fs_create("|user|a") == FS_OK, "create |user|a");
    CHECK(fs_write("|user|a", A, 100 * 1024) == FS_OK, "write 100 KiB");
    CHECK(fs_commit() == FS_OK, "commit");
    CHECK(fsck(), "fsck after commit");
    CHECK(fs_mount(&dev) == FS_OK, "remount");
    CHECK(read_eq("|user|a", A, 100 * 1024), "100 KiB file reads back intact");
    CHECK(fsck(), "fsck after remount");

    /* 3. a 16 MiB file, contiguous */
    pattern(B, 16u << 20, 2);
    CHECK(fs_create("|boot|big") == FS_OK, "create |boot|big");
    CHECK(fs_write("|boot|big", B, 16u << 20) == FS_OK, "write 16 MiB");
    CHECK(fs_commit() == FS_OK, "commit 16 MiB");
    CHECK(fs_mount(&dev) == FS_OK, "remount after 16 MiB");
    CHECK(read_eq("|boot|big", B, 16u << 20), "16 MiB file reads back intact");
    CHECK(fsck(), "fsck with a large file");

    /* 4. fragmentation: a 40-block file must span holes, and so chain */
    fragment_volume();
    CHECK(fsck(), "fsck on the fragmented volume");
    pattern(B, 40 * 4096, 99);
    CHECK(fs_create("|bin|prog") == FS_OK, "create |bin|prog");
    CHECK(fs_write("|bin|prog", B, 40 * 4096) == FS_OK, "write 40 blocks into the holes");
    const FsFile *prog = fs_lookup("|bin|prog");
    CHECK(prog && prog->n > FS_INL, "the file needs more extents than fit in the inode");
    CHECK(fs_commit() == FS_OK, "commit fragmented file");
    CHECK(prog && prog->nchain >= 1, "the extent chain was written");
    CHECK(fsck(), "fsck with a chained file");
    CHECK(fs_mount(&dev) == FS_OK, "remount chained file");
    CHECK(read_eq("|bin|prog", B, 40 * 4096), "chained file reads back intact");
    CHECK(fsck(), "fsck after remount with chains");

    /* 5. superblock fallback: a torn newest superblock uses the previous one.
     * The checksum covers bytes 0..47, so the tear goes inside that range. */
    uint64_t gen_before = fs_generation();
    pattern(A, 4096, 77);
    CHECK(fs_create("|root|note") == FS_OK, "create |root|note");
    CHECK(fs_write("|root|note", A, 4096) == FS_OK, "write note");
    CHECK(fs_commit() == FS_OK, "commit note");
    uint64_t newest_slot = fs_generation() & 1;
    disk[newest_slot * FS_BLOCK + 20] ^= 0x5A;
    CHECK(fs_mount(&dev) == FS_OK, "mount with a torn newest superblock");
    CHECK(fs_generation() == gen_before, "the previous generation is used");
    CHECK(fs_lookup("|root|note") == NULL, "the uncommitted change is not visible");
    CHECK(fsck(), "fsck after superblock fallback");

    /* 6. crash sweep on a fragmented file: cut every write of its commit */
    fresh();
    CHECK(fs_format(&dev) == FS_OK, "format for the crash sweep");
    fragment_volume();
    pattern(A, 40 * 4096, 3);
    CHECK(fs_create("|user|r") == FS_OK, "create |user|r");
    CHECK(fs_write("|user|r", A, 40 * 4096) == FS_OK, "write old content");
    CHECK(fs_commit() == FS_OK, "commit old content");
    static uint8_t back[sizeof disk];
    memcpy(back, disk, sizeof disk);

    pattern(B, 40 * 4096, 4);
    CHECK(fs_write("|user|r", B, 40 * 4096) == FS_OK, "write new content");
    writes_seen = 0;
    CHECK(fs_commit() == FS_OK, "reference commit");
    int W = writes_seen;
    CHECK(W >= (int)FS_ITAB_BLOCKS + 2, "a fragmented commit writes its chain, table and superblock");
    CHECK(fs_mount(&dev) == FS_OK, "mount the new state");
    CHECK(read_eq("|user|r", B, 40 * 4096), "reference: the new content is committed");

    int sweep_ok = 1;
    for (int k = 0; k <= W; k++) {
        memcpy(disk, back, sizeof disk);
        if (fs_mount(&dev) != FS_OK) { sweep_ok = 0; break; }
        if (fs_write("|user|r", B, 40 * 4096) != FS_OK) { sweep_ok = 0; break; }
        writes_seen = 0;
        fail_write_at = k;
        int rc = fs_commit();
        fail_write_at = -1;
        if (k < W && rc == FS_OK) sweep_ok = 0;
        if (k == W && rc != FS_OK) sweep_ok = 0;

        if (fs_mount(&dev) != FS_OK) { sweep_ok = 0; break; }
        int is_old = read_eq("|user|r", A, 40 * 4096);
        int is_new = read_eq("|user|r", B, 40 * 4096);
        if (k < W && !is_old) sweep_ok = 0;
        if (k == W && !is_new) sweep_ok = 0;
        if (!is_old && !is_new) sweep_ok = 0;
        if (!fsck()) sweep_ok = 0;
    }
    CHECK(sweep_ok, "every cut point leaves the old or the new content, never a mix, and fsck passes");

    /* 7. delete releases blocks after the commit, not before */
    fresh();
    CHECK(fs_format(&dev) == FS_OK, "format for delete");
    pattern(A, 8 * 4096, 5);
    CHECK(fs_create("|user|d") == FS_OK, "create |user|d");
    CHECK(fs_write("|user|d", A, 8 * 4096) == FS_OK, "write 8 blocks");
    CHECK(fs_commit() == FS_OK, "commit");
    uint64_t free_before = fs_free_blocks();
    CHECK(fs_delete("|user|d") == FS_OK, "delete");
    CHECK(fs_free_blocks() == free_before, "blocks stay in use until the commit");
    CHECK(fs_commit() == FS_OK, "commit the delete");
    CHECK(fs_free_blocks() == free_before + 8, "exactly the 8 data blocks are free after the commit");
    CHECK(fsck(), "fsck after delete");

    /* 8. names: refused when too long or duplicated */
    char longname[FS_NAME_MAX + 2];
    memset(longname, 'x', sizeof longname - 1);
    longname[sizeof longname - 1] = '\0';
    CHECK(fs_create(longname) == FS_EINVAL, "a name over the limit is refused");
    CHECK(fs_create("|user|d") == FS_OK, "re-create after delete");
    CHECK(fs_create("|user|d") == FS_EEXIST, "a duplicate name is refused");

    printf("fs v2: %d passed, %d failed\n", passes, fails);
    return fails != 0;
}
