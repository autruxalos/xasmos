/******************************************************************************
 * XASMOS - Coreutil: install. Copies the programs built into the system
 * image to |bin. Writes to the mounted volume; run sync to make it durable.
 * Path: source/arch/amd64/apps/xsh/utils/fs/install.c
 ******************************************************************************/
#include "common.h"
#include "images.h"
#include "exfs.h"

#define BIN_PREFIX "|bin|"

int cmd_install_main(const char *arg) {
    (void)arg;
    if (!exfs_is_mounted()) {
        xk_println("EXNOFS: mount a volume first");
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

        if (exfs_stat(path) == EXFS_ENOENT && exfs_create(path) != EXFS_OK) {
            xk_print("EXFULL: cannot create ");
            xk_println(path);
            return -1;
        }
        if (exfs_write(path, img->data, (uint64_t)img->len) != EXFS_OK) {
            xk_print("EXIOERR: cannot store ");
            xk_println(path);
            return -1;
        }
        xk_print("installed ");
        xk_print(path);
        xk_print(" (");
        put_dec(img->len);
        xk_println(" bytes)");
    }
    return 0;
}
