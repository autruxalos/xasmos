/******************************************************************************
 * XASMOS - Coreutil: nvme.
 *   nvme          read-only: brings the controller up, reads sector 0
 *   nvme test     write test on sector 1, only if the controller's serial
 *                 number is "xasmos" (the QEMU test image). Anything else is
 *                 refused before a single write is sent.
 * Path: source/arch/amd64/apps/xsh/utils/nvme.c
 ******************************************************************************/
#include "common.h"
#include "../../../drivers/pci/pci.h"
#include "../../../drivers/storage/nvme/nvme.h"

#define TEST_SERIAL "xasmos"

static BlkDev  g_dev;
static uint8_t g_sector[4096];
static uint8_t g_pattern[4096];

static int same_str(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

static int bring_up(void) {
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
    return 0;
}

static int read_probe(void) {
    int rc = blk_read(&g_dev, 0, 1, g_sector);
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

static int write_test(void) {
    char serial[24];
    int rc = nvme_controller_serial(serial, sizeof serial);
    if (rc != BLK_OK) {
        xk_print("EXIOERR: identify controller failed, code ");
        put_dec((unsigned long long)(-rc));
        xk_println("");
        return -1;
    }
    xk_print("controller serial: ");
    xk_println(serial);
    if (!same_str(serial, TEST_SERIAL)) {
        xk_println("REFUSED: the write test runs only on the QEMU test image (serial xasmos)");
        return -1;
    }

    uint32_t ss = blk_sector_size(&g_dev);
    if (ss == 0 || ss > sizeof g_pattern) {
        xk_println("EXBADARG: sector size not supported by the test");
        return -1;
    }
    for (uint32_t i = 0; i < ss; i++) {
        g_pattern[i] = (uint8_t)(i * 131u + 7u);
    }

    rc = blk_write(&g_dev, 1, 1, g_pattern);
    if (rc != BLK_OK) { xk_println("FAIL write sector 1"); return -1; }
    rc = blk_flush(&g_dev);
    if (rc != BLK_OK) { xk_println("FAIL flush"); return -1; }
    rc = blk_read(&g_dev, 1, 1, g_sector);
    if (rc != BLK_OK) { xk_println("FAIL read sector 1"); return -1; }

    for (uint32_t i = 0; i < ss; i++) {
        if (g_sector[i] != g_pattern[i]) {
            xk_print("FAIL byte ");
            put_dec(i);
            xk_print(" wrote ");
            put_hex(g_pattern[i], 2);
            xk_print(" read ");
            put_hex(g_sector[i], 2);
            xk_println("");
            return -1;
        }
    }
    xk_print("PASS write/flush/read sector 1, ");
    put_dec(ss);
    xk_println(" bytes match");
    return 0;
}

int cmd_nvme_main(const char *arg) {
    if (bring_up() != 0) return -1;
    if (arg[0] == '\0')           return read_probe();
    if (same_str(arg, "test"))    return write_test();
    xk_println("usage: nvme [test]");
    return -1;
}
