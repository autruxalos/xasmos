/******************************************************************************
 * XASMOS - EXFS group map: free-block bitmap kept on disk, cached in RAM
 * Path: source/core/exfs/gmap.h
 *
 * The volume is cut into groups of gb blocks. Each group's bitmap is one
 * 4 KiB block, stored in two slots at the start of the group (slot 0 and
 * slot 1). The superblock says which slot is committed for each group. A
 * transaction writes the other slot and the superblock last, so a crash
 * before the superblock leaves every group at its committed slot.
 *
 * Only GMAP_CACHE groups are in RAM. The cache writes a dirty group to its
 * pending slot when it evicts it; that is safe because the pending slot is
 * never referenced by the committed state.
 *
 * Commit sequence, used by the filesystem:
 *   gmap_flush   write dirty groups to their pending slots
 *   gmap_export  the slot and free-count tables the superblock must hold
 *   <superblock written and durable>
 *   gmap_commit  pending slots become committed
 ******************************************************************************/
#ifndef XASMOS_EXFS_GMAP_H
#define XASMOS_EXFS_GMAP_H

#include <stdint.h>
#include "exfs.h"

#define GMAP_MAX_GROUPS  512u       /* 64 GiB at 32768 blocks per group        */
#define GMAP_CACHE       8u         /* groups held in RAM: 32 KiB              */
#define GMAP_GROUP_MAX   32768u     /* blocks per group: one 4 KiB bitmap      */
#define GMAP_GROUP_MIN   64u

#define GMAP_OK       0
#define GMAP_EIO    (-1)
#define GMAP_ENOSPC (-2)
#define GMAP_EUSED  (-3)            /* release of a block that is not in use  */

typedef struct {
    uint8_t  bits[EXFS_BLOCK];
    uint32_t g;
    int      valid;
    int      dirty;
    uint32_t stamp;
} GMapEntry;

typedef struct {
    BlkDev    *dev;
    uint32_t   spb;                           /* sectors per block            */
    uint64_t   total;                         /* blocks on the volume         */
    uint32_t   gb;                            /* blocks per group             */
    uint32_t   ngroups;
    uint8_t    slot[GMAP_MAX_GROUPS];         /* committed slot per group     */
    uint8_t    written[GMAP_MAX_GROUPS];      /* pending slot written in this transaction */
    uint32_t   free[GMAP_MAX_GROUPS];         /* free blocks per group        */
    GMapEntry  cache[GMAP_CACHE];
    uint32_t   clock;
} GMap;

/* Sets the geometry. No I/O. group_blocks must be a power of two in
 * [GMAP_GROUP_MIN, GMAP_GROUP_MAX]. */
int      gmap_init(GMap *m, BlkDev *d, uint64_t total, uint32_t group_blocks);

/* Writes a fresh map: every group at slot 0, reserved blocks taken. */
int      gmap_format(GMap *m);

/* Adopts the committed slot and free-count tables (from a superblock). */
void     gmap_import(GMap *m, const uint8_t *slot, const uint32_t *free);

/* Writes every dirty group to its pending slot. Call before gmap_export. */
int      gmap_flush(GMap *m);

/* The slot and free-count tables as they will be once this transaction
 * commits. Valid only after gmap_flush. */
void     gmap_export(const GMap *m, uint8_t *slot, uint32_t *free);

/* Makes the pending slots committed. Call after the superblock is durable. */
void     gmap_commit(GMap *m);

/* Takes up to need contiguous free blocks, searching from the group of hint.
 * Returns GMAP_OK with start and len, or GMAP_ENOSPC. */
int      gmap_take(GMap *m, uint64_t hint, uint64_t need, uint64_t *start, uint64_t *len);

/* Frees len blocks from start. GMAP_EUSED if any of them was already free. */
int      gmap_release(GMap *m, uint64_t start, uint64_t len);

/* 1 if the block is in use, 0 if free, GMAP_EIO on a read error. */
int      gmap_is_used(GMap *m, uint64_t block);

uint64_t gmap_free_total(const GMap *m);

#endif /* XASMOS_EXFS_GMAP_H */
