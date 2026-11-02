/******************************************************************************
 * XASMOS - Coreutil: nvme. Checks the NVMe driver through the storage layer.
 *   nvme          read-only: opens the disk, reads sector 0
 *   nvme test     write test on sector 1, only on the test disk (serial xasmos)
 * Path: source/arch/amd64/apps/xsh/utils/nvme.c
 ******************************************************************************/
#include "common.h"
#include "../../../drivers/storage/storage.h"

static uint8_t g_sector[4096];
static uint8_t g_pattern[4096];

static int same_str(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

static void print_code(const char *what, int rc) {
    xk_print(what);
    xk_print(", code ");
    put_dec((unsigned long long)(-rc));
    xk_println("");
}

static int bring_up(void) {
    int rc = storage_open();
    if (rc == STORAGE_OK) {
        BlkDev *dev = storage_device();
        xk_print("nvme: sectors=");
        put_dec(blk_sector_count(dev));
        xk_print(" sector=");
        put_dec(blk_sector_size(dev));
        xk_println(" bytes");
        return 0;
    }
    if (rc == STORAGE_ENODEV) xk_println("EXNOENT: no NVMe controller on the PCI bus");
    else                      print_code("EXIOERR: nvme_init failed", rc);
    return -1;
}

static int read_probe(void) {
    int rc = blk_read(storage_device(), 0, 1, g_sector);
    if (rc != BLK_OK) {
        print_code("EXIOERR: read of sector 0 failed", rc);
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
    if (!storage_is_test_disk()) {
        xk_println("REFUSED: the write test runs only on the test disk (serial xasmos)");
        return -1;
    }

    BlkDev *dev = storage_device();
    uint32_t ss = blk_sector_size(dev);
    if (ss == 0 || ss > sizeof g_pattern) {
        xk_println("EXBADARG: sector size not supported by the test");
        return -1;
    }
    for (uint32_t i = 0; i < ss; i++) {
        g_pattern[i] = (uint8_t)(i * 131u + 7u);
    }

    int rc = blk_write(dev, 1, 1, g_pattern);
    if (rc != BLK_OK) { xk_println("FAIL write sector 1"); return -1; }
    rc = blk_flush(dev);
    if (rc != BLK_OK) { xk_println("FAIL flush"); return -1; }
    rc = blk_read(dev, 1, 1, g_sector);
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
    if (bring_up() != 0)           return -1;
    if (arg[0] == '\0')            return read_probe();
    if (same_str(arg, "test"))     return write_test();
    xk_println("usage: nvme [test]");
    return -1;
}
