/******************************************************************************
 * XASMOS - EXFS path helpers and directory listing
 * Path: source/arch/amd64/drivers/storage/exfs/path.c
 *
 * Paths use '|' as the delimiter: "|home|xent". A child of "|home" is
 * "|home|x"; "|homework" is NOT under "|home".
 ******************************************************************************/
#include "exfs_internal.h"

void exfs_strcpy(char *dest, const char *src) {
    int i = 0;
    while (src[i] != '\0' && i < MAX_PATH_LEN - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

int exfs_strequal(const char *s1, const char *s2) {
    int i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return 0;
        i++;
    }
    return s1[i] == s2[i];
}

/* Calls cb(name, type) for every DIRECT child of dir. Returns the child count. */
int exfs_enum(const char *dir, void (*cb)(const char *name, int type)) {
    exfs_init();
    int dlen = 0;
    while (dir[dlen] != '\0') dlen++;
    int root = (dlen == 1 && dir[0] == '|');
    int count = 0;

    for (int i = 0; i < MAX_NODES; i++) {
        if (!exfs_table[i].active) continue;
        const char *p = exfs_table[i].path;

        int k = 0;
        while (k < dlen && p[k] == dir[k]) k++;
        if (k != dlen) continue;                 /* dir is not a prefix */

        const char *rest = p + dlen;
        if (!root) {
            if (rest[0] != '|') continue;        /* "|homework" is not under "|home" */
            rest++;
        }
        if (rest[0] == '\0') continue;           /* the directory itself */

        int nested = 0;
        for (int j = 0; rest[j] != '\0'; j++) {
            if (rest[j] == '|') nested = 1;
        }
        if (nested) continue;                    /* grandchild, not direct */

        cb(rest, exfs_table[i].type);
        count++;
    }
    return count;
}
