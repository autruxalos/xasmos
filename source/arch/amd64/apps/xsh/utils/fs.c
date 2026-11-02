}/******************************************************************************
 * XASMOS - Coreutil: fs. The persistent EXFS volume on the system disk.
 *   fs mount      read the committed state from the disk (read-only)
 *   fs install    copy the embedded programs into |bin (RAM; then fs sync)
 *   fs sync       commit changes to the disk             (test disk only)
 *   fs format     write a new empty volume                (test disk only)
 * Disk selection and the test-disk check live in drivers/storage/storage.c.
 * Path: source/arch/amd64/apps/xsh/utils/fs.c
 ******************************************************************************/
#include "common.h"
#include "../../../drivers/storage/storage.h"
#include "../../../drivers/storage/exfs/store.h"
#include "../images/images.h"
#include <exfs.h>

#define FS_TYPE_FILE 0
#define BIN_PREFIX   "|bin|"

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

/* Opens the system disk. Prints the reason on failure. */
static int disk_up(void) {
    int rc = storage_open();
    if (rc == STORAGE_OK) return 0;
    if (rc == STORAGE_ENODEV) {
        xk_println("EXNOENT: no storage device found");
        return -1;
    }
    print_code("EXIOERR: storage open failed", rc);
    return -1;
}

static int cmd_mount(void) {
    if (disk_up() != 0) return -1;
    int rc = exfs_mount(storage_device());
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
    if (!storage_is_test_disk()) {
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
    if (!storage_is_test_disk()) {
        xk_println("REFUSED: this disk is not the test disk (serial xasmos)");
        return -1;
    }
    int rc = exfs_format(storage_device());
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
        if (exfs_write_bytes(path, img->data, (int)img->len) != 0) {
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
