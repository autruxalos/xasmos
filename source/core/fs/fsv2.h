/******************************************************************************
 * XASMOS - filesystem v2: 64-bit blocks, extents, copy-on-write commits
 * Path: source/core/fs/fsv2.h
 *
 * Portable: it depends only on blk.h. The kernel supplies a BlkDev; the
 * format and its crash behaviour are the same on every architecture.
 *
 * Disk layout, 4 KiB blocks, block numbers are 64-bit:
 *   block 0, 1       superblock copies; the newest valid generation wins
 *   blocks 2..9      inode table, area 0 (used by even generations)
 *   blocks 10..17    inode table, area 1 (used by odd generations)
 *   blocks 18..      file extents and extent-chain blocks, allocated on demand
 * Two fixed table areas mean a commit never has to find space for the table,
 * and the previous generation's table is never overwritten by the next commit.
 *
 * Commit protocol (copy-on-write):
 *   1. file contents are written to blocks no committed state uses
 *   2. extent chains and the inode table are written to new blocks
 *   3. the superblock of the next generation is written last: the commit point
 *   4. only then are the blocks freed by this transaction released
 * A crash before step 3 leaves the previous generation intact. A torn
 * superblock fails its checksum and the previous generation is used instead.
 *
 * Free space is a bitmap in RAM, rebuilt at mount from the reachable blocks,
 * so a crash can never leak a block permanently.
 ******************************************************************************/
#ifndef XASMOS_FSV2_H
#define XASMOS_FSV2_H

#include <stdint.h>
#include "../blk/blk.h"

#define FS_BLOCK         4096u
#define FS_MAX_BLOCKS    (1u << 24)          /* 64 GiB volume ceiling            */
#define FS_GROUP_BLOCKS  32768u              /* 128 MiB per allocation group     */
#define FS_MAX_GROUPS    (FS_MAX_BLOCKS / FS_GROUP_BLOCKS)
#define FS_INODES        128
#define FS_NAME_MAX      95
#define FS_INL           8                   /* extents stored inside an inode   */
#define FS_IND_EXT       254                 /* extents per chain block          */
#define FS_MAX_EXT       2048                /* extents per file (RAM limit)     */
#define FS_MAX_CHAIN     16                  /* chain blocks per file            */
#define FS_ITAB_BLOCKS   8u                  /* 128 inodes * 256 bytes           */
#define FS_ITAB_AREA0    2u                  /* first block of table area 0      */
#define FS_ITAB_AREA1    (FS_ITAB_AREA0 + FS_ITAB_BLOCKS)
#define FS_DATA_START    (FS_ITAB_AREA1 + FS_ITAB_BLOCKS)   /* 18 */
#define FS_DEFER_MAX     32768               /* blocks awaiting release          */

#define FS_OK        0
#define FS_ENOENT   (-1)
#define FS_ENOSPC   (-2)
#define FS_EIO      (-3)
#define FS_ENOFS    (-4)   /* the disk has no valid volume                    */
#define FS_EEXIST   (-5)
#define FS_EINVAL   (-6)
#define FS_EBROKEN  (-7)   /* a commit failed after the commit point: remount  */
#define FS_ENOMEM   (-8)   /* file needs more than FS_MAX_EXT extents         */

typedef struct {
    uint64_t start;        /* first block */
    uint32_t len;          /* blocks, contiguous */
} FsExtent;

typedef struct {
    int      used;
    uint32_t type;                        /* 0 = regular file               */
    uint64_t size;                        /* bytes                          */
    char     name[FS_NAME_MAX + 1];
    uint32_t n;                           /* extents in ext[]               */
    FsExtent ext[FS_MAX_EXT];
    uint32_t nchain;                      /* committed chain blocks         */
    uint64_t chain[FS_MAX_CHAIN];
    int      dirty;                       /* changed since the last commit  */
} FsFile;

int            fs_format(BlkDev *d);      /* destroys the device content    */
int            fs_mount(BlkDev *d);       /* newest valid generation        */
int            fs_commit(void);
int            fs_create(const char *name);
int            fs_delete(const char *name);
int            fs_write(const char *name, const void *buf, uint64_t len);
int64_t        fs_read(const char *name, void *buf, uint64_t cap);
const FsFile  *fs_lookup(const char *name);
const FsFile  *fs_file(uint32_t i);       /* i < FS_INODES                  */

uint64_t       fs_total_blocks(void);
uint64_t       fs_free_blocks(void);
uint64_t       fs_generation(void);
uint64_t       fs_itab_start(void);       /* first block of the committed table */
const uint8_t *fs_bitmap(void);

#endif /* XASMOS_FSV2_H */
