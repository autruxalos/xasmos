#define MAX_NODES 128
#define MAX_PATH_LEN 256
#define TYPE_FILE 0
#define TYPE_DIR  1

#define EXFS_DATA_MAX 192

typedef struct {
    char path[MAX_PATH_LEN];
    int type;
    int active;
    char data[EXFS_DATA_MAX];   /* inline storage -- RAM-only, no persistence yet */
    int data_len;
} EXFSNode;

static EXFSNode exfs_table[MAX_NODES];
static int exfs_initialized = 0;

void exfs_init(void) {
    if (exfs_initialized) return;
    for (int i = 0; i < MAX_NODES; i++) {
        exfs_table[i].active = 0;
    }
    exfs_table[0].active = 1;
    exfs_table[0].type = TYPE_DIR;
    const char *root = "|";
    int j = 0;
    while (root[j] != '\0') {
        exfs_table[0].path[j] = root[j];
        j++;
    }
    exfs_table[0].path[j] = '\0';
    exfs_initialized = 1;
}

static void exfs_strcpy(char *dest, const char *src) {
    int i = 0;
    while (src[i] != '\0' && i < MAX_PATH_LEN - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static int exfs_strequal(const char *s1, const char *s2) {
    int i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return 0;
        i++;
    }
    return s1[i] == s2[i];
}

int exfs_create_object(const char *raw_path, int type) {
    exfs_init();
    int slot = -1;
    for (int i = 0; i < MAX_NODES; i++) {
        if (!exfs_table[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1;
    exfs_table[slot].active = 1;
    exfs_table[slot].type = type;
    exfs_strcpy(exfs_table[slot].path, raw_path);
    exfs_table[slot].data[0] = '\0';
    exfs_table[slot].data_len = 0;
    return 0;
}

int exfs_delete_object(const char *raw_path) {
    exfs_init();
    for (int i = 0; i < MAX_NODES; i++) {
        if (exfs_table[i].active && exfs_strequal(exfs_table[i].path, raw_path)) {
            exfs_table[i].active = 0;
            return 0;
        }
    }
    return -1;
}

/* ===== NEW: appended for the shell (exfs_stat, exfs_enum) ================ */

/* Returns -1 if no active node has exactly this path, else TYPE_FILE/TYPE_DIR. */
int exfs_stat(const char *raw_path) {
    exfs_init();
    for (int i = 0; i < MAX_NODES; i++) {
        if (exfs_table[i].active && exfs_strequal(exfs_table[i].path, raw_path)) {
            return exfs_table[i].type;
        }
    }
    return -1;
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

/* ===== NEW: data storage for files (RAM-only, capped at EXFS_DATA_MAX-1) ==
 * Not yet backed by the physical allocator / token_ledger -- that is the
 * next step, once paging and real page ownership are in place. */

/* Returns 0 on success, -1 if the path has no active FILE node, -2 if the
 * text was truncated to fit EXFS_DATA_MAX-1 bytes (data is still stored). */
int exfs_write_data(const char *path, const char *text) {
    exfs_init();
    for (int i = 0; i < MAX_NODES; i++) {
        if (!exfs_table[i].active || exfs_table[i].type != TYPE_FILE) continue;
        if (!exfs_strequal(exfs_table[i].path, path)) continue;

        int n = 0;
        while (text[n] != '\0' && n < EXFS_DATA_MAX - 1) {
            exfs_table[i].data[n] = text[n];
            n++;
        }
        exfs_table[i].data[n] = '\0';
        exfs_table[i].data_len = n;
        return (text[n] != '\0') ? -2 : 0;
    }
    return -1;
}

/* Returns the stored length (0 or more) on success, -1 if the path has no
 * active FILE node. out is always null-terminated on success. */
int exfs_read_data(const char *path, char *out, int max) {
    exfs_init();
    for (int i = 0; i < MAX_NODES; i++) {
        if (!exfs_table[i].active || exfs_table[i].type != TYPE_FILE) continue;
        if (!exfs_strequal(exfs_table[i].path, path)) continue;

        int n = 0;
        while (n < exfs_table[i].data_len && n < max - 1) {
            out[n] = exfs_table[i].data[n];
            n++;
        }
        out[n] = '\0';
        return n;
    }
    return -1;
}
