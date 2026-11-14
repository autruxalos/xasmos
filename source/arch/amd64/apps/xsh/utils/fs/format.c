/******************************************************************************
 * XASMOS - Coreutil: format <format> <device> <device> <serial>
 *   format exfs |dev|nd0 |dev|nd0 xasmos
 * The device is named twice, and its serial number is given as a third
 * argument. Formatting destroys everything on the device, so any mismatch
 * refuses before a single block is written. A device with no recorded serial
 * can never be formatted.
 * Path: source/arch/amd64/apps/xsh/utils/fs/format.c
 ******************************************************************************/
#include "common.h"
#include "args.h"
#include "dev.h"
#include "exfs.h"

static int same(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

int cmd_format_main(const char *arg) {
    char fmt[16], path[64], confirm[64], serial_arg[DEV_SERIAL_MAX + 1];
    const char *rest = next_word(arg, fmt, sizeof fmt);
    rest = next_word(rest, path, sizeof path);
    rest = next_word(rest, confirm, sizeof confirm);
    next_word(rest, serial_arg, sizeof serial_arg);

    if (fmt[0] == '\0' || path[0] == '\0' || confirm[0] == '\0' || serial_arg[0] == '\0') {
        xk_println("usage: format exfs|vexfs |dev|<name> |dev|<name> <serial>");
        xk_println("the device is repeated, and its serial given, to confirm: formatting destroys its content");
        return -1;
    }
    if (!same(path, confirm)) {
        xk_println("REFUSED: the device name does not match the confirmation; nothing was written");
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

    const char *recorded = dev_serial(path);
    if (!recorded) {
        xk_println("REFUSED: this device has no recorded serial; it cannot be formatted");
        return -1;
    }
    if (!same(recorded, serial_arg)) {
        xk_print("REFUSED: serial mismatch, the device reports ");
        xk_println(recorded);
        xk_println("nothing was written");
        return -1;
    }

    if (exfs_is_mounted()) {
        xk_println("EXBUSY: a volume is mounted; run unmount before formatting");
        return -1;
    }

    int rc = exfs_format(d);
    if (rc == EXFS_OK) {
        xk_print("formatted ");
        xk_print(path);
        xk_println(" as exfs; |user |root |bin |boot created");
        return 0;
    }
    xk_print("EXIOERR: format failed, code ");
    put_dec((unsigned long long)(-rc));
    xk_println("");
    return -1;
}
