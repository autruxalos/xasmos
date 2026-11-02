/******************************************************************************
 * XASMOS - EXFS persistence API
 * Path: source/arch/amd64/drivers/storage/exfs/store.h
 *
 * Disk layout, in 4 KiB blocks:
 *   block 0            superblock (magic, generation, active table, checksums)
 *   blocks 1..9        node table, area A
 *   blocks 10..18      node table, area B
 *   blocks 19..        file contents, one block per file
 * A commit writes the inactive table area, then the superblock. The superblock
 * is the only write that changes what the disk means, so a crash before it
 * leaves the previous state intact.
 ******************************************************************************/
#ifndef XASMOS_EXFS_STORE_H
#define XASMOS_EXFS_STORE_H

#include "../../../../../core/blk/blk.h"

#define EXFS_ERR_NOFS  (-5)   /* the disk has no valid EXFS volume            */
#define EXFS_ERR_IO    (-6)   /* the device failed during the operation       */

/* Writes an empty volume with the four system directories and commits it.
 * Destroys whatever was on the device. The caller must have checked that. */
int exfs_format(BlkDev *d);

/* Loads the last committed state. Fails with EXFS_ERR_NOFS if the disk has no
 * valid volume; it never formats. */
int exfs_mount(BlkDev *d);

/* Commits every change made since the last commit. Returns 0, or an error
 * with the previous committed state still intact. */
int exfs_sync(void);

int exfs_mounted(void);

#endif /* XASMOS_EXFS_STORE_H */
