/******************************************************************************
 * XASMOS - EXFS: 64-bit blocks, extents, copy-on-write commits
 * Path: source/core/exfs/exfs.h
 *
 * Portable: depends only on blk.h and its own modules (gmap, sb). The kernel
 * supplies a BlkDev; the format and its crash behaviour are the same on every
 * architecture.
 *
 * Disk layout, 4 KiB blocks, block numbers are 64-bit:
 *   block 0, 1       superblock copies, format v3; the newest valid one wins
 *   blocks 2..9      inode table, area 0 (used by even generations)
 *   blocks 10..17    inode table, area 1 (used by odd generations)
 *   blocks 18..      group map slots, file extents, extent-chain blocks
 *
 * Free space lives in the group map (gmap.h), on disk, cached in RAM.
 *
 * Commit order (the one invariant that prevents corruption):
 *   1. chain blocks for changed files are allocated from free blocks only
 *   2. blocks freed by this transaction are released into the map
 *   3. dirty map groups are written to their pending slots
 *   4. the inode table is written to this generation's area
 *   5. the superblock of the new generation is written: the commit point
 *   6. pending map slots become committed
 * Blocks freed by a transaction are in use until step 2, so no write inside the
 * transaction can reuse them. A failure after step 1 marks the volume broken:
 * the in-memory map has moved on, so the volume must be remounted.
 ******************************************************************************/
#ifndef XASMOS_EXFS_H
#define XASMOS_EXFS_H

#include <stdint.h>
#include "../blk/blk.h"

#define EXFS_BLOCK         4096u
#define EXFS_MAX_BLOCKS    (1u << 24)          /* 64 GiB volume ceiling            */
#define EXFS_INODES        128
#define EXFS_NAME_MAX      95
#define EXFS_INL           8                   /* extents stored inside an inode   */
#define EXFS_IND_EXT       254                 /* extents per chain block          */
#define EXFS_MAX_EXT       2048                /* extents per file                 */
#define EXFS_MAX_CHAIN     16                  /* chain blocks per file            */
#define EXFS_ITAB_BLOCKS   8u                  /* 128 inodes * 256 bytes           */
#define EXFS_ITAB_AREA0    2u                  /* first block of table area 0      */
#define EXFS_ITAB_AREA1    (EXFS_ITAB_AREA0 + EXFS_ITAB_BLOCKS)
#define EXFS_DATA_START    (EXFS_ITAB_AREA1 + EXFS_ITAB_BLOCKS)   /* 18 */
#define EXFS_DEFER_MAX     8192                /* blocks awaiting release          */
#define EXFS_POOL_EXT      16384               /* extents shared by all files (256 KiB) */

#define EXFS_OK        0
#define EXFS_ENOENT   (-1)
#define EXFS_ENOSPC   (-2)
#define EXFS_EIO      (-3)
#define EXFS_ENOFS    (-4)   /* the disk has no valid volume                    */
#define EXFS_EEXIST   (-5)
#define EXFS_EINVAL   (-6)
#define EXFS_EBROKEN  (-7)   /* a commit failed: remount before using           */
#define EXFS_ENOMEM   (-8)   /* no room: file extents or the extent pool        */
#define EXFS_EISDIR   (-9)   /* a directory is not a file                       */
#define EXFS_ENOTEMPTY (-10) /* directory still has children                    */
#define EXFS_ENOTDIR  (-11)  /* a path component is not a directory             */

#define EXFS_TYPE_FILE  0
#define EXFS_TYPE_DIR   1

typedef struct {
    uint64_t start;        /* first block */
    uint32_t len;          /* blocks, contiguous */
} ExfsExtent;

typedef struct {
    int      used;
    uint32_t type;                        /* EXFS_TYPE_FILE or EXFS_TYPE_DIR  */
    uint64_t size;                        /* bytes                          */
    char     name[EXFS_NAME_MAX + 1];
    uint32_t n;                           /* extents in the pool slice      */
    uint32_t ext_off;                     /* slice of the shared extent pool */
    uint32_t ext_cap;                     /* slice capacity, 0 = none        */
    uint32_t nchain;                      /* committed chain blocks         */
    uint64_t chain[EXFS_MAX_CHAIN];
    int      dirty;                       /* changed since the last commit  */
} ExfsFile;

/* Volume life cycle. Format destroys the device content and is not atomic:
 * a crash during format leaves no valid volume, never a half-valid one. */
int            exfs_format(BlkDev *d);
int            exfs_mount(BlkDev *d);       /* newest valid superblock        */
int            exfs_commit(void);
int            exfs_unmount(void);          /* commits, then unmounts          */
int            exfs_is_mounted(void);
int            exfs_is_used(uint64_t block); /* 1 used, 0 free, <0 error        */

/* Names. A name starts with '|'. Its parent must exist and be a directory,
 * except the root '|', which always exists. */
int            exfs_create(const char *name);
int            exfs_mkdir(const char *name);
int            exfs_delete(const char *name);   /* a directory must be empty */
int            exfs_stat(const char *name);     /* type, or EXFS_ENOENT     */
int64_t        exfs_size(const char *name);     /* bytes, or a negative EXFS_E* */
int            exfs_list(const char *dir, void (*cb)(const char *name, int type));

/* Content. exfs_write replaces the whole file; exfs_write_at changes a byte
 * range. Any size, any offset: the caller never needs a buffer as large as the
 * file. Writing past the end leaves zeros in the gap. A failed call changes
 * nothing. */
int            exfs_write(const char *name, const void *buf, uint64_t len);
int            exfs_write_at(const char *name, uint64_t off, const void *buf, uint64_t len);
int64_t        exfs_read(const char *name, void *buf, uint64_t cap);
int64_t        exfs_read_at(const char *name, uint64_t off, void *buf, uint64_t cap);

/* Read-only views. */
const ExfsExtent *exfs_extents(const ExfsFile *f);   /* f->n entries, or NULL */
const ExfsFile   *exfs_lookup(const char *name);
const ExfsFile   *exfs_file(uint32_t i);             /* i < EXFS_INODES       */

uint64_t       exfs_total_blocks(void);
uint64_t       exfs_free_blocks(void);
uint64_t       exfs_generation(void);
uint64_t       exfs_itab_start(void);       /* first block of the committed table */

#endif /* XASMOS_EXFS_H */
