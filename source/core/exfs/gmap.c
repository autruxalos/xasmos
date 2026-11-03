/******************************************************************************
 * XASMOS - EXFS group map implementation
 * Path: source/core/exfs/gmap.c
 ******************************************************************************/
#include "gmap.h"

/* Block number of slot s of group g. Group 0 starts after the fixed superblock
 * and table areas, so its slots sit at EXFS_DATA_START. */
static uint64_t slot_block(const GMap *m, uint32_t g, uint8_t s) {
    return (g == 0 ? (uint64_t)EXFS_DATA_START : (uint64_t)g * m->gb) + s;
}

static uint32_t valid_bits(const GMap *m, uint32_t g) {
    uint64_t lo = (uint64_t)g * m->gb;
    uint64_t n = m->total - lo;
    return n < m->gb ? (uint32_t)n : m->gb;
}

static int bit_of(const uint8_t *b, uint32_t i) {
    return (b[i >> 3] >> (i & 7)) & 1;
}

static void bit_put(uint8_t *b, uint32_t i, int v) {
    if (v) b[i >> 3] = (uint8_t)(b[i >> 3] | (1u << (i & 7)));
    else   b[i >> 3] = (uint8_t)(b[i >> 3] & ~(1u << (i & 7)));
}

static int gwrite(GMap *m, uint32_t g, uint8_t s, const uint8_t *bits) {
    uint64_t blk = slot_block(m, g, s);
    return blk_write(m->dev, blk * m->spb, m->spb, bits) == BLK_OK ? GMAP_OK : GMAP_EIO;
}

static int gread(GMap *m, uint32_t g, uint8_t s, uint8_t *bits) {
    uint64_t blk = slot_block(m, g, s);
    return blk_read(m->dev, blk * m->spb, m->spb, bits) == BLK_OK ? GMAP_OK : GMAP_EIO;
}

/* A fresh group bitmap: reserved blocks taken, bits past the end taken. */
static uint32_t fresh_bits(const GMap *m, uint32_t g, uint8_t *bits) {
    for (uint32_t i = 0; i < EXFS_BLOCK; i++) bits[i] = 0;
    uint32_t reserved = (g == 0) ? (uint32_t)EXFS_DATA_START + 2u : 2u;
    uint32_t valid = valid_bits(m, g);
    for (uint32_t i = 0; i < reserved; i++) bit_put(bits, i, 1);
    uint32_t free = 0;
    for (uint32_t i = 0; i < m->gb; i++) {
        if (i >= valid) bit_put(bits, i, 1);
        else if (!bit_of(bits, i)) free++;
    }
    return free;
}

int gmap_init(GMap *m, BlkDev *d, uint64_t total, uint32_t group_blocks) {
    if (!m || !d) return GMAP_EIO;
    if (group_blocks < GMAP_GROUP_MIN || group_blocks > GMAP_GROUP_MAX) return GMAP_EIO;
    if (group_blocks & (group_blocks - 1)) return GMAP_EIO;
    uint64_t ng = (total + group_blocks - 1) / group_blocks;
    if (ng == 0 || ng > GMAP_MAX_GROUPS) return GMAP_EIO;
    uint32_t ss = blk_sector_size(d);
    if (ss == 0 || EXFS_BLOCK % ss != 0) return GMAP_EIO;

    m->dev = d;
    m->spb = EXFS_BLOCK / ss;
    m->total = total;
    m->gb = group_blocks;
    m->ngroups = (uint32_t)ng;
    m->clock = 0;
    for (uint32_t g = 0; g < GMAP_MAX_GROUPS; g++) {
        m->slot[g] = 0;
        m->written[g] = 0;
        m->free[g] = 0;
    }
    for (uint32_t i = 0; i < GMAP_CACHE; i++) m->cache[i].valid = 0;
    return GMAP_OK;
}

int gmap_format(GMap *m) {
    uint8_t bits[EXFS_BLOCK];
    for (uint32_t g = 0; g < m->ngroups; g++) {
        m->free[g] = fresh_bits(m, g, bits);
        if (gwrite(m, g, 0, bits) != GMAP_OK) return GMAP_EIO;
        m->slot[g] = 0;
        m->written[g] = 0;
    }
    for (uint32_t i = 0; i < GMAP_CACHE; i++) m->cache[i].valid = 0;
    return GMAP_OK;
}

void gmap_import(GMap *m, const uint8_t *slot, const uint32_t *free) {
    for (uint32_t g = 0; g < m->ngroups; g++) {
        m->slot[g] = slot[g] ? 1 : 0;
        m->written[g] = 0;
        m->free[g] = free[g];
    }
    for (uint32_t i = 0; i < GMAP_CACHE; i++) m->cache[i].valid = 0;
}

/* Writes one dirty group to its pending slot. */
static int flush_one(GMap *m, GMapEntry *e) {
    uint8_t s = (uint8_t)(1 - m->slot[e->g]);
    if (gwrite(m, e->g, s, e->bits) != GMAP_OK) return GMAP_EIO;
    m->written[e->g] = 1;
    e->dirty = 0;
    return GMAP_OK;
}

/* Returns the cache entry for group g, loading it and evicting the least
 * recently used entry if needed. A dirty victim goes to its pending slot. */
static GMapEntry *load(GMap *m, uint32_t g) {
    for (uint32_t i = 0; i < GMAP_CACHE; i++) {
        if (m->cache[i].valid && m->cache[i].g == g) {
            m->cache[i].stamp = ++m->clock;
            return &m->cache[i];
        }
    }
    GMapEntry *v = 0;
    for (uint32_t i = 0; i < GMAP_CACHE; i++) {
        if (!m->cache[i].valid) { v = &m->cache[i]; break; }
        if (!v || m->cache[i].stamp < v->stamp) v = &m->cache[i];
    }
    if (v->valid && v->dirty && flush_one(m, v) != GMAP_OK) {
        v->valid = 0;
        return 0;
    }
    /* the newest copy of a group is in its pending slot once this transaction
     * has written it; otherwise it is in the committed slot */
    uint8_t src = m->written[g] ? (uint8_t)(1 - m->slot[g]) : m->slot[g];
    if (gread(m, g, src, v->bits) != GMAP_OK) { v->valid = 0; return 0; }
    v->g = g;
    v->valid = 1;
    v->dirty = 0;
    v->stamp = ++m->clock;
    return v;
}

int gmap_flush(GMap *m) {
    for (uint32_t i = 0; i < GMAP_CACHE; i++) {
        if (m->cache[i].valid && m->cache[i].dirty) {
            if (flush_one(m, &m->cache[i]) != GMAP_OK) return GMAP_EIO;
        }
    }
    return GMAP_OK;
}

void gmap_export(const GMap *m, uint8_t *slot, uint32_t *free) {
    for (uint32_t g = 0; g < m->ngroups; g++) {
        slot[g] = m->written[g] ? (uint8_t)(1 - m->slot[g]) : m->slot[g];
        free[g] = m->free[g];
    }
}

void gmap_commit(GMap *m) {
    for (uint32_t g = 0; g < m->ngroups; g++) {
        if (m->written[g]) m->slot[g] = (uint8_t)(1 - m->slot[g]);
        m->written[g] = 0;
    }
}

int gmap_take(GMap *m, uint64_t hint, uint64_t need, uint64_t *start, uint64_t *len) {
    if (need == 0) return GMAP_ENOSPC;
    uint32_t g0 = (uint32_t)(hint / m->gb);
    for (uint32_t k = 0; k < m->ngroups; k++) {
        uint32_t g = (g0 + k) % m->ngroups;
        if (m->free[g] == 0) continue;
        GMapEntry *e = load(m, g);
        if (!e) return GMAP_EIO;
        uint32_t valid = valid_bits(m, g);
        uint32_t i = 0;
        while (i < valid && bit_of(e->bits, i)) i++;
        if (i == valid) continue;
        uint32_t j = i;
        while (j < valid && (uint64_t)(j - i) < need && !bit_of(e->bits, j)) {
            bit_put(e->bits, j, 1);
            j++;
        }
        e->dirty = 1;
        m->free[g] -= (j - i);
        *start = (uint64_t)g * m->gb + i;
        *len = j - i;
        return GMAP_OK;
    }
    return GMAP_ENOSPC;
}

int gmap_release(GMap *m, uint64_t start, uint64_t len) {
    for (uint64_t b = start; b < start + len; b++) {
        uint32_t g = (uint32_t)(b / m->gb);
        uint32_t i = (uint32_t)(b % m->gb);
        GMapEntry *e = load(m, g);
        if (!e) return GMAP_EIO;
        if (!bit_of(e->bits, i)) return GMAP_EUSED;
        bit_put(e->bits, i, 0);
        e->dirty = 1;
        m->free[g]++;
    }
    return GMAP_OK;
}

int gmap_is_used(GMap *m, uint64_t block) {
    uint32_t g = (uint32_t)(block / m->gb);
    uint32_t i = (uint32_t)(block % m->gb);
    GMapEntry *e = load(m, g);
    if (!e) return GMAP_EIO;
    return bit_of(e->bits, i);
}

uint64_t gmap_free_total(const GMap *m) {
    uint64_t n = 0;
    for (uint32_t g = 0; g < m->ngroups; g++) n += m->free[g];
    return n;
}
