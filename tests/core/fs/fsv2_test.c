/* Host test for filesystem v2. A RAM disk counts and can fail any block write.
 * fsck checks that the free map matches the reachable blocks exactly after
 * each commit and each mount. The crash sweeps cut every write of a commit. */
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

    /* 9. byte-range I/O against a model: random writes and reads, commits and
     * remounts in between. The model is a plain RAM copy of the file. */
    static uint8_t model[2u << 20];
    uint64_t model_size = 0;
    uint32_t rs = 12345;
#define RNG() (rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5, rs)
    fresh();
    CHECK(fs_format(&dev) == FS_OK, "format for the byte-range model");
    CHECK(fs_create("|user|m") == FS_OK, "create |user|m");
    int model_ok = 1;
    for (int op = 0; op < 400 && model_ok; op++) {
        if (RNG() % 4 != 0) {
            uint64_t off = RNG() % (model_size + 8192);
            uint64_t len = 1 + RNG() % 20000;
            if (off + len > sizeof model) len = sizeof model - off;
            for (uint64_t i = 0; i < len; i++) A[i] = (uint8_t)RNG();
            if (fs_write_at("|user|m", off, A, len) != FS_OK) model_ok = 0;
            memcpy(model + off, A, (size_t)len);
            if (off + len > model_size) model_size = off + len;
        } else {
            uint64_t off = RNG() % (model_size + 4096);
            uint64_t cap = 1 + RNG() % 30000;
            int64_t n = fs_read_at("|user|m", off, B, cap);
            uint64_t want = off >= model_size ? 0 : (model_size - off < cap ? model_size - off : cap);
            if (n != (int64_t)want) model_ok = 0;
            else if (n > 0 && memcmp(B, model + off, (size_t)n) != 0) model_ok = 0;
        }
        if (op % 50 == 49) {
            if (fs_commit() != FS_OK || !fsck()) model_ok = 0;
            if (fs_mount(&dev) != FS_OK) model_ok = 0;
        }
    }
    CHECK(model_ok, "400 random byte-range operations agree with the model");
    CHECK(fs_commit() == FS_OK && fsck(), "commit and fsck after the model run");
    CHECK(fs_mount(&dev) == FS_OK, "remount after the model run");
    const FsFile *mf = fs_lookup("|user|m");
    CHECK(mf && mf->size == model_size, "file size matches the model after remount");
    CHECK(model_size > 0 && fs_read_at("|user|m", 0, B, model_size) == (int64_t)model_size &&
          memcmp(B, model, (size_t)model_size) == 0, "whole file matches the model after remount");

    /* 10. streaming: a 16 MiB file written and read in 4 KiB and 64 KiB pieces,
     * never holding more than one piece in memory */
#define GEN(i) ((uint8_t)(((i) * 131u + ((i) >> 12) * 7u + 3u) & 0xFF))
    fresh();
    CHECK(fs_format(&dev) == FS_OK, "format for streaming");
    CHECK(fs_create("|boot|stream") == FS_OK, "create |boot|stream");
    int stream_ok = 1;
    for (uint64_t off = 0; off < (16u << 20); off += 4096) {
        for (uint64_t j = 0; j < 4096; j++) A[j] = GEN(off + j);
        if (fs_write_at("|boot|stream", off, A, 4096) != FS_OK) stream_ok = 0;
    }
    CHECK(stream_ok, "write 16 MiB in 4 KiB pieces");
    CHECK(fs_commit() == FS_OK && fsck(), "commit the streamed file");
    CHECK(fs_mount(&dev) == FS_OK, "remount the streamed file");
    const FsFile *sf = fs_lookup("|boot|stream");
    CHECK(sf && sf->size == (16u << 20) && sf->n == 1, "16 MiB stored as one contiguous extent");
    stream_ok = 1;
    for (uint64_t off = 0; off < (16u << 20); off += 65536) {
        if (fs_read_at("|boot|stream", off, B, 65536) != 65536) { stream_ok = 0; break; }
        for (uint64_t j = 0; j < 65536; j++) if (B[j] != GEN(off + j)) stream_ok = 0;
    }
    CHECK(stream_ok, "read 16 MiB back in 64 KiB pieces, byte for byte");

    /* 11. a write that cannot fit changes nothing */
    CHECK(fs_write_at("|boot|stream", 1ull << 30, A, 1) == FS_ENOSPC,
          "a write 1 GiB past the end on a 64 MiB disk is refused");
    CHECK(fs_lookup("|boot|stream")->size == (16u << 20), "the size is unchanged after the refusal");
    CHECK(fs_commit() == FS_OK && fsck(), "nothing to commit, fsck still exact");

    /* 12. crash sweep for a partial rewrite in the middle of a fragmented file */
    fresh();
    CHECK(fs_format(&dev) == FS_OK, "format for the partial-write sweep");
    fragment_volume();
    pattern(A, 40 * 4096, 3);
    CHECK(fs_create("|user|r") == FS_OK, "create |user|r");
    CHECK(fs_write("|user|r", A, 40 * 4096) == FS_OK, "write old content");
    CHECK(fs_commit() == FS_OK, "commit old content");
    static uint8_t back2[sizeof disk];
    memcpy(back2, disk, sizeof disk);

    static uint8_t patch[100000];
    pattern(patch, sizeof patch, 8);
    memcpy(B, A, 40 * 4096);
    memcpy(B + 5000, patch, sizeof patch);           /* the expected new content */

    memcpy(disk, back2, sizeof disk);
    CHECK(fs_mount(&dev) == FS_OK, "sweep: mount the old state");
    CHECK(fs_write_at("|user|r", 5000, patch, sizeof patch) == FS_OK, "sweep: partial rewrite");
    writes_seen = 0;
    CHECK(fs_commit() == FS_OK, "sweep: reference commit");
    int W2 = writes_seen;
    CHECK(fs_mount(&dev) == FS_OK && read_eq("|user|r", B, 40 * 4096), "sweep: reference content is the patched file");

    int psweep = 1;
    for (int k = 0; k <= W2; k++) {
        memcpy(disk, back2, sizeof disk);
        if (fs_mount(&dev) != FS_OK) { psweep = 0; break; }
        if (fs_write_at("|user|r", 5000, patch, sizeof patch) != FS_OK) { psweep = 0; break; }
        writes_seen = 0;
        fail_write_at = k;
        int rc = fs_commit();
        fail_write_at = -1;
        if (k < W2 && rc == FS_OK) psweep = 0;
        if (k == W2 && rc != FS_OK) psweep = 0;
        if (fs_mount(&dev) != FS_OK) { psweep = 0; break; }
        int is_old = read_eq("|user|r", A, 40 * 4096);
        int is_new = read_eq("|user|r", B, 40 * 4096);
        if (k < W2 && !is_old) psweep = 0;
        if (k == W2 && !is_new) psweep = 0;
        if (!is_old && !is_new) psweep = 0;
        if (!fsck()) psweep = 0;
    }
    CHECK(psweep, "every cut of a partial rewrite leaves the old or the new content, and fsck passes");

    printf("fs v2: %d passed, %d failed\n", passes, fails);
    return fails != 0;
}
