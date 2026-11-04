/******************************************************************************
 * XASMOS - EXFS superblock v3 implementation
 * Path: source/core/exfs/sb.c
 ******************************************************************************/
#include "sb.h"

static const uint8_t MAGIC[8] = { 'X', 'A', 'S', 'M', 'F', 'S', '2', 0 };

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

/* The fields the checksum-verified page must satisfy. */
static int fields_ok(const SbFields *f) {
    if (f->ngroups == 0 || f->ngroups > SB_MAX_GROUPS) return 0;
    if (f->itab != EXFS_ITAB_AREA0 && f->itab != EXFS_ITAB_AREA1) return 0;
    if (f->nitab != EXFS_ITAB_BLOCKS) return 0;
    if (f->ninodes != EXFS_INODES) return 0;
    if (f->total < EXFS_DATA_START + 16 || f->total > EXFS_MAX_BLOCKS) return 0;
    /* the table area must be the one this generation uses */
    if (f->itab != ((f->gen & 1) ? EXFS_ITAB_AREA1 : EXFS_ITAB_AREA0)) return 0;
    for (uint32_t g = 0; g < f->ngroups; g++) {
        if (f->slot[g] > 1) return 0;
    }
    for (uint32_t g = f->ngroups; g < SB_MAX_GROUPS; g++) {
        if (f->slot[g] != 0 || f->free[g] != 0) return 0;
    }
    return 1;
}

int sb_encode(uint8_t *page, const SbFields *f) {
    if (!fields_ok(f)) return SB_EFIELD;
    for (uint32_t i = 0; i < EXFS_BLOCK; i++) page[i] = 0;
    for (uint32_t i = 0; i < 8; i++) page[i] = MAGIC[i];
    put32(page + 8, SB_VERSION);
    put64(page + 16, f->gen);
    put64(page + 24, f->itab);
    put32(page + 32, f->nitab);
    put32(page + 36, f->ninodes);
    put64(page + 40, f->total);
    put32(page + 48, f->ngroups);
    for (uint32_t g = 0; g < SB_MAX_GROUPS; g++) {
        page[64 + g] = f->slot[g];
        put32(page + 576 + g * 4, f->free[g]);
    }
    put64(page + SB_CHECK_OFF, fnv64(page, SB_CHECK_OFF));
    return SB_OK;
}

int sb_decode(const uint8_t *page, SbFields *f) {
    for (uint32_t i = 0; i < 8; i++) {
        if (page[i] != MAGIC[i]) return SB_EBAD;
    }
    if (get32(page + 8) != SB_VERSION) return SB_EBAD;
    if (get64(page + SB_CHECK_OFF) != fnv64(page, SB_CHECK_OFF)) return SB_ECHECK;

    f->gen = get64(page + 16);
    f->itab = get64(page + 24);
    f->nitab = get32(page + 32);
    f->ninodes = get32(page + 36);
    f->total = get64(page + 40);
    f->ngroups = get32(page + 48);
    for (uint32_t g = 0; g < SB_MAX_GROUPS; g++) {
        f->slot[g] = page[64 + g];
        f->free[g] = get32(page + 576 + g * 4);
    }
    return fields_ok(f) ? SB_OK : SB_EFIELD;
}
