/* XASMOS - Coreutil: stat. Type and size of a node. The caller resolves the
 * path; this only reports what EXFS knows about it. */
#include "common.h"
#include <exfs.h>

extern int exfs_stat(const char *raw_path);
extern int exfs_size(const char *raw_path);

int cmd_stat_main(const char *path) {
    int type = exfs_stat(path);
    if (type < 0) {
        xk_print("EXNOENT: path unresolved: ");
        xk_println(path);
        return -1;
    }
    xk_print(path);
    if (type == 1) {                      /* TYPE_DIR, see exfs.c */
        xk_println("  type=dir");
        return 0;
    }
    xk_print("  type=file  size=");
    put_dec((unsigned long long)exfs_size(path));
    xk_println(" bytes");
    return 0;
}
