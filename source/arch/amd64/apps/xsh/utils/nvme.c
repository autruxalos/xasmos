/******************************************************************************
 * XASMOS - Coreutil: nvme. Brings up the NVMe controller and reads sector 0.
 * READ-ONLY: it never writes to the device. Use it to check the driver before
 * anything else touches the disk.
 * Path: source/arch/amd64/apps/xsh/utils/nvme.c
 ******************************************************************************/
#include "common.h"
#include "../../../drivers/pci/pci.h"
#include "../../../drivers/storage/nvme/nvme.h"

static BlkDev g_dev;
static uint8_t g_sector[4096];

int cmd_nvme_main(void) {
    pci_scan();
    const PciDev *d = pci_find(0x01, 0x08, PCI_ANY_PROGIF, 0);
    if (!d) {
        xk_println("EXNOENT: no NVMe controller on the PCI bus");
        return -1;
    }

    int rc = nvme_init(d, &g_dev);
    if (rc != BLK_OK) {
        xk_print("EXIOERR: nvme_init failed, code ");
        put_dec((unsigned long long)(-rc));
        xk_println("");
        return -1;
    }

    xk_print("nvme: sectors=");
    put_dec(blk_sector_count(&g_dev));
    xk_print(" sector=");
    put_dec(blk_sector_size(&g_dev));
    xk_println(" bytes");

    rc = blk_read(&g_dev, 0, 1, g_sector);
    if (rc != BLK_OK) {
        xk_print("EXIOERR: read of sector 0 failed, code ");
        put_dec((unsigned long long)(-rc));
        xk_println("");
        return -1;
    }

    xk_print("sector 0:");
    for (int i = 0; i < 16; i++) {
        xk_print(" ");
        put_hex(g_sector[i], 2);
    }
    xk_println("");
    return 0;
}
