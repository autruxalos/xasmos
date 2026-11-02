/******************************************************************************
 * XASMOS - storage: opens the system disk and records whether it is the test disk
 * Path: source/arch/amd64/drivers/storage/storage.c
 ******************************************************************************/
#include "../pci/pci.h"
#include "nvme/nvme.h"
#include "storage.h"

#define TEST_SERIAL "xasmos"

static BlkDev g_dev;
static int    g_open;
static int    g_test;

static int same_str(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

int storage_open(void) {
    if (g_open) return STORAGE_OK;

    pci_scan();
    const PciDev *d = pci_find(0x01, 0x08, PCI_ANY_PROGIF, 0);
    if (!d) return STORAGE_ENODEV;

    int rc = nvme_init(d, &g_dev);
    if (rc != BLK_OK) return rc;

    char serial[24];
    g_test = (nvme_controller_serial(serial, sizeof serial) == BLK_OK &&
              same_str(serial, TEST_SERIAL));
    g_open = 1;
    return STORAGE_OK;
}

BlkDev *storage_device(void) {
    return g_open ? &g_dev : 0;
}

int storage_is_test_disk(void) {
    return g_open && g_test;
}
