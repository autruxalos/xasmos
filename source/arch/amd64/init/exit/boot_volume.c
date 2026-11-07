/******************************************************************************
 * XASMOS - boot volume and system disk registration
 * Path: source/arch/amd64/init/exit/boot_volume.c
 *
 * The boot volume lives in memory and lasts until the next boot. Nothing here
 * writes to a disk. The system disk is only opened and published.
 ******************************************************************************/
#include "exfs.h"
#include "dev.h"
#include "ramdisk.h"
#include "storage.h"
#include "boot_volume.h"

#define BOOT_VOLUME_BYTES (64u * 1024u * 1024u)

extern int xk_println(const char *s);

static uint8_t g_boot_mem[BOOT_VOLUME_BYTES];
static RamDisk g_boot;

int boot_volume_init(void) {
    if (ramdisk_init(&g_boot, g_boot_mem, BOOT_VOLUME_BYTES, 512u) != BLK_OK) {
        xk_println("EXBOOT: boot volume memory rejected");
        return -1;
    }
    if (dev_register("rd0", ramdisk_device(&g_boot)) != DEV_OK) {
        xk_println("EXBOOT: cannot publish |dev|rd0");
        return -1;
    }
    BlkDev *boot = dev_lookup("|dev|rd0");
    if (!boot || exfs_format(boot) != EXFS_OK) {
        xk_println("EXBOOT: boot volume format failed");
        return -1;
    }
    xk_println("boot volume |dev|rd0: exfs, 64 MiB, RAM (lost at shutdown)");

    if (storage_open() == STORAGE_OK && storage_device() != 0) {
        if (dev_register("nd0", storage_device()) == DEV_OK) {
            xk_println("system disk |dev|nd0 registered");
        }
    } else {
        xk_println("no system disk: |dev|nd0 unavailable");
    }
    return 0;
}
