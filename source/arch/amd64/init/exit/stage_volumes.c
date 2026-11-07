/******************************************************************************
 * XASMOS - bootstrap stage: volumes
 * Path: source/arch/amd64/init/exit/stage_volumes.c
 *
 * Creates the boot volume (|dev|rd0) and publishes the system disk
 * (|dev|nd0) when one is present. Nothing here writes to a disk.
 ******************************************************************************/
#include "exfs.h"
#include "dev.h"
#include "ramdisk.h"
#include "storage.h"
#include "stage_volumes.h"

#define BOOT_VOLUME_BYTES (64u * 1024u * 1024u)

extern void xk_println(const char *s);

static uint8_t g_boot_mem[BOOT_VOLUME_BYTES];
static RamDisk g_boot;

int stage_volumes(void) {
    if (ramdisk_init(&g_boot, g_boot_mem, BOOT_VOLUME_BYTES, 512u) != BLK_OK) {
        xk_println("[EXIT ERROR] boot volume memory rejected");
        return -1;
    }
    if (dev_register("rd0", ramdisk_device(&g_boot)) != DEV_OK) {
        xk_println("[EXIT ERROR] cannot publish |dev|rd0");
        return -1;
    }
    BlkDev *boot = dev_lookup("|dev|rd0");
    if (!boot || exfs_format(boot) != EXFS_OK) {
        xk_println("[EXIT ERROR] boot volume format failed");
        return -1;
    }
    xk_println("[EXIT] boot volume |dev|rd0: exfs, 64 MiB, RAM");

    if (storage_open() == STORAGE_OK && storage_device() != 0) {
        if (dev_register("nd0", storage_device()) == DEV_OK) {
            xk_println("[EXIT] system disk |dev|nd0 registered");
        }
    } else {
        xk_println("[EXIT] no system disk: |dev|nd0 unavailable");
    }
    return 0;
}
