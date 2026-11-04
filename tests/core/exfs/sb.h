/******************************************************************************
 * XASMOS - EXFS superblock, version 3 (pure encoding, no I/O)
 * Path: source/core/exfs/sb.h
 *
 * One 4 KiB block, little-endian:
 *    0  magic "XASMFS2\0"          8  version (3)        16  generation
 *   24  inode table block          32  inode table blocks 36  inode count
 *   40  volume blocks              48  group count
 *   52  reserved (zero)            64  slot per group   [512 bytes]
 *  576  free blocks per group      [512 x u32, 2048 bytes]
 *  4088 checksum: FNV-1a 64 of bytes 0..4087
 * Unused slots and free counts are zero. The checksum covers every field, so
 * a torn or corrupted superblock is rejected as a whole.
 ******************************************************************************/
#ifndef XASMOS_EXFS_SB_H
#define XASMOS_EXFS_SB_H

#include <stdint.h>
#include "exfs.h"
#include "gmap.h"

#define SB_VERSION      3u
#define SB_MAX_GROUPS   GMAP_MAX_GROUPS
#define SB_CHECK_OFF    4088u

#define SB_OK        0
#define SB_EBAD    (-1)   /* not an EXFS superblock, or wrong version         */
#define SB_ECHECK  (-2)   /* checksum mismatch: torn or corrupted              */
#define SB_EFIELD  (-3)   /* checksum matches but a field is out of range      */

typedef struct {
    uint64_t gen;
    uint64_t itab;                              /* first block of the table    */
    uint32_t nitab;
    uint32_t ninodes;
    uint64_t total;
    uint32_t ngroups;
    uint8_t  slot[SB_MAX_GROUPS];
    uint32_t free[SB_MAX_GROUPS];
} SbFields;

/* Fills page (EXFS_BLOCK bytes) from f. Returns SB_OK or SB_EFIELD if f is
 * not representable. */
int sb_encode(uint8_t *page, const SbFields *f);

/* Checks and reads page into f. Checks run in order: magic and version, then
 * checksum, then every field. */
int sb_decode(const uint8_t *page, SbFields *f);

#endif /* XASMOS_EXFS_SB_H */
