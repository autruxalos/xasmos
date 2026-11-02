/******************************************************************************
 * XASMOS - EXFS implementation
 * Path: source/core/fs/exfs.c
 ******************************************************************************/
#include "exfs.h"

#define MAGIC_LEN   8
#define SB_BYTES    48                 /* checksummed header of a superblock */
#define INODE_BYTES 256
#define VERSION     2u

static const uint8_t MAGIC[MAGIC_LEN] = { 'X', 'A', 'S', 'M', 'F', 'S', '2', 0 };

static struct {
    BlkDev    *dev;
    uint32_t   spb;                             /* sectors per block          */
    uint64_t   total;                           /* blocks on the volume       */
    uint64_t   gen;                             /* committed generation       */
    uint64_t   itab_start;                      /* committed inode table area */
    int        mounted;
    int        broken;
    int        dirty;                           /* something to commit        */
    uint8_t    bitmap[EXFS_MAX_BLOCKS / 8];       /* 1 = block in use           */
    uint32_t   group_free[EXFS_MAX_GROUPS];
    uint64_t   hint;                            /* where the next search starts */
    ExfsFile     files[EXFS_INODES];
    ExfsExtent   defer[EXFS_DEFER_MAX];             /* freed after the next commit */
    uint32_t   ndefer;
    ExfsExtent   fresh[EXFS_MAX_EXT];               /* extents of the current write */
    ExfsExtent   stage[EXFS_MAX_EXT];               /* new extent list being built  */
    uint32_t   nstage;
    uint8_t    page[EXFS_BLOCK];
    uint8_t    itab[EXFS_ITAB_BLOCKS * EXFS_BLOCK];
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

static uint64_t fnv64(const uint8_t *p, uint64_t n) {
    uint64_t h = 14695981039346656037ULL;
    for (uint64_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* The inode table area used by a generation: even -> area 0, odd -> area 1. */
static uint64_t itab_area(uint64_t gen) {
    return (gen & 1) ? EXFS_ITAB_AREA1 : EXFS_ITAB_AREA0;
}

/* ---- bitmap and groups -------------------------------------------------- */

static int bit_get(uint64_t b) {
    return (V.bitmap[b >> 3] >> (b & 7)) & 1;
}

static void bit_set(uint64_t b) {
    if (bit_get(b)) return;
    V.bitmap[b >> 3] |= (uint8_t)(1u << (b & 7));
    V.group_free[b / EXFS_GROUP_BLOCKS]--;
}

static void bit_clr(uint64_t b) {
    if (!bit_get(b)) return;
    V.bitmap[b >> 3] &= (uint8_t)~(1u << (b & 7));
    V.group_free[b / EXFS_GROUP_BLOCKS]++;
}

/* Empty map, with the reserved blocks (superblocks and both table areas) taken. */
static void bitmap_clear_all(void) {
    for (uint64_t i = 0; i < (V.total + 7) / 8; i++) V.bitmap[i] = 0;
    for (uint32_t g = 0; g * (uint64_t)EXFS_GROUP_BLOCKS < V.total; g++) {
        uint64_t lo = (uint64_t)g * EXFS_GROUP_BLOCKS;
        uint64_t hi = lo + EXFS_GROUP_BLOCKS;
        if (hi > V.total) hi = V.total;
        V.group_free[g] = (uint32_t)(hi - lo);
    }
    for (uint64_t b = 0; b < EXFS_DATA_START; b++) bit_set(b);
}

/* Marks a block taken while rebuilding from disk. A block claimed twice means
 * the disk is inconsistent, and the volume is refused. */
static int mark_once(uint64_t b) {
    if (b < EXFS_DATA_START || b >= V.total) return EXFS_ENOFS;
    if (bit_get(b)) return EXFS_ENOFS;
    bit_set(b);
    return EXFS_OK;
}

/* Takes up to `need` blocks as one contiguous extent, from the hint onward.
 * Returns the length taken (0 if the volume is full). */
static uint32_t take_extent(uint64_t need, ExfsExtent *out) {
    uint64_t b = V.hint, scanned = 0;
    while (scanned < V.total) {
        if (b >= V.total) b = EXFS_DATA_START;
        if (b % EXFS_GROUP_BLOCKS == 0 && V.group_free[b / EXFS_GROUP_BLOCKS] == 0) {
            b += EXFS_GROUP_BLOCKS;
            scanned += EXFS_GROUP_BLOCKS;
            continue;
        }
        if (!bit_get(b)) break;
        b++;
        scanned++;
    }
    if (scanned >= V.total) return 0;

    uint64_t len = 0;
    while (len < need && b + len < V.total && !bit_get(b + len)) len++;
    for (uint64_t i = 0; i < len; i++) bit_set(b + i);

    out->start = b;
    out->len = (uint32_t)len;
    V.hint = b + len;
    return (uint32_t)len;
}

static void release_fresh(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t k = 0; k < V.fresh[i].len; k++) bit_clr(V.fresh[i].start + k);
    }
}

static uint64_t free_blocks(void) {
    uint64_t n = 0;
    for (uint32_t g = 0; g * (uint64_t)EXFS_GROUP_BLOCKS < V.total; g++) n += V.group_free[g];
    return n;
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

/* ---- superblock --------------------------------------------------------- */

static void sb_encode(uint64_t gen, uint64_t itab, uint32_t nitab) {
    for (uint32_t i = 0; i < EXFS_BLOCK; i++) V.page[i] = 0;
    for (uint32_t i = 0; i < MAGIC_LEN; i++) V.page[i] = MAGIC[i];
    put32(V.page + 8, VERSION);
    put64(V.page + 16, gen);
    put64(V.page + 24, itab);
    put32(V.page + 32, nitab);
    put32(V.page + 36, EXFS_INODES);
    put64(V.page + 40, V.total);
    put64(V.page + 48, fnv64(V.page, SB_BYTES));       /* checksum of bytes 0..47 */
}

/* Reads one superblock copy. EXFS_OK only if magic, version, geometry, table
 * location and checksum all match. */
static int sb_read(uint64_t slot, uint64_t *gen, uint64_t *itab, uint32_t *nitab) {
    if (blk_got(slot, V.page) != EXFS_OK) return EXFS_EIO;
    for (uint32_t i = 0; i < MAGIC_LEN; i++) {
        if (V.page[i] != MAGIC[i]) return EXFS_ENOFS;
    }
    if (get32(V.page + 8) != VERSION) return EXFS_ENOFS;
    if (get64(V.page + 40) != V.total) return EXFS_ENOFS;
    if (get32(V.page + 36) != EXFS_INODES) return EXFS_ENOFS;
    if (get64(V.page + 48) != fnv64(V.page, SB_BYTES)) return EXFS_ENOFS;
    *gen = get64(V.page + 16);
    *itab = get64(V.page + 24);
    *nitab = get32(V.page + 32);
    if (*nitab != EXFS_ITAB_BLOCKS) return EXFS_ENOFS;
    if (*itab != EXFS_ITAB_AREA0 && *itab != EXFS_ITAB_AREA1) return EXFS_ENOFS;
    if (itab_area(*gen) != *itab) return EXFS_ENOFS;
    return EXFS_OK;
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
        put64(r + 128 + i * 16, f->ext[i].start);
        put32(r + 128 + i * 16 + 8, f->ext[i].len);
    }
}

static int inode_decode(const uint8_t *r, ExfsFile *f) {
    f->used = get32(r + 0) != 0;
    f->type = get32(r + 4);
    f->size = get64(r + 8);
    for (uint32_t i = 0; i <= EXFS_NAME_MAX; i++) f->name[i] = (char)r[16 + i];
    f->name[EXFS_NAME_MAX] = '\0';
    f->dirty = 0;
    f->nchain = 0;
    f->n = 0;
    if (!f->used) return EXFS_OK;

    uint32_t ninl = get32(r + 120);
    if (ninl > EXFS_INL) return EXFS_ENOFS;
    for (uint32_t i = 0; i < ninl; i++) {
        f->ext[f->n].start = get64(r + 128 + i * 16);
        f->ext[f->n].len = get32(r + 128 + i * 16 + 8);
        f->n++;
    }
    /* chain: each block holds a next pointer, a count, then the extents */
    uint64_t blk = get64(r + 112);
    while (blk != 0) {
        if (f->nchain >= EXFS_MAX_CHAIN) return EXFS_ENOFS;
        if (blk_got(blk, V.page) != EXFS_OK) return EXFS_EIO;
        uint64_t next = get64(V.page);
        uint32_t cnt = get32(V.page + 8);
        if (cnt > EXFS_IND_EXT || f->n + cnt > EXFS_MAX_EXT) return EXFS_ENOFS;
        for (uint32_t j = 0; j < cnt; j++) {
            f->ext[f->n].start = get64(V.page + 16 + j * 16);
            f->ext[f->n].len = get32(V.page + 16 + j * 16 + 8);
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

/* Hands a file's blocks (extents and chain) to the deferred list. */
static int defer_file(ExfsFile *f) {
    uint32_t need = f->n + f->nchain;
    if (V.ndefer + need > EXFS_DEFER_MAX) return EXFS_ENOMEM;
    for (uint32_t i = 0; i < f->n; i++) defer_push(f->ext[i].start, f->ext[i].len);
    for (uint32_t i = 0; i < f->nchain; i++) defer_push(f->chain[i], 1);
    f->n = 0;
    f->nchain = 0;
    return EXFS_OK;
}

static void release_deferred(void) {
    for (uint32_t i = 0; i < V.ndefer; i++) {
        for (uint32_t k = 0; k < V.defer[i].len; k++) bit_clr(V.defer[i].start + k);
    }
    V.ndefer = 0;
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
    uint32_t n = 0;
    while (name[n] != '\0') n++;
    return n > 0 && n <= EXFS_NAME_MAX;
}

/* ---- public: volume life cycle ------------------------------------------ */

int exfs_commit(void);

int exfs_format(BlkDev *d) {
    if (!d || geometry(d) != EXFS_OK) return EXFS_ENOFS;
    bitmap_clear_all();
    for (uint32_t i = 0; i < EXFS_INODES; i++) V.files[i].used = 0;
    V.ndefer = 0;
    V.gen = 0;
    V.itab_start = 0;
    V.hint = EXFS_DATA_START;
    V.broken = 0;
    V.mounted = 1;
    V.dirty = 1;
    return exfs_commit();
}

int exfs_mount(BlkDev *d) {
    V.mounted = 0;
    if (!d || geometry(d) != EXFS_OK) return EXFS_ENOFS;

    uint64_t g0 = 0, g1 = 0, i0 = 0, i1 = 0;
    uint32_t n0 = 0, n1 = 0;
    int ok0 = sb_read(0, &g0, &i0, &n0) == EXFS_OK;
    int ok1 = sb_read(1, &g1, &i1, &n1) == EXFS_OK;
    if (!ok0 && !ok1) return EXFS_ENOFS;

    uint64_t gen = ok0 ? g0 : g1;
    uint64_t itab = ok0 ? i0 : i1;
    if (ok0 && ok1 && g1 > g0) { gen = g1; itab = i1; }

    for (uint32_t b = 0; b < EXFS_ITAB_BLOCKS; b++) {
        if (blk_got(itab + b, V.itab + b * EXFS_BLOCK) != EXFS_OK) return EXFS_EIO;
    }

    bitmap_clear_all();
    V.hint = EXFS_DATA_START;
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        if (inode_decode(V.itab + i * INODE_BYTES, &V.files[i]) != EXFS_OK) return EXFS_ENOFS;
        ExfsFile *f = &V.files[i];
        if (!f->used) continue;
        for (uint32_t k = 0; k < f->nchain; k++) {
            if (mark_once(f->chain[k]) != EXFS_OK) return EXFS_ENOFS;
        }
        for (uint32_t k = 0; k < f->n; k++) {
            if (f->ext[k].len == 0) return EXFS_ENOFS;
            for (uint32_t j = 0; j < f->ext[k].len; j++) {
                if (mark_once(f->ext[k].start + j) != EXFS_OK) return EXFS_ENOFS;
            }
        }
    }

    V.gen = gen;
    V.itab_start = itab;
    V.ndefer = 0;
    V.broken = 0;
    V.dirty = 0;
    V.mounted = 1;
    return EXFS_OK;
}

/* Commit order: chains, inode table area, superblock (the commit point),
 * then release. A failure before the superblock leaves the previous
 * generation as the truth; a failure after it cannot be rolled back, so the
 * volume is marked broken and must be remounted. */
int exfs_commit(void) {
    if (!V.mounted) return EXFS_ENOFS;
    if (V.broken) return EXFS_EBROKEN;
    if (!V.dirty) return EXFS_OK;

    /* 0. everything that can refuse is checked before anything changes */
    uint64_t need = 0;
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        const ExfsFile *f = &V.files[i];
        if (!f->used || !f->dirty || f->n <= EXFS_INL) continue;
        uint32_t nind = (f->n - EXFS_INL + EXFS_IND_EXT - 1) / EXFS_IND_EXT;
        if (nind > EXFS_MAX_CHAIN) return EXFS_ENOMEM;
        need += nind;
    }
    if (need > free_blocks()) return EXFS_ENOSPC;

    /* 1. chains for files whose extent list changed */
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        ExfsFile *f = &V.files[i];
        if (!f->used || !f->dirty) continue;

        uint32_t nind = f->n > EXFS_INL ? (f->n - EXFS_INL + EXFS_IND_EXT - 1) / EXFS_IND_EXT : 0;
        for (uint32_t k = 0; k < nind; k++) {
            ExfsExtent e;
            if (take_extent(1, &e) != 1) { V.broken = 1; return EXFS_ENOSPC; }
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
                put64(V.page + 16 + j * 16, f->ext[first + j].start);
                put32(V.page + 16 + j * 16 + 8, f->ext[first + j].len);
            }
            if (blk_put(f->chain[k], V.page) != EXFS_OK) { V.broken = 1; return EXFS_EIO; }
        }
    }

    /* 2. the inode table, into the area this generation uses */
    uint64_t gen = V.gen + 1;
    uint64_t area = itab_area(gen);
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        inode_encode(V.itab + i * INODE_BYTES, &V.files[i]);
    }
    for (uint32_t b = 0; b < EXFS_ITAB_BLOCKS; b++) {
        if (blk_put(area + b, V.itab + b * EXFS_BLOCK) != EXFS_OK) { V.broken = 1; return EXFS_EIO; }
    }

    /* 3. the commit point: superblock of the new generation */
    sb_encode(gen, area, EXFS_ITAB_BLOCKS);
    if (blk_put(gen & 1, V.page) != EXFS_OK) { V.broken = 1; return EXFS_EIO; }

    /* 4. the new state is durable: release what it no longer uses */
    V.gen = gen;
    V.itab_start = area;
    release_deferred();
    for (uint32_t i = 0; i < EXFS_INODES; i++) V.files[i].dirty = 0;
    V.dirty = 0;
    return EXFS_OK;
}

/* ---- public: files ------------------------------------------------------ */

int exfs_create(const char *name) {
    if (!V.mounted) return EXFS_ENOFS;
    if (V.broken) return EXFS_EBROKEN;
    if (!name_ok(name)) return EXFS_EINVAL;
    if (find(name)) return EXFS_EEXIST;
    for (uint32_t i = 0; i < EXFS_INODES; i++) {
        ExfsFile *f = &V.files[i];
        if (f->used) continue;
        f->used = 1;
        f->type = 0;
        f->size = 0;
        f->n = 0;
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

int exfs_delete(const char *name) {
    if (!V.mounted) return EXFS_ENOFS;
    if (V.broken) return EXFS_EBROKEN;
    ExfsFile *f = find(name);
    if (!f) return EXFS_ENOENT;
    int rc = defer_file(f);
    if (rc != EXFS_OK) return rc;
    f->used = 0;
    f->dirty = 1;
    V.dirty = 1;
    return EXFS_OK;
}

/* Replaces the whole content. The new blocks are written before anything
 * refers to them; the old ones are released only after the next commit. */
int exfs_write(const char *name, const void *buf, uint64_t len) {
    if (!V.mounted) return EXFS_ENOFS;
    if (V.broken) return EXFS_EBROKEN;
    ExfsFile *f = find(name);
    if (!f) return EXFS_ENOENT;

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

    const uint8_t *src = (const uint8_t *)buf;
    uint64_t left = len;
    for (uint32_t i = 0; i < nf; i++) {
        for (uint32_t k = 0; k < V.fresh[i].len; k++) {
            uint64_t n = left < EXFS_BLOCK ? left : EXFS_BLOCK;
            for (uint64_t j = 0; j < EXFS_BLOCK; j++) V.page[j] = j < n ? src[j] : 0;
            if (blk_put(V.fresh[i].start + k, V.page) != EXFS_OK) {
                release_fresh(nf);
                return EXFS_EIO;
            }
            src += n;
            left -= n;
        }
    }

    defer_file(f);
    for (uint32_t i = 0; i < nf; i++) f->ext[i] = V.fresh[i];
    f->n = nf;
    f->size = len;
    f->dirty = 1;
    V.dirty = 1;
    return EXFS_OK;
}

/* ---- byte-range I/O ----------------------------------------------------- */

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
        uint64_t elen = f->ext[i].len;
        uint64_t a = lpos > lo ? lpos : lo;
        uint64_t b = lpos + elen < hi ? lpos + elen : hi;
        if (a < b) {
            uint64_t phys = f->ext[i].start + (a - lpos);
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
        if (l < lpos + f->ext[i].len) {
            *phys = f->ext[i].start + (l - lpos);
            return EXFS_OK;
        }
        lpos += f->ext[i].len;
    }
    return EXFS_ENOFS;
}

/* Only the blocks the range touches are rewritten. Whole blocks are written
 * straight from the caller's buffer; partial blocks are read, merged, then
 * written to a new block. Untouched blocks keep their place on disk. */
int exfs_write_at(const char *name, uint64_t off, const void *buf, uint64_t len) {
    if (!V.mounted) return EXFS_ENOFS;
    if (V.broken) return EXFS_EBROKEN;
    ExfsFile *f = find(name);
    if (!f) return EXFS_ENOENT;
    if (len == 0) return EXFS_OK;
    if (off + len < off) return EXFS_EINVAL;

    uint64_t oldb = 0;
    for (uint32_t i = 0; i < f->n; i++) oldb += f->ext[i].len;
    uint64_t L0 = off / EXFS_BLOCK;
    uint64_t L1 = (off + len - 1) / EXFS_BLOCK;
    uint64_t W0 = L0 < oldb ? L0 : oldb;       /* a gap past the end starts at oldb */
    uint64_t nfresh = L1 - W0 + 1;

    /* everything that can refuse is checked before anything changes */
    if (nfresh > free_blocks()) return EXFS_ENOSPC;
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
            release_fresh(nf);
            return EXFS_EIO;
        }
        fk++;
        if (fk == V.fresh[fi].len) { fi++; fk = 0; }
    }

    /* commit the change in memory: the rewritten blocks are released after
     * the next commit, the chain is rebuilt there */
    walk_old(f, W0, L1 + 1 < oldb ? L1 + 1 : oldb, 1);
    for (uint32_t i = 0; i < f->nchain; i++) defer_push(f->chain[i], 1);
    f->nchain = 0;
    for (uint32_t i = 0; i < V.nstage; i++) f->ext[i] = V.stage[i];
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
    if (off >= f->size) return 0;

    uint64_t want = f->size - off < cap ? f->size - off : cap;
    uint8_t *dst = (uint8_t *)buf;

    uint64_t l = off / EXFS_BLOCK;
    uint64_t inblk = off % EXFS_BLOCK;
    uint32_t i = 0;
    uint64_t base = 0;
    while (i < f->n && base + f->ext[i].len <= l) {
        base += f->ext[i].len;
        i++;
    }
    if (i == f->n) return EXFS_EIO;
    uint64_t k = l - base;

    uint64_t done = 0;
    while (done < want) {
        if (i == f->n) return EXFS_EIO;
        if (blk_got(f->ext[i].start + k, V.page) != EXFS_OK) return EXFS_EIO;
        uint64_t avail = EXFS_BLOCK - inblk;
        uint64_t n = want - done < avail ? want - done : avail;
        for (uint64_t j = 0; j < n; j++) dst[done + j] = V.page[inblk + j];
        done += n;
        inblk = 0;
        k++;
        if (k == f->ext[i].len) { i++; k = 0; }
    }
    return (int64_t)done;
}

/* Whole-file read: the first cap bytes. */
int64_t exfs_read(const char *name, void *buf, uint64_t cap) {
    return exfs_read_at(name, 0, buf, cap);
}

/* ---- public: state ------------------------------------------------------ */

const ExfsFile *exfs_lookup(const char *name) {
    return find(name);
}

const ExfsFile *exfs_file(uint32_t i) {
    return i < EXFS_INODES ? &V.files[i] : 0;
}

uint64_t exfs_total_blocks(void) { return V.total; }
uint64_t exfs_free_blocks(void)  { return free_blocks(); }
uint64_t exfs_generation(void)   { return V.gen; }
uint64_t exfs_itab_start(void)   { return V.itab_start; }
const uint8_t *exfs_bitmap(void) { return V.bitmap; }
