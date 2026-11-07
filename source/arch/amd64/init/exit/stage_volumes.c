/******************************************************************************
 * XASMOS - bootstrap stage: volumes
 * Path: source/arch/amd64/init/exit/stage_volumes.c
 *
 * Creates the boot volume (|dev|rd0) and publishes the system disk
 * (|dev|nd0) when one is present. Nothing here writes to a disk.
 *
 * Boot report: the boot volume's memory must lie inside the kernel image
 * range the allocator reserved. If it does not, the volume could be handed
 * out as page tables, so the stage fails.
 ******************************************************************************/
#include "exfs.h"
#include "dev.h"
#include "ramdisk.h"
#include "storage.h"
#include "stage_volumes.h"
#include "kmemory.h"

#define BOOT_VOLUME_BYTES (64u * 1024u * 1024u)

extern void xk_println(const char *s);
extern void xk_print(const char *s);
/* Hex printing for the boot report. shell's put_hex lives in xsh, which
 * is not linked into this stage, so the digits are written here. */
static void report_hex(UINT64 v, unsigned width) {
    static const char h[] = "0123456789ABCDEF";
    char b[17];
    unsigned i;
    for (i = 0; i < width; i++) {
        b[width - 1 - i] = h[v & 0xF];
        v >>= 4;
    }
    b[width] = '\0';
    xk_print(b);
}

static uint8_t g_boot_mem[BOOT_VOLUME_BYTES];
static RamDisk g_boot;

/* Reports where the volume lives and whether the reserved kernel image covers
 * it. Returns 1 if covered, 0 if not. */
static int boot_report(void) {
    UINT64 base = 0, size = 0;
    phys_image_range(&base, &size);

    UINT64 vol = (UINT64)(UINTN)g_boot_mem;
    UINT64 vol_end = vol + BOOT_VOLUME_BYTES;
    int covered = (size != 0 && vol >= base && vol_end <= base + size);

    xk_print("[EXIT] image reserved ");
    report_hex(base, 16);
    xk_print(" + ");
    report_hex(size, 8);
    xk_println("");

    xk_print("[EXIT] boot volume memory ");
    report_hex(vol, 16);
    xk_print(" + ");
    report_hex(BOOT_VOLUME_BYTES, 8);
    xk_println(covered ? " : inside the reserved image" : " : OUTSIDE the reserved image");
    return covered;
}

int stage_volumes(void) {
    if (ramdisk_init(&g_boot, g_boot_mem, BOOT_VOLUME_BYTES, 512u) != BLK_OK) {
        xk_println("[EXIT ERROR] boot volume memory rejected");
        return -1;
    }
    if (!boot_report()) {
        xk_println("[EXIT ERROR] boot volume is not covered by the reserved image");
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
