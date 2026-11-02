/******************************************************************************
 * XASMOS - Coreutil: fs. The persistent EXFS volume on the NVMe controller.
 *   fs mount      read the committed state from the disk (read-only)
 *   fs install    copy the embedded programs into |bin (RAM; then fs sync)
 *   fs sync       commit changes to the disk             (test disk only)
 *   fs format     write a new empty volume                (test disk only)
 * "Test disk only" means the controller's serial number is "xasmos", which
 * is what the QEMU test image reports. Any other disk refuses those commands
 * before a single write is sent.
 * Path: source/arch/amd64/apps/xsh/utils/fs.c
 ******************************************************************************/
#include "common.h"
#include "../../../drivers/pci/pci.h"
#include "../../../drivers/storage/nvme/nvme.h"
#include "../../../drivers/storage/exfs/store.h"
#include "../images/images.h"
#include <exfs.h>

#define TEST_SERIAL "xasmos"
#define FS_TYPE_FILE 0
#define BIN_PREFIX   "|bin|"

static BlkDev g_dev;
static int    g_have_dev;
static int    g_test_disk;

static int same_str(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

static int disk_up(void) {
    if (g_have_dev) return 0;
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
    g_have_dev = 1;

    char serial[24];
    g_test_disk = (nvme_controller_serial(serial, sizeof serial) == BLK_OK &&
                   same_str(serial, TEST_SERIAL));
    return 0;
}

static void print_code(const char *what, int rc) {
    xk_print(what);
    xk_print(", code ");
    put_dec((unsigned long long)(-rc));
    xk_println("");
}

static int cmd_mount(void) {
    if (disk_up() != 0) return -1;
    int rc = exfs_mount(&g_dev);
    if (rc == 0) {
        xk_println("fs: mounted the committed state");
        return 0;
    }
    if (rc == EXFS_ERR_NOFS) {
        xk_println("EXNOFS: no EXFS volume on this disk");
        return -1;
    }
    print_code("EXIOERR: mount failed", rc);
    return -1;
}

static int cmd_sync(void) {
    if (!exfs_mounted()) {
        xk_println("EXNOFS: nothing mounted; use 'fs mount' first");
        return -1;
    }
    if (!g_test_disk) {
        xk_println("REFUSED: this disk is not the test disk (serial xasmos)");
        return -1;
    }
    int rc = exfs_sync();
    if (rc == 0) {
        xk_println("fs: committed");
        return 0;
    }
    print_code("EXIOERR: commit failed, previous state intact", rc);
    return -1;
}

static int cmd_format(void) {
    if (disk_up() != 0) return -1;
    if (!g_test_disk) {
        xk_println("REFUSED: this disk is not the test disk (serial xasmos)");
        return -1;
    }
    int rc = exfs_format(&g_dev);
    if (rc == 0) {
        xk_println("fs: formatted; |user |root |bin |boot created and committed");
        return 0;
    }
    print_code("EXIOERR: format failed", rc);
    return -1;
}

static int cmd_install(void) {
    if (!exfs_mounted()) {
        xk_println("EXNOFS: nothing mounted; use 'fs mount' first");
        return -1;
    }
    int n = xsh_image_count();
    for (int i = 0; i < n; i++) {
        const XshImage *img = xsh_image_at(i);

        char path[64];
        int p = 0;
        for (const char *s = BIN_PREFIX; *s != '\0'; s++) path[p++] = *s;
        for (const char *s = img->name; *s != '\0' && p < 60; s++) path[p++] = *s;
        path[p] = '\0';

        if (exfs_stat(path) < 0 && exfs_create_object(path, FS_TYPE_FILE) != 0) {
            xk_print("EXFULL: no node for ");
            xk_println(path);
            return -1;
        }
        int rc = exfs_write_bytes(path, img->data, (int)img->len);
        if (rc != 0) {
            xk_print("EXIOERR: cannot store ");
            xk_println(path);
            return -1;
        }
        xk_print("installed ");
        xk_print(path);
        xk_print(" (");
        put_dec(img->len);
        xk_println(" bytes); run 'fs sync' to commit");
    }
    return 0;
}

int cmd_fs_main(const char *arg) {
    if (same_str(arg, "mount"))   return cmd_mount();
    if (same_str(arg, "sync"))    return cmd_sync();
    if (same_str(arg, "format"))  return cmd_format();
    if (same_str(arg, "install")) return cmd_install();
    xk_println("usage: fs mount | install | sync | format");
    return -1;
}
