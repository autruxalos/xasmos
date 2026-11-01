/******************************************************************************
 * XASMOS - block device interface: range checks and dispatch (portable)
 * Path: source/core/blk/blk.c
 ******************************************************************************/
#include "blk.h"

static int check(const BlkDev *d, uint64_t lba, uint32_t count, const void *buf) {
    if (!d || !d->ops || !buf || count == 0) return BLK_E_ARG;
    if (lba > d->sector_count) return BLK_E_RANGE;
    if (count > d->sector_count - lba) return BLK_E_RANGE;   /* no overflow: lba <= sector_count */
    return BLK_OK;
}

int blk_read(BlkDev *d, uint64_t lba, uint32_t count, void *buf) {
    int rc = check(d, lba, count, buf);
    if (rc != BLK_OK) return rc;
    if (!d->ops->read) return BLK_E_IO;
    return d->ops->read(d->ctx, lba, count, buf);
}

int blk_write(BlkDev *d, uint64_t lba, uint32_t count, const void *buf) {
    int rc = check(d, lba, count, buf);
    if (rc != BLK_OK) return rc;
    if (!d->ops->write) return BLK_E_IO;
    return d->ops->write(d->ctx, lba, count, buf);
}

int blk_flush(BlkDev *d) {
    if (!d || !d->ops) return BLK_E_ARG;
    if (!d->ops->flush) return BLK_OK;   /* a device without a volatile cache */
    return d->ops->flush(d->ctx);
}

uint32_t blk_sector_size(const BlkDev *d)  { return d ? d->sector_size  : 0; }
uint64_t blk_sector_count(const BlkDev *d) { return d ? d->sector_count : 0; }
