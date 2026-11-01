/******************************************************************************
 * XASMOS - EXFS node table: create, delete, stat, lookup
 * Path: source/arch/amd64/drivers/storage/exfs/table.c
 *
 * The root "|" is node 0 and is never deleted. Requires xtok_init() first:
 * a node's frame is released through the ledger when the node is deleted.
 ******************************************************************************/
#include "exfs_internal.h"

EXFSNode exfs_table[MAX_NODES];
static int exfs_initialized = 0;

void exfs_init(void) {
    if (exfs_initialized) return;
    for (int i = 0; i < MAX_NODES; i++) {
        exfs_table[i].active   = 0;
        exfs_table[i].data_tok = XTOKEN_INVALID;
        exfs_table[i].data_len = 0;
    }
    exfs_table[0].active  = 1;
    exfs_table[0].type    = TYPE_DIR;
    exfs_table[0].path[0] = '|';
    exfs_table[0].path[1] = '\0';
    exfs_initialized = 1;
}

EXFSNode *exfs_find(const char *path) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (exfs_table[i].active && exfs_strequal(exfs_table[i].path, path)) {
            return &exfs_table[i];
        }
    }
    return 0;
}

int exfs_create_object(const char *raw_path, int type) {
    exfs_init();
    for (int i = 0; i < MAX_NODES; i++) {
        if (!exfs_table[i].active) {
            exfs_table[i].active   = 1;
            exfs_table[i].type     = type;
            exfs_table[i].data_tok = XTOKEN_INVALID;   /* no frame until first write */
            exfs_table[i].data_len = 0;
            exfs_strcpy(exfs_table[i].path, raw_path);
            return 0;
        }
    }
    return -1;   /* node table full */
}

/* Returns 0 on success, -1 if absent or if the frame could not be returned.
 * On failure the node stays intact, so a frame is never leaked silently. */
int exfs_delete_object(const char *raw_path) {
    exfs_init();
    EXFSNode *n = exfs_find(raw_path);
    if (!n) return -1;
    if (exfs_data_release(n) != 0) return -1;
    n->data_len = 0;
    n->active   = 0;
    return 0;
}

/* Returns -1 if no active node has exactly this path, else TYPE_FILE/TYPE_DIR. */
int exfs_stat(const char *raw_path) {
    exfs_init();
    EXFSNode *n = exfs_find(raw_path);
    return n ? n->type : -1;
}
