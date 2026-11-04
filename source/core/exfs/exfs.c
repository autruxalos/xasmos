/******************************************************************************
 * XASMOS - EXFS implementation
 * Path: source/core/exfs/exfs.c
 *
 * Free space is the group map (gmap). Superblocks are format v3 (sb). The
 * commit order is fixed; see the comment on exfs_commit below.
 ******************************************************************************/
#include "exfs.h"
#include "gmap.h"
#include "sb.h"

#define INODE_BYTES 256
#define POOL_MIN_CAP 8u
#define FEXT(f) (&V.pool[(f)->ext_off])

static struct {
    BlkDev     *dev;
    uint32_t    spb;                            /* sectors per block          */
    uint64_t    total;                          /* blocks on the volume       */
    uint64_t    gen;                            /* committed generation       */
    uint64_t    itab_start;                     /* committed inode table area */
    int         mounted;
    int         broken;
    int         dirty;                          /* something to commit        */
    GMap        gm;                             /* free space, on disk        */
    uint64_t    hint;                           /* where the next search starts */
    ExfsFile    files[EXFS_INODES];
    ExfsExtent  pool[EXFS_POOL_EXT];            /* extent lists of all files  */
    uint8_t     pool_used[EXFS_POOL_EXT / 8];
    ExfsExtent  defer[EXFS_DEFER_MAX];          /* freed by the next commit   */
    uint32_t    ndefer;
    ExfsExtent  fresh[EXFS_MAX_EXT];            /* extents of the current write */
    ExfsExtent  stage[EXFS_MAX_EXT];            /* new extent list being built  */
    uint32_t    nstage;
    uint8_t     page[EXFS_BLOCK];
    uint8_t     itab[EXFS_ITAB_BLOCKS * EXFS_BLOCK];
} V;

/* ---- little-endian fields ----------------------------------------------- */

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t get64(const uint8_t *p) {
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

/* The inode table area used by a generation: even -> area 0, odd -> area 1. */
static uint64_t itab_area(uint64_t gen) {
    return (gen & 1) ? EXFS_ITAB_AREA1 : EXFS_ITAB_AREA0;
}

/* ---- block I/O ---------------------------------------------------------- */

static int blk_put(uint64_t b, const uint8_t *buf) {
    return blk_write(V.dev, b * V.spb, V.spb, buf) == BLK_OK ? EXFS_OK : EXFS_EIO;
}

static int blk_got(uint64_t b, uint8_t *buf) {
    return blk_read(V.dev, b * V.spb, V.spb, buf) == BLK_OK ? EXFS_OK : EXFS_EIO;
}

static int geometry(BlkDev *d) {
    uint32_t ss = blk_sector_size(d);
    if (ss == 0 || EXFS_BLOCK % ss != 0) return EXFS_ENOFS;
    V.dev = d;
    V.spb = EXFS_BLOCK / ss;
    V.total = (blk_sector_count(d) * ss) / EXFS_BLOCK;
    if (V.total < EXFS_DATA_START + 16 || V.total > EXFS_MAX_BLOCKS) return EXFS_ENOFS;
    return EXFS_OK;
}

/* ---- free space (the group map) ----------------------------------------- */

/* Takes up to need contiguous free blocks, from the hint onward. Returns the
 * length taken, 0 if there is no free space. */
static uint32_t take_extent(uint64_t need, ExfsExtent *out) {
    uint64_t start = 0, len = 0;
    if (gmap_take(&V.gm, V.hint, need, &start, &len) != GMAP_OK) return 0;
    out->start = start;
    out->len = (uint32_t)len;
    V.hint = start + len;
    return (uint32_t)len;
}

/* Gives back blocks taken during the current write, which nothing refers to. */
static void release_fresh(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        gmap_release(&V.gm, V.fresh[i].start, V.fresh[i].len);
    }
}

/* ---- extent pool --------------------------------------------------------
 * Every file's extent list lives in one shared pool. A file owns a slice
 * (ext_off, ext_cap) of it. Capacities are powers of two, at least 8. */

static int pool_is_used(uint32_t i) {
    return (V.pool_used[i >> 3] >> (i & 7)) & 1;
}

static void pool_mark(uint32_t off, uint32_t cap, int on) {
    for (uint32_t i = 0; i < cap; i++) {
        uint32_t s = off + i;
        if (on) V.pool_used[s >> 3] |= (uint8_t)(1u << (s & 7));
        else    V.pool_used[s >> 3] &= (uint8_t)~(1u << (s & 7));
    }
}

/* First free run of cap slots. Returns 1 and its offset, or 0. */
static int pool_find(uint32_t cap, uint32_t *off) {
    uint32_t run = 0;
    for (uint32_t i = 0; i < EXFS_POOL_EXT; i++) {
        if (pool_is_used(i)) { run = 0; continue; }
        if (++run == cap) { *off = i + 1 - cap; return 1; }
    }
    return 0;
}

/* Packs every slice to the front, keeping the files in offset order. Slices
 * only move down, so each forward copy is safe. */
static void pool_compact(void) {
    uint32_t order[EXFS_INODES], cnt = 0;
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        if (V.files[i].used && V.files[i].ext_cap > 0) order[cnt++] = i;
    }
    for (uint32_t a = 1; a < cnt; a++) {
        uint32_t x = order[a], b = a;
        while (b > 0 && V.files[order[b - 1]].ext_off > V.files[x].ext_off) {
            order[b] = order[b - 1];
            b--;
        }
        order[b] = x;
    }
    for (uint32_t i = 0; i < EXFS_POOL_EXT / 8; i++) V.pool_used[i] = 0;
    uint32_t cursor = 0;
    for (uint32_t a = 0; a < cnt; a++) {
        ExfsFile *f = &V.files[order[a]];
        if (f->ext_off != cursor) {
            for (uint32_t k = 0; k < f->n; k++) V.pool[cursor + k] = V.pool[f->ext_off + k];
            f->ext_off = cursor;
        }
        cursor += f->ext_cap;
        pool_mark(f->ext_off, f->ext_cap, 1);
    }
}

static uint32_t slice_cap_for(uint32_t n) {
    uint32_t c = POOL_MIN_CAP;
    while (c < n) c <<= 1;
    return c;
}

/* Reserves a slice for n extents. Compacts once if the pool is fragmented. */
static int pool_reserve(uint32_t n, uint32_t *off, uint32_t *cap) {
    if (n == 0) { *off = 0; *cap = 0; return EXFS_OK; }
    *cap = slice_cap_for(n);
    if (!pool_find(*cap, off)) {
        pool_compact();
        if (!pool_find(*cap, off)) return EXFS_ENOMEM;
    }
    pool_mark(*off, *cap, 1);
    return EXFS_OK;
}

static void pool_release(uint32_t off, uint32_t cap) {
    pool_mark(off, cap, 0);
}

/* ---- inode table -------------------------------------------------------- */
/* Record layout (256 bytes):
 *   0 used, 4 type, 8 size, 16 name[96], 112 first chain block,
 *   120 inline extents, 128 inline extent array (8 x 16 bytes)            */

static void inode_encode(uint8_t *r, const ExfsFile *f) {
    for (uint32_t i = 0; i < INODE_BYTES; i++) r[i] = 0;
    put32(r + 0, f->used ? 1u : 0u);
    put32(r + 4, f->type);
    put64(r + 8, f->size);
    for (uint32_t i = 0; i <= EXFS_NAME_MAX; i++) r[16 + i] = (uint8_t)f->name[i];
    put64(r + 112, f->nchain ? f->chain[0] : 0);
    uint32_t ninl = f->n < EXFS_INL ? f->n : EXFS_INL;
    put32(r + 120, ninl);
    for (uint32_t i = 0; i < ninl; i++) {
        put64(r + 128 + i * 16, FEXT(f)[i].start);
        put32(r + 128 + i * 16 + 8, FEXT(f)[i].len);
    }
}

/* Decodes a record. Its extents go to V.stage; mount moves them into the pool. */
static int inode_decode(const uint8_t *r, ExfsFile *f) {
    f->used = get32(r + 0) != 0;
    f->type = get32(r + 4);
    f->size = get64(r + 8);
    for (uint32_t i = 0; i <= EXFS_NAME_MAX; i++) f->name[i] = (char)r[16 + i];
    f->name[EXFS_NAME_MAX] = '\0';
    f->dirty = 0;
    f->nchain = 0;
    f->n = 0;
    f->ext_off = 0;
    f->ext_cap = 0;
    if (!f->used) return EXFS_OK;

    uint32_t ninl = get32(r + 120);
    if (ninl > EXFS_INL) return EXFS_ENOFS;
    for (uint32_t i = 0; i < ninl; i++) {
        V.stage[f->n].start = get64(r + 128 + i * 16);
        V.stage[f->n].len = get32(r + 128 + i * 16 + 8);
        f->n++;
    }
    uint64_t blk = get64(r + 112);
    while (blk != 0) {
        if (f->nchain >= EXFS_MAX_CHAIN) return EXFS_ENOFS;
        if (blk_got(blk, V.page) != EXFS_OK) return EXFS_EIO;
        uint64_t next = get64(V.page);
        uint32_t cnt = get32(V.page + 8);
        if (cnt > EXFS_IND_EXT || f->n + cnt > EXFS_MAX_EXT) return EXFS_ENOFS;
        for (uint32_t j = 0; j < cnt; j++) {
            V.stage[f->n].start = get64(V.page + 16 + j * 16);
            V.stage[f->n].len = get32(V.page + 16 + j * 16 + 8);
            f->n++;
        }
        f->chain[f->nchain++] = blk;
        blk = next;
    }
    return EXFS_OK;
}

/* ---- deferred release --------------------------------------------------- */

static int defer_push(uint64_t start, uint32_t len) {
    if (V.ndefer >= EXFS_DEFER_MAX) return EXFS_ENOMEM;
    V.defer[V.ndefer].start = start;
    V.defer[V.ndefer].len = len;
    V.ndefer++;
    return EXFS_OK;
}

/* Hands a file's blocks (extents and chain) to the deferred list, and frees
 * its slice of the pool. The blocks stay in use until the commit. */
static int defer_file(ExfsFile *f) {
    uint32_t need = f->n + f->nchain;
    if (V.ndefer + need > EXFS_DEFER_MAX) return EXFS_ENOMEM;
    for (uint32_t i = 0; i < f->n; i++) defer_push(FEXT(f)[i].start, FEXT(f)[i].len);
    for (uint32_t i = 0; i < f->nchain; i++) defer_push(f->chain[i], 1);
    pool_release(f->ext_off, f->ext_cap);
    f->ext_off = 0;
    f->ext_cap = 0;
    f->n = 0;
    f->nchain = 0;
    return EXFS_OK;
}

/* Step 2 of the commit: the deferred blocks become free in the map. */
static int apply_deferred(void) {
    for (uint32_t i = 0; i < V.ndefer; i++) {
        if (gmap_release(&V.gm, V.defer[i].start, V.defer[i].len) != GMAP_OK) return EXFS_EIO;
    }
    V.ndefer = 0;
    return EXFS_OK;
}

/* ---- lookup ------------------------------------------------------------- */

static ExfsFile *find(const char *name) {
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        if (!V.files[i].used) continue;
        const char *a = V.files[i].name, *b = name;
        while (*a != '\0' && *a == *b) { a++; b++; }
        if (*a == *b) return &V.files[i];
    }
    return 0;
}

static int name_ok(const char *name) {
    if (name[0] != '|') return 0;
    uint32_t n = 0;
    while (name[n] != '\0') n++;
    if (n < 2 || n > EXFS_NAME_MAX) return 0;
    if (name[n - 1] == '|') return 0;
    for (uint32_t i = 1; i < n; i++) {
        if (name[i] == '|' && name[i - 1] == '|') return 0;
    }
    return 1;
}

static int is_root(const char *name) {
    return name[0] == '|' && name[1] == '\0';
}

/* The parent of "|a|b|c" is "|a|b"; the parent of "|a" is the root, which
 * always exists. Any other parent must be a directory. */
static int parent_ok(const char *name) {
    int last = -1;
    for (int i = 0; name[i] != '\0'; i++) {
        if (name[i] == '|') last = i;
    }
    if (last == 0) return 1;
    char par[EXFS_NAME_MAX + 1];
    for (int i = 0; i < last; i++) par[i] = name[i];
    par[last] = '\0';
    const ExfsFile *p = find(par);
    return p != 0 && p->type == EXFS_TYPE_DIR;
}

/* Takes a free inode slot for a new node of the given type. */
static int make_node(const char *name, uint32_t type) {
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        ExfsFile *f = &V.files[i];
        if (f->used) continue;
        f->used = 1;
        f->type = type;
        f->size = 0;
        f->n = 0;
        f->ext_off = 0;
        f->ext_cap = 0;
        f->nchain = 0;
        f->dirty = 1;
        uint32_t k = 0;
        while (name[k] != '\0') { f->name[k] = name[k]; k++; }
        f->name[k] = '\0';
        V.dirty = 1;
        return EXFS_OK;
    }
    return EXFS_ENOSPC;
}

static int has_children(const char *dir) {
    uint32_t dl = 0;
    while (dir[dl] != '\0') dl++;
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        const ExfsFile *g = &V.files[i];
        if (!g->used || g->name[dl] != '|') continue;
        uint32_t k = 0;
        while (k < dl && g->name[k] == dir[k]) k++;
        if (k == dl) return 1;
    }
    return 0;
}

/* ---- commit ------------------------------------------------------------- */

static void mark_broken(void) {
    V.broken = 1;
}

/* The commit. Every step that can fail before the superblock is checked here
 * or marks the volume broken; the superblock is the only commit point. */
int exfs_commit(void) {
    if (!V.mounted) return EXFS_ENOFS;
    if (V.broken) return EXFS_EBROKEN;
    if (!V.dirty) return EXFS_OK;

    /* 0. validation: nothing has changed yet */
    uint64_t need = 0;
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        const ExfsFile *f = &V.files[i];
        if (!f->used || !f->dirty || f->n <= EXFS_INL) continue;
        uint32_t nind = (f->n - EXFS_INL + EXFS_IND_EXT - 1) / EXFS_IND_EXT;
        if (nind > EXFS_MAX_CHAIN) return EXFS_ENOMEM;
        need += nind;
    }
    if (need > gmap_free_total(&V.gm)) return EXFS_ENOSPC;

    /* 1. chain blocks for changed files: from free blocks only, so nothing
     * freed by this transaction is handed out here */
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        ExfsFile *f = &V.files[i];
        if (!f->used || !f->dirty) continue;

        uint32_t nind = f->n > EXFS_INL ? (f->n - EXFS_INL + EXFS_IND_EXT - 1) / EXFS_IND_EXT : 0;
        for (uint32_t k = 0; k < nind; k++) {
            ExfsExtent e;
            if (take_extent(1, &e) != 1) { mark_broken(); return EXFS_ENOSPC; }
            f->chain[k] = e.start;
        }
        f->nchain = nind;

        for (uint32_t k = 0; k < nind; k++) {
            uint32_t first = EXFS_INL + k * EXFS_IND_EXT;
            uint32_t cnt = f->n - first;
            if (cnt > EXFS_IND_EXT) cnt = EXFS_IND_EXT;
            for (uint32_t j = 0; j < EXFS_BLOCK; j++) V.page[j] = 0;
            put64(V.page, k + 1 < nind ? f->chain[k + 1] : 0);
            put32(V.page + 8, cnt);
            for (uint32_t j = 0; j < cnt; j++) {
                put64(V.page + 16 + j * 16, FEXT(f)[first + j].start);
                put32(V.page + 16 + j * 16 + 8, FEXT(f)[first + j].len);
            }
            if (blk_put(f->chain[k], V.page) != EXFS_OK) { mark_broken(); return EXFS_EIO; }
        }
    }

    /* 2. the blocks freed by this transaction become free in the map */
    if (apply_deferred() != EXFS_OK) { mark_broken(); return EXFS_EIO; }

    /* 3. dirty map groups go to their pending slots */
    if (gmap_flush(&V.gm) != GMAP_OK) { mark_broken(); return EXFS_EIO; }

    /* 4. the inode table, into this generation's area */
    uint64_t gen = V.gen + 1;
    uint64_t area = itab_area(gen);
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        inode_encode(V.itab + i * INODE_BYTES, &V.files[i]);
    }
    for (uint32_t b = 0; b < EXFS_ITAB_BLOCKS; b++) {
        if (blk_put(area + b, V.itab + b * EXFS_BLOCK) != EXFS_OK) { mark_broken(); return EXFS_EIO; }
    }

    /* 5. the commit point: superblock of the new generation */
    SbFields f;
    for (uint32_t g = 0; g < SB_MAX_GROUPS; g++) { f.slot[g] = 0; f.free[g] = 0; }
    f.gen = gen;
    f.itab = area;
    f.nitab = EXFS_ITAB_BLOCKS;
    f.ninodes = EXFS_INODES;
    f.total = V.total;
    f.ngroups = V.gm.ngroups;
    gmap_export(&V.gm, f.slot, f.free);
    if (sb_encode(V.page, &f) != SB_OK) { mark_broken(); return EXFS_EIO; }
    if (blk_put(gen & 1, V.page) != EXFS_OK) { mark_broken(); return EXFS_EIO; }

    /* 6. durable: pending map slots become committed */
    gmap_commit(&V.gm);
    V.gen = gen;
    V.itab_start = area;
    for (uint32_t i = 0; i < EXFS_INODES; i++) V.files[i].dirty = 0;
    V.dirty = 0;
    return EXFS_OK;
}

/* ---- volume life cycle -------------------------------------------------- */

int exfs_format(BlkDev *d) {
    if (!d || geometry(d) != EXFS_OK) return EXFS_ENOFS;
    if (gmap_init(&V.gm, d, V.total, GMAP_GROUP_MAX) != GMAP_OK) return EXFS_ENOFS;
    if (gmap_format(&V.gm) != GMAP_OK) return EXFS_EIO;

    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        V.files[i].used = 0;
        V.files[i].ext_off = 0;
        V.files[i].ext_cap = 0;
    }
    for (uint32_t i = 0; i < EXFS_POOL_EXT / 8; i++) V.pool_used[i] = 0;
    V.ndefer = 0;
    V.gen = 0;
    V.itab_start = 0;
    V.hint = EXFS_DATA_START;
    V.broken = 0;
    V.mounted = 1;
    V.dirty = 1;
    (void)make_node("|user", EXFS_TYPE_DIR);
    (void)make_node("|root", EXFS_TYPE_DIR);
    (void)make_node("|bin",  EXFS_TYPE_DIR);
    (void)make_node("|boot", EXFS_TYPE_DIR);
    return exfs_commit();
}

/* Reads the newest valid superblock, adopts its map tables and inode table,
 * and checks every extent against the volume. Nothing is written. */
int exfs_mount(BlkDev *d) {
    V.mounted = 0;
    if (!d || geometry(d) != EXFS_OK) return EXFS_ENOFS;
    if (gmap_init(&V.gm, d, V.total, GMAP_GROUP_MAX) != GMAP_OK) return EXFS_ENOFS;

    SbFields best;
    int have = 0;
    for (uint64_t s = 0; s < 2; s++) {
        if (blk_got(s, V.page) != EXFS_OK) continue;
        SbFields f;
        if (sb_decode(V.page, &f) != SB_OK) continue;
        if (!have || f.gen > best.gen) { best = f; have = 1; }
    }
    if (!have) return EXFS_ENOFS;
    if (best.total != V.total || best.ngroups != V.gm.ngroups) return EXFS_ENOFS;

    for (uint32_t b = 0; b < EXFS_ITAB_BLOCKS; b++) {
        if (blk_got(best.itab + b, V.itab + b * EXFS_BLOCK) != EXFS_OK) return EXFS_EIO;
    }
    gmap_import(&V.gm, best.slot, best.free);

    for (uint32_t i = 0; i < EXFS_POOL_EXT / 8; i++) V.pool_used[i] = 0;
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        V.files[i].ext_off = 0;
        V.files[i].ext_cap = 0;
    }
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        if (inode_decode(V.itab + i * INODE_BYTES, &V.files[i]) != EXFS_OK) return EXFS_ENOFS;
        ExfsFile *f = &V.files[i];
        if (!f->used) continue;
        uint32_t roff = 0, rcap = 0;
        if (pool_reserve(f->n, &roff, &rcap) != EXFS_OK) return EXFS_ENOMEM;
        for (uint32_t k = 0; k < f->n; k++) V.pool[roff + k] = V.stage[k];
        f->ext_off = roff;
        f->ext_cap = rcap;
        for (uint32_t k = 0; k < f->nchain; k++) {
            if (f->chain[k] < EXFS_DATA_START || f->chain[k] >= V.total) return EXFS_ENOFS;
        }
        for (uint32_t k = 0; k < f->n; k++) {
            if (FEXT(f)[k].len == 0) return EXFS_ENOFS;
            if (FEXT(f)[k].start < EXFS_DATA_START) return EXFS_ENOFS;
            if (FEXT(f)[k].start + FEXT(f)[k].len > V.total) return EXFS_ENOFS;
        }
    }

    V.gen = best.gen;
    V.itab_start = best.itab;
    V.ndefer = 0;
    V.hint = EXFS_DATA_START;
    V.broken = 0;
    V.dirty = 0;
    V.mounted = 1;
    return EXFS_OK;
}

int exfs_unmount(void) {
    if (!V.mounted) return EXFS_ENOFS;
    int rc = exfs_commit();
    if (rc != EXFS_OK) return rc;
    V.mounted = 0;
    return EXFS_OK;
}

int exfs_is_mounted(void) {
    return V.mounted && !V.broken;
}

int exfs_is_used(uint64_t block) {
    if (!V.mounted) return EXFS_ENOFS;
    return gmap_is_used(&V.gm, block);
}

/* ---- names -------------------------------------------------------------- */

int exfs_create(const char *name) {
    if (!V.mounted) return EXFS_ENOFS;
    if (V.broken) return EXFS_EBROKEN;
    if (!name_ok(name)) return EXFS_EINVAL;
    if (find(name)) return EXFS_EEXIST;
    if (!parent_ok(name)) return EXFS_ENOENT;
    return make_node(name, EXFS_TYPE_FILE);
}

int exfs_mkdir(const char *name) {
    if (!V.mounted) return EXFS_ENOFS;
    if (V.broken) return EXFS_EBROKEN;
    if (!name_ok(name)) return EXFS_EINVAL;
    if (find(name)) return EXFS_EEXIST;
    if (!parent_ok(name)) return EXFS_ENOENT;
    return make_node(name, EXFS_TYPE_DIR);
}

int exfs_delete(const char *name) {
    if (!V.mounted) return EXFS_ENOFS;
    if (V.broken) return EXFS_EBROKEN;
    ExfsFile *f = find(name);
    if (!f) return EXFS_ENOENT;
    if (f->type == EXFS_TYPE_DIR && has_children(name)) return EXFS_ENOTEMPTY;
    int rc = defer_file(f);
    if (rc != EXFS_OK) return rc;
    f->used = 0;
    f->dirty = 1;
    V.dirty = 1;
    return EXFS_OK;
}

int exfs_stat(const char *name) {
    if (is_root(name)) return EXFS_TYPE_DIR;
    const ExfsFile *f = find(name);
    return f ? (int)f->type : EXFS_ENOENT;
}

int64_t exfs_size(const char *name) {
    if (!V.mounted) return EXFS_ENOFS;
    const ExfsFile *f = find(name);
    if (!f) return EXFS_ENOENT;
    if (f->type != EXFS_TYPE_FILE) return EXFS_EISDIR;
    return (int64_t)f->size;
}

static int starts_with(const char *s, const char *p) {
    while (*p != '\0') {
        if (*s != *p) return 0;
        s++;
        p++;
    }
    return 1;
}

int exfs_list(const char *dir, void (*cb)(const char *name, int type)) {
    if (!V.mounted) return EXFS_ENOFS;
    if (!is_root(dir)) {
        const ExfsFile *d = find(dir);
        if (!d) return EXFS_ENOENT;
        if (d->type != EXFS_TYPE_DIR) return EXFS_EINVAL;
    }

    char pre[EXFS_NAME_MAX + 2];
    uint32_t pl = 0;
    if (is_root(dir)) {
        pre[pl++] = '|';
    } else {
        for (uint32_t k = 0; dir[k] != '\0'; k++) pre[pl++] = dir[k];
        pre[pl++] = '|';
    }
    pre[pl] = '\0';

    int count = 0;
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        const ExfsFile *g = &V.files[i];
        if (!g->used || !starts_with(g->name, pre)) continue;
        const char *rest = g->name + pl;
        if (*rest == '\0') continue;
        int nested = 0;
        for (const char *c = rest; *c != '\0'; c++) {
            if (*c == '|') { nested = 1; break; }
        }
        if (nested) continue;
        cb(rest, (int)g->type);
        count++;
    }
    return count;
}

/* ---- content ------------------------------------------------------------ */

/* Appends a run to the list being built, merging it into the previous run
 * when the two are contiguous on disk. */
static int stage_add(uint64_t start, uint64_t len) {
    if (V.nstage > 0) {
        ExfsExtent *last = &V.stage[V.nstage - 1];
        if (last->start + last->len == start && (uint64_t)last->len + len <= 0xFFFFFFFFull) {
            last->len += (uint32_t)len;
            return EXFS_OK;
        }
    }
    if (V.nstage >= EXFS_MAX_EXT) return EXFS_ENOMEM;
    V.stage[V.nstage].start = start;
    V.stage[V.nstage].len = (uint32_t)len;
    V.nstage++;
    return EXFS_OK;
}

/* Walks the file's logical blocks [lo, hi) in order. mode 0 appends the disk
 * runs to the new list; mode 1 hands them to the deferred release. */
static int walk_old(const ExfsFile *f, uint64_t lo, uint64_t hi, int mode) {
    uint64_t lpos = 0;
    for (uint32_t i = 0; i < f->n && lpos < hi; i++) {
        uint64_t elen = FEXT(f)[i].len;
        uint64_t a = lpos > lo ? lpos : lo;
        uint64_t b = lpos + elen < hi ? lpos + elen : hi;
        if (a < b) {
            uint64_t phys = FEXT(f)[i].start + (a - lpos);
            uint64_t n = b - a;
            if (mode == 0) {
                if (stage_add(phys, n) != EXFS_OK) return EXFS_ENOMEM;
            } else {
                if (defer_push(phys, (uint32_t)n) != EXFS_OK) return EXFS_ENOMEM;
            }
        }
        lpos += elen;
    }
    return EXFS_OK;
}

/* Disk block holding logical block l of the file. */
static int old_phys(const ExfsFile *f, uint64_t l, uint64_t *phys) {
    uint64_t lpos = 0;
    for (uint32_t i = 0; i < f->n; i++) {
        if (l < lpos + FEXT(f)[i].len) {
            *phys = FEXT(f)[i].start + (l - lpos);
            return EXFS_OK;
        }
        lpos += FEXT(f)[i].len;
    }
    return EXFS_ENOFS;
}

/* Replaces the whole content. The new blocks are written before anything
 * refers to them; the old ones are released by the next commit. */
int exfs_write(const char *name, const void *buf, uint64_t len) {
    if (!V.mounted) return EXFS_ENOFS;
    if (V.broken) return EXFS_EBROKEN;
    ExfsFile *f = find(name);
    if (!f) return EXFS_ENOENT;
    if (f->type != EXFS_TYPE_FILE) return EXFS_EISDIR;

    uint64_t need = (len + EXFS_BLOCK - 1) / EXFS_BLOCK;
    uint32_t nf = 0;
    uint64_t got = 0;
    while (got < need) {
        if (nf == EXFS_MAX_EXT) { release_fresh(nf); return EXFS_ENOMEM; }
        ExfsExtent e;
        uint32_t l = take_extent(need - got, &e);
        if (l == 0) { release_fresh(nf); return EXFS_ENOSPC; }
        V.fresh[nf++] = e;
        got += l;
    }
    if (V.ndefer + f->n + f->nchain > EXFS_DEFER_MAX) { release_fresh(nf); return EXFS_ENOMEM; }

    uint32_t roff = 0, rcap = 0;
    if (pool_reserve(nf, &roff, &rcap) != EXFS_OK) { release_fresh(nf); return EXFS_ENOMEM; }

    const uint8_t *src = (const uint8_t *)buf;
    uint64_t left = len;
    for (uint32_t i = 0; i < nf; i++) {
        for (uint32_t k = 0; k < V.fresh[i].len; k++) {
            uint64_t n = left < EXFS_BLOCK ? left : EXFS_BLOCK;
            for (uint64_t j = 0; j < EXFS_BLOCK; j++) V.page[j] = j < n ? src[j] : 0;
            if (blk_put(V.fresh[i].start + k, V.page) != EXFS_OK) {
                pool_release(roff, rcap);
                release_fresh(nf);
                return EXFS_EIO;
            }
            src += n;
            left -= n;
        }
    }

    defer_file(f);
    for (uint32_t i = 0; i < nf; i++) V.pool[roff + i] = V.fresh[i];
    f->ext_off = roff;
    f->ext_cap = rcap;
    f->n = nf;
    f->size = len;
    f->dirty = 1;
    V.dirty = 1;
    return EXFS_OK;
}

/* Only the blocks the range touches are rewritten. Whole blocks are written
 * straight from the caller's buffer; partial blocks are read, merged, then
 * written to a new block. Untouched blocks keep their place on disk. */
int exfs_write_at(const char *name, uint64_t off, const void *buf, uint64_t len) {
    if (!V.mounted) return EXFS_ENOFS;
    if (V.broken) return EXFS_EBROKEN;
    ExfsFile *f = find(name);
    if (!f) return EXFS_ENOENT;
    if (f->type != EXFS_TYPE_FILE) return EXFS_EISDIR;
    if (len == 0) return EXFS_OK;
    if (off + len < off) return EXFS_EINVAL;

    uint64_t oldb = 0;
    for (uint32_t i = 0; i < f->n; i++) oldb += FEXT(f)[i].len;
    uint64_t L0 = off / EXFS_BLOCK;
    uint64_t L1 = (off + len - 1) / EXFS_BLOCK;
    uint64_t W0 = L0 < oldb ? L0 : oldb;       /* a gap past the end starts at oldb */
    uint64_t nfresh = L1 - W0 + 1;

    if (nfresh > gmap_free_total(&V.gm)) return EXFS_ENOSPC;
    if (V.ndefer + f->n + f->nchain + 1 > EXFS_DEFER_MAX) return EXFS_ENOMEM;

    uint32_t nf = 0;
    uint64_t got = 0;
    while (got < nfresh) {
        if (nf == EXFS_MAX_EXT) { release_fresh(nf); return EXFS_ENOMEM; }
        ExfsExtent e;
        uint32_t l = take_extent(nfresh - got, &e);
        if (l == 0) { release_fresh(nf); return EXFS_ENOSPC; }
        V.fresh[nf++] = e;
        got += l;
    }

    /* the new list: old blocks before W0, the fresh runs, old blocks after L1 */
    V.nstage = 0;
    if (walk_old(f, 0, W0, 0) != EXFS_OK) { release_fresh(nf); return EXFS_ENOMEM; }
    for (uint32_t i = 0; i < nf; i++) {
        if (stage_add(V.fresh[i].start, V.fresh[i].len) != EXFS_OK) { release_fresh(nf); return EXFS_ENOMEM; }
    }
    if (walk_old(f, L1 + 1, oldb, 0) != EXFS_OK) { release_fresh(nf); return EXFS_ENOMEM; }

    uint32_t roff = 0, rcap = 0;
    if (pool_reserve(V.nstage, &roff, &rcap) != EXFS_OK) { release_fresh(nf); return EXFS_ENOMEM; }

    /* data first: nothing refers to the new blocks until the metadata below */
    const uint8_t *src = (const uint8_t *)buf;
    uint64_t end = off + len;
    uint32_t fi = 0;
    uint64_t fk = 0;
    for (uint64_t l = W0; l <= L1; l++) {
        uint64_t bs = l * EXFS_BLOCK, be = bs + EXFS_BLOCK;
        int full = off <= bs && end >= be;
        if (!full && l < oldb) {
            uint64_t op = 0;
            if (old_phys(f, l, &op) != EXFS_OK || blk_got(op, V.page) != EXFS_OK) {
                pool_release(roff, rcap);
                release_fresh(nf);
                return EXFS_EIO;
            }
            /* bytes past the old end must read as zero */
            uint64_t zero_from = f->size > bs ? f->size - bs : 0;
            for (uint64_t j = zero_from; j < EXFS_BLOCK; j++) V.page[j] = 0;
        } else {
            for (uint64_t j = 0; j < EXFS_BLOCK; j++) V.page[j] = 0;
        }
        uint64_t from = off > bs ? off : bs;
        uint64_t to = end < be ? end : be;
        for (uint64_t p = from; p < to; p++) V.page[p - bs] = src[p - off];

        if (blk_put(V.fresh[fi].start + fk, V.page) != EXFS_OK) {
            pool_release(roff, rcap);
            release_fresh(nf);
            return EXFS_EIO;
        }
        fk++;
        if (fk == V.fresh[fi].len) { fi++; fk = 0; }
    }

    /* the change in memory: old blocks and the old slice are released by the
     * next commit; the chain is rebuilt there */
    walk_old(f, W0, L1 + 1 < oldb ? L1 + 1 : oldb, 1);
    for (uint32_t i = 0; i < f->nchain; i++) defer_push(f->chain[i], 1);
    pool_release(f->ext_off, f->ext_cap);
    for (uint32_t i = 0; i < V.nstage; i++) V.pool[roff + i] = V.stage[i];
    f->ext_off = roff;
    f->ext_cap = rcap;
    f->nchain = 0;
    f->n = V.nstage;
    if (end > f->size) f->size = end;
    f->dirty = 1;
    V.dirty = 1;
    return EXFS_OK;
}

/* Reads up to cap bytes at off. Returns the bytes read; 0 at or past the end. */
int64_t exfs_read_at(const char *name, uint64_t off, void *buf, uint64_t cap) {
    if (!V.mounted) return EXFS_ENOFS;
    ExfsFile *f = find(name);
    if (!f) return EXFS_ENOENT;
    if (f->type != EXFS_TYPE_FILE) return EXFS_EISDIR;
    if (off >= f->size) return 0;

    uint64_t want = f->size - off < cap ? f->size - off : cap;
    uint8_t *dst = (uint8_t *)buf;

    uint64_t l = off / EXFS_BLOCK;
    uint64_t inblk = off % EXFS_BLOCK;
    uint32_t i = 0;
    uint64_t base = 0;
    while (i < f->n && base + FEXT(f)[i].len <= l) {
        base += FEXT(f)[i].len;
        i++;
    }
    if (i == f->n) return EXFS_EIO;
    uint64_t k = l - base;

    uint64_t done = 0;
    while (done < want) {
        if (i == f->n) return EXFS_EIO;
        if (blk_got(FEXT(f)[i].start + k, V.page) != EXFS_OK) return EXFS_EIO;
        uint64_t avail = EXFS_BLOCK - inblk;
        uint64_t n = want - done < avail ? want - done : avail;
        for (uint64_t j = 0; j < n; j++) dst[done + j] = V.page[inblk + j];
        done += n;
        inblk = 0;
        k++;
        if (k == FEXT(f)[i].len) { i++; k = 0; }
    }
    return (int64_t)done;
}

int64_t exfs_read(const char *name, void *buf, uint64_t cap) {
    return exfs_read_at(name, 0, buf, cap);
}

/* ---- read-only views ---------------------------------------------------- */

const ExfsExtent *exfs_extents(const ExfsFile *f) {
    return f->n ? FEXT(f) : 0;
}

const ExfsFile *exfs_lookup(const char *name) {
    return find(name);
}

const ExfsFile *exfs_file(uint32_t i) {
    return i < EXFS_INODES ? &V.files[i] : 0;
}

uint64_t exfs_total_blocks(void) { return V.total; }
uint64_t exfs_free_blocks(void)  { return gmap_free_total(&V.gm); }
uint64_t exfs_generation(void)   { return V.gen; }
uint64_t exfs_itab_start(void)   { return V.itab_start; }
