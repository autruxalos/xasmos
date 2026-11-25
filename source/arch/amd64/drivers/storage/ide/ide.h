#ifndef XASMOS_IDE_H
#define XASMOS_IDE_H
#include <stdint.h>
#include "../../../../core/blk/blk.h"
#include "../../pci/pci.h"
#include "ide_q.h"

typedef struct {
    int      active;
    uint16_t cmd_base;
    uint16_t ctrl_base;
    uint8_t  slave;
    uint64_t sectors;
    char     model[48];
} IdeInfo;

int     ide_probe_and_init(IdeInfo *out);
int     ide_init_channel(uint16_t cmd_base, uint16_t ctrl_base, uint8_t slave, BlkDev *out, IdeInfo *info_out);
BlkDev *storage_ide_device(void);
#endif
