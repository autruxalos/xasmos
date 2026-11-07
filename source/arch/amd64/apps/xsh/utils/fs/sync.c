/******************************************************************************
 * XASMOS - Coreutil: sync. Confirms the pending changes of the mounted volume.
 * Path: source/arch/amd64/apps/xsh/utils/fs/sync.c
 ******************************************************************************/
#include "common.h"
#include "exfs.h"

int cmd_sync_main(const char *arg) {
    (void)arg;
    if (!exfs_is_mounted()) {
        xk_println("EXNOFS: nothing is mounted");
        return -1;
    }
    int rc = exfs_commit();
    if (rc == EXFS_OK) {
        xk_println("committed");
        return 0;
    }
    xk_print("EXIOERR: commit failed, previous state intact, code ");
    put_dec((unsigned long long)(-rc));
    xk_println("");
    return -1;
}
