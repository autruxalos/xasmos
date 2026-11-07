/******************************************************************************
 * XASMOS - Coreutil: form. Creates a file or a directory.
 * Path: source/arch/amd64/apps/xsh/utils/form.c
 ******************************************************************************/
#include "common.h"
#include "exfs.h"

static int same(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

/* Returns an EXFS code; the shell reports errors. */
int cmd_form_main(const char *flag, const char *path) {
    if (same(flag, "-file")) return exfs_create(path);
    if (same(flag, "-dir"))  return exfs_mkdir(path);
    return EXFS_EINVAL;
}
