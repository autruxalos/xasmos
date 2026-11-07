/******************************************************************************
 * XASMOS - Coreutil: stat. Shows the type and size of a node.
 * Path: source/arch/amd64/apps/xsh/utils/stat.c
 ******************************************************************************/
#include "common.h"
#include "exfs.h"

int cmd_stat_main(const char *path) {
    int type = exfs_stat(path);
    if (type < 0) return type;

    xk_print(type == EXFS_TYPE_DIR ? "dir   " : "file  ");
    xk_print(path);
    if (type == EXFS_TYPE_FILE) {
        xk_print("  size=");
        put_dec((unsigned long long)exfs_size(path));
        xk_print(" bytes");
    }
    xk_println("");
    return EXFS_OK;
}
