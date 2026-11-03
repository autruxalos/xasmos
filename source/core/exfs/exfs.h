/******************************************************************************
 * XASMOS - EXFS: 64-bit blocks, extents, copy-on-write commits
 * Path: source/core/exfs/exfs.h
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
#ifndef XASMOS_EXFS_H
#define XASMOS_EXFS_H

#include <stdint.h>
#include "../blk/blk.h"

#define EXFS_BLOCK         4096u
#define EXFS_MAX_BLOCKS    (1u << 24)          /* 64 GiB volume ceiling            */
#define EXFS_GROUP_BLOCKS  32768u              /* 128 MiB per allocation group     */
#define EXFS_MAX_GROUPS    (EXFS_MAX_BLOCKS / EXFS_GROUP_BLOCKS)
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

typedef struct {
    uint64_t start;        /* first block */
    uint32_t len;          /* blocks, contiguous */
} ExfsExtent;

typedef struct {
    int      used;
    uint32_t type;                        /* 0 = regular file               */
    uint64_t size;                        /* bytes                          */
    char     name[EXFS_NAME_MAX + 1];
    uint32_t n;                           /* extents in the pool slice      */
    uint32_t ext_off;                     /* slice of the shared extent pool */
    uint32_t ext_cap;                     /* slice capacity, 0 = none        */
    uint32_t nchain;                      /* committed chain blocks         */
    uint64_t chain[EXFS_MAX_CHAIN];
    int      dirty;                       /* changed since the last commit  */
} ExfsFile;

int            exfs_format(BlkDev *d);      /* destroys the device content    */
int            exfs_mount(BlkDev *d);       /* newest valid generation        */
int            exfs_commit(void);
int            exfs_create(const char *name);
int            exfs_delete(const char *name);
int            exfs_write(const char *name, const void *buf, uint64_t len);
int64_t        exfs_read(const char *name, void *buf, uint64_t cap);

/* Directories: a name is a directory, a file, or absent. The root '|' always
 * exists. Children are the names one level below: "|user|x" is a child of
 * "|user". A directory with children cannot be deleted. */
#define EXFS_TYPE_FILE  0
#define EXFS_TYPE_DIR   1
int            exfs_mkdir(const char *name);
int            exfs_is_mounted(void);
int            exfs_unmount(void);            /* commits, then unmounts         */
int64_t        exfs_size(const char *name);   /* bytes, or a negative EXFS_E* code */
int            exfs_stat(const char *name);   /* type, or EXFS_ENOENT           */
int            exfs_list(const char *dir, void (*cb)(const char *name, int type));

/* Byte-range I/O. Any size, any offset; the caller never needs a buffer as
 * large as the file. Writing past the end leaves zeros in the gap. A failed
 * call changes nothing. */
int            exfs_write_at(const char *name, uint64_t off, const void *buf, uint64_t len);
int64_t        exfs_read_at(const char *name, uint64_t off, void *buf, uint64_t cap);

/* The extents of a file: f->n entries, or NULL if it has none. */
const ExfsExtent *exfs_extents(const ExfsFile *f);
const ExfsFile   *exfs_lookup(const char *name);
const ExfsFile   *exfs_file(uint32_t i);      /* i < EXFS_INODES                */

uint64_t       exfs_total_blocks(void);
uint64_t       exfs_free_blocks(void);
uint64_t       exfs_generation(void);
uint64_t       exfs_itab_start(void);       /* first block of the committed table */
const uint8_t *exfs_bitmap(void);

#endif /* XASMOS_EXFS_H */
