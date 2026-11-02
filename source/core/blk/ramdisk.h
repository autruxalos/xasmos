/******************************************************************************
 * XASMOS - RAM block device (portable)
 * Path: source/core/blk/ramdisk.h
 *
 * A BlkDev whose storage is memory the caller provides. The boot volume is one
 * of these: it lets the filesystem run before any disk is present. Its content
 * lasts until the next boot; nothing it holds reaches a disk.
 ******************************************************************************/
#ifndef XASMOS_RAMDISK_H
#define XASMOS_RAMDISK_H

#include <stdint.h>
#include "blk.h"

typedef struct {
    uint8_t  *mem;             /* sector_count * sector_size bytes, caller-owned */
    uint32_t  sector_size;
    uint64_t  sector_count;
    BlkOps    ops;
    BlkDev    dev;
} RamDisk;

/* Binds r to mem and zeroes it. sector_size must be a power of two in
 * 512..4096. Returns BLK_OK or BLK_E_ARG. */
int      ramdisk_init(RamDisk *r, void *mem, uint64_t bytes, uint32_t sector_size);

/* The block device view of r, for the filesystem. */
BlkDev  *ramdisk_device(RamDisk *r);

#endif /* XASMOS_RAMDISK_H */
