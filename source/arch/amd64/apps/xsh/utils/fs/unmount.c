/******************************************************************************
 * XASMOS - Coreutil: unmount. Confirms the pending changes, then unmounts.
 * Path: source/arch/amd64/apps/xsh/utils/fs/unmount.c
 ******************************************************************************/
#include "common.h"
#include "exfs.h"

int cmd_unmount_main(const char *arg) {
    (void)arg;
    if (!exfs_is_mounted()) {
        xk_println("EXNOFS: nothing is mounted");
        return -1;
    }
    int rc = exfs_unmount();
    if (rc == EXFS_OK) {
        xk_println("unmounted");
        return 0;
    }
    xk_print("EXIOERR: commit failed, volume still mounted, code ");
    put_dec((unsigned long long)(-rc));
    xk_println("");
    return -1;
}
