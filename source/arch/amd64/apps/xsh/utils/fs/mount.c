/******************************************************************************
 * XASMOS - Coreutil: mount <format> <device>
 *   mount exfs |dev|nd0      mounts the EXFS volume on the NVMe disk
 * The format and the device are always named explicitly; nothing is guessed.
 * Path: source/arch/amd64/apps/xsh/utils/fs/mount.c
 ******************************************************************************/
#include "common.h"
#include "args.h"
#include "dev.h"
#include "exfs.h"

static int same(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

int cmd_mount_main(const char *arg) {
    char fmt[16], path[64];
    const char *rest = next_word(arg, fmt, sizeof fmt);
    next_word(rest, path, sizeof path);
    if (fmt[0] == '\0' || path[0] == '\0') {
        xk_println("usage: mount exfs|vexfs |dev|<name>");
        return -1;
    }
    if (same(fmt, "vexfs")) {
        xk_println("EXNOTIMPL: vexfs is not available yet");
        return -1;
    }
    if (!same(fmt, "exfs")) {
        xk_print("EXBADARG: unknown format ");
        xk_println(fmt);
        return -1;
    }
    BlkDev *d = dev_lookup(path);
    if (!d) {
        xk_print("EXNOENT: no such device ");
        xk_println(path);
        return -1;
    }
    if (exfs_is_mounted()) {
        xk_println("EXBUSY: a volume is already mounted; run umount first");
        return -1;
    }
    int rc = exfs_mount(d);
    if (rc == EXFS_OK) {
        xk_print("mounted ");
        xk_print(path);
        xk_println(" as exfs");
        return 0;
    }
    if (rc == EXFS_ENOFS) {
        xk_print("EXNOFS: no exfs volume on ");
        xk_println(path);
        return -1;
    }
    xk_print("EXIOERR: mount failed, code ");
    put_dec((unsigned long long)(-rc));
    xk_println("");
    return -1;
}
