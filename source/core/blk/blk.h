/******************************************************************************
 * XASMOS - block device interface (portable)
 * Path: source/core/blk/blk.h
 *
 * EXFS and any other client talk to disks only through this header. Drivers
 * (NVMe, AHCI, a test image) fill in a BlkOps table; blk.c checks every
 * request against the device size before a driver sees it.
 *
 * Contract for drivers: a write returns BLK_OK only after the device has
 * acknowledged it. Anything else is an error, and the caller must not assume
 * the data reached the disk.
 ******************************************************************************/
#ifndef XASMOS_BLK_H
#define XASMOS_BLK_H

#include <stdint.h>

#define BLK_OK         0
#define BLK_E_ARG     (-1)   /* null pointer, zero count                       */
#define BLK_E_RANGE   (-2)   /* lba + count runs past the end of the device    */
#define BLK_E_IO      (-3)   /* the device reported an error                    */
#define BLK_E_TIMEOUT (-4)   /* the device did not answer in time               */
#define BLK_E_BUSY    (-5)   /* no room to submit now: nothing was written   */

/* Asynchronous request: the device takes it and completes it later, or the
 * device has no submit and the caller transfers synchronously. */
#define BLK_OP_READ  0u
#define BLK_OP_WRITE 1u
typedef struct BlkReq {
    uint64_t lba;      /* absolute first block on the device */
    uint32_t count;    /* blocks, at most 8 (one 4 KiB frame) */
    uint32_t op;       /* BLK_OP_READ or BLK_OP_WRITE */
    uint64_t phys;     /* physical address of the data frame */
    uint32_t ring;     /* ring id, used to route the completion */
    uint32_t slot;     /* ring slot, returned with the completion */
} BlkReq;

/* submit: 0 if the device took the request, XQ_ST_BUSY if it has no room,
 * another negative code if it refused. NULL means synchronous device. */
typedef struct BlkOps {
    int (*read)(void *ctx, uint64_t lba, uint32_t count, void *buf);
    int (*write)(void *ctx, uint64_t lba, uint32_t count, const void *buf);
    int (*flush)(void *ctx);
    int (*submit)(void *ctx, const BlkReq *req);
} BlkOps;

typedef struct BlkDev {
    void          *ctx;          /* driver state, passed back to every op          */
    const BlkOps  *ops;
    uint32_t       sector_size;  /* bytes per block, e.g. 512 or 4096              */
    uint64_t       sector_count; /* number of blocks                               */
} BlkDev;

int      blk_read(BlkDev *d, uint64_t lba, uint32_t count, void *buf);
int      blk_write(BlkDev *d, uint64_t lba, uint32_t count, const void *buf);
int      blk_flush(BlkDev *d);
uint32_t blk_sector_size(const BlkDev *d);
uint64_t blk_sector_count(const BlkDev *d);

#endif /* XASMOS_BLK_H */
