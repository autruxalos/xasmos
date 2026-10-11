/******************************************************************************
 * XASMOS - EXFS Filesystem Driver (Native '|' Delimiter Support)
 * Path: source|arch|amd64|drivers|storage|exfs.c
 ******************************************************************************/

#define MAX_NODES 128
#define MAX_PATH_LEN 256
#define TYPE_FILE 0
#define TYPE_DIR  1

typedef struct {
    char path[MAX_PATH_LEN];
    int type;      // TYPE_FILE or TYPE_DIR
    int active;    // 1 = Active, 0 = Deleted (EXFS 0xE5 equivalent)
} EXFSNode;

// Static metadata table for EXFS objects
static EXFSNode exfs_table[MAX_NODES];
static int exfs_initialized = 0;

void exfs_init(void) {
    if (exfs_initialized) return;
    for (int i = 0; i < MAX_NODES; i++) {
        exfs_table[i].active = 0;
    }
    // Set root node
    exfs_table[0].active = 1;
    exfs_table[0].type = TYPE_DIR;
    // Copy string safely without standard library string.h
    const char *root = "|";
    int j = 0;
    while (root[j] != '\0') {
        exfs_table[0].path[j] = root[j];
        j++;
    }
    exfs_table[0].path[j] = '\0';
    exfs_initialized = 1;
}

// Internal string copy helper
static void exfs_strcpy(char *dest, const char *src) {
    int i = 0;
    while (src[i] != '\0' && i < MAX_PATH_LEN - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

// Internal string equality helper
static int exfs_strequal(const char *s1, const char *s2) {
    int i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return 0;
        i++;
    }
    return s1[i] == s2[i];
}

/**
 * Creates a file or directory node using the strict '|' path format.
 * Called by the 'form' coreutil.
 */
int exfs_create_object(const char *raw_path, int type) {
    exfs_init();

    // Find first available slot
    int slot = -1;
    for (int i = 0; i < MAX_NODES; i++) {
        if (!exfs_table[i].active) {
            slot = i;
            break;
        }
    }

    if (slot == -1) return -1; // EXFS Table Full

    // Register node
    exfs_table[slot].active = 1;
    exfs_table[slot].type = type;
    exfs_strcpy(exfs_table[slot].path, raw_path);

    return 0; // Success
}

/**
 * Deletes/marks an object as deleted without needing file/dir flags.
 * Called by the 'remove' coreutil.
 */
int exfs_delete_object(const char *raw_path) {
    exfs_init();

    for (int i = 0; i < MAX_NODES; i++) {
        if (exfs_table[i].active && exfs_strequal(exfs_table[i].path, raw_path)) {
            exfs_table[i].active = 0; // Mark as deleted (similar to 0xE5)
            return 0;
        }
    }

    return -1; // Object not found in EXFS
}
