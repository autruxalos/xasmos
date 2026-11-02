/******************************************************************************
 * XASMOS - RAM block device implementation
 * Path: source/core/blk/ramdisk.c
 ******************************************************************************/
#include "ramdisk.h"

static int rd_read(void *c, uint64_t lba, uint32_t n, void *buf) {
    RamDisk *r = (RamDisk *)c;
    uint64_t bytes = (uint64_t)n * r->sector_size;
    uint8_t *src = r->mem + lba * r->sector_size;
    uint8_t *dst = (uint8_t *)buf;
    for (uint64_t i = 0; i < bytes; i++) dst[i] = src[i];
    return BLK_OK;
}

static int rd_write(void *c, uint64_t lba, uint32_t n, const void *buf) {
    RamDisk *r = (RamDisk *)c;
    uint64_t bytes = (uint64_t)n * r->sector_size;
    uint8_t *dst = r->mem + lba * r->sector_size;
    const uint8_t *src = (const uint8_t *)buf;
    for (uint64_t i = 0; i < bytes; i++) dst[i] = src[i];
    return BLK_OK;
}

static int rd_flush(void *c) {
    (void)c;
    return BLK_OK;                      /* memory is its own stable storage */
}

int ramdisk_init(RamDisk *r, void *mem, uint64_t bytes, uint32_t sector_size) {
    if (!r || !mem) return BLK_E_ARG;
    if (sector_size < 512 || sector_size > 4096 || (sector_size & (sector_size - 1)) != 0) return BLK_E_ARG;
    if (bytes == 0 || bytes % sector_size != 0) return BLK_E_ARG;

    r->mem = (uint8_t *)mem;
    r->sector_size = sector_size;
    r->sector_count = bytes / sector_size;
    for (uint64_t i = 0; i < bytes; i++) r->mem[i] = 0;

    r->ops.read = rd_read;
    r->ops.write = rd_write;
    r->ops.flush = rd_flush;
    r->dev.ctx = r;
    r->dev.ops = &r->ops;
    r->dev.sector_size = sector_size;
    r->dev.sector_count = r->sector_count;
    return BLK_OK;
}

BlkDev *ramdisk_device(RamDisk *r) {
    return r ? &r->dev : 0;
}
