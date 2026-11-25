#ifndef XASMOS_USB_MSC_H
#define XASMOS_USB_MSC_H
#include <stdint.h>
#include "../../../../core/blk/blk.h"
typedef struct {
    int      active;
    uint32_t slot;
    uint8_t  port;
    uint8_t  speed;
    uint64_t blocks;
    uint32_t block_size;
} UsbMscInfo;
int usb_msc_init(UsbMscInfo *out);
int usb_msc_blkdev(BlkDev *out);
int usb_msc_read_blocks(uint64_t lba, uint16_t count, void *buf);
int usb_msc_write_blocks(uint64_t lba, uint16_t count, const void *buf);
#endif
