#ifndef XASMOS_AHCI_H
#define XASMOS_AHCI_H
#include <stdint.h>
#include "../../../../core/blk/blk.h"
#include "../../pci/pci.h"
#include "ahci_q.h"

typedef struct {
    int      active;
    uint8_t  port;
    uint32_t slots;
    uint64_t sectors;
    char     model[48];
} AhciInfo;

int     ahci_probe_and_init(AhciInfo *out);
int     ahci_init(const PciDev *d, BlkDev *out, AhciInfo *info_out);
BlkDev *storage_ahci_device(void);
#endif
