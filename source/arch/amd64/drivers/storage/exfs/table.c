/******************************************************************************
 * XASMOS - EXFS node table: create, delete, stat, lookup, system directories
 * Path: source/arch/amd64/drivers/storage/exfs/table.c
 *
 * The root "|" is node 0 and is never deleted. The four system directories
 * |user, |root, |bin and |boot exist from the first boot on.
 ******************************************************************************/
#include "exfs_internal.h"

EXFSNode exfs_table[MAX_NODES];
int      exfs_table_dirty = 0;
static int exfs_initialized = 0;

static void node_clear(EXFSNode *n) {
    n->active     = 0;
    n->type       = TYPE_FILE;
    n->data_tok   = XTOKEN_INVALID;
    n->data_len   = 0;
    n->data_dirty = 0;
    n->disk_block = 0;
    n->path[0]    = '\0';
}

static void root_set(void) {
    exfs_table[0].active  = 1;
    exfs_table[0].type    = TYPE_DIR;
    exfs_table[0].path[0] = '|';
    exfs_table[0].path[1] = '\0';
}

static int node_add(const char *raw_path, int type) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (!exfs_table[i].active) {
            exfs_table[i].active     = 1;
            exfs_table[i].type       = type;
            exfs_table[i].data_tok   = XTOKEN_INVALID;
            exfs_table[i].data_len   = 0;
            exfs_table[i].data_dirty = 0;
            exfs_table[i].disk_block = 0;
            exfs_strcpy(exfs_table[i].path, raw_path);
            exfs_table_dirty = 1;
            return 0;
        }
    }
    return -1;
}

void exfs_clear(void) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (exfs_table[i].data_tok != XTOKEN_INVALID) {
            (void)exfs_data_release(&exfs_table[i]);
        }
        node_clear(&exfs_table[i]);
    }
    root_set();
    exfs_table_dirty = 1;
}

/* Clears the table and seeds the system layout. Used at boot and by format. */
void exfs_reset_system(void) {
    exfs_clear();
    (void)node_add("|user", TYPE_DIR);
    (void)node_add("|root", TYPE_DIR);
    (void)node_add("|bin",  TYPE_DIR);
    (void)node_add("|boot", TYPE_DIR);
    exfs_initialized = 1;
}

void exfs_init(void) {
    if (exfs_initialized) return;
    exfs_reset_system();
}

/* Marks the table as initialised, so later calls do not reseed it. Mount uses
 * this after loading a committed table. */
void exfs_init_mark(void) {
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
    return node_add(raw_path, type);
}

/* Returns 0 on success, -1 if absent or if the content could not be
 * released. On failure the node stays intact. */
int exfs_delete_object(const char *raw_path) {
    exfs_init();
    EXFSNode *n = exfs_find(raw_path);
    if (!n) return -1;
    if (exfs_data_release(n) != 0) return -1;
    node_clear(n);
    exfs_table_dirty = 1;
    return 0;
}

int exfs_stat(const char *raw_path) {
    exfs_init();
    EXFSNode *n = exfs_find(raw_path);
    return n ? n->type : -1;
}
