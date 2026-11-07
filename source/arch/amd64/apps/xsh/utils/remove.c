/******************************************************************************
 * XASMOS - Coreutil: remove. Deletes a file or an empty directory.
 * Path: source/arch/amd64/apps/xsh/utils/remove.c
 ******************************************************************************/
#include "exfs.h"

/* Returns an EXFS code; the shell reports errors. */
int cmd_remove_main(const char *path) {
    return exfs_delete(path);
}
