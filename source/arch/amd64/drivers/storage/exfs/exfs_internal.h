/******************************************************************************
 * XASMOS - EXFS internal interface (shared by table.c, path.c, data.c)
 * Path: source/arch/amd64/drivers/storage/exfs/exfs_internal.h
 *
 * Not for callers outside this directory. Everything else goes through exfs.h.
 ******************************************************************************/
#ifndef XASMOS_EXFS_INTERNAL_H
#define XASMOS_EXFS_INTERNAL_H

#include "capability.h"
#include "exfs.h"

#define MAX_NODES    128
#define MAX_PATH_LEN 256
#define TYPE_FILE    0
#define TYPE_DIR     1

typedef struct {
    char   path[MAX_PATH_LEN];
    int    type;
    int    active;
    XToken data_tok;     /* XTOKEN_INVALID until the first write */
    int    data_len;
} EXFSNode;

extern EXFSNode exfs_table[MAX_NODES];

/* table.c */
EXFSNode *exfs_find(const char *path);            /* active node with this exact path, or 0 */

/* path.c */
void exfs_strcpy(char *dest, const char *src);
int  exfs_strequal(const char *s1, const char *s2);

/* data.c: returns the node's frame to the ledger. 0 on success (or nothing
 * to return); -1 if the ledger refuses, and then the node keeps its frame. */
int  exfs_data_release(EXFSNode *n);

#endif /* XASMOS_EXFS_INTERNAL_H */
