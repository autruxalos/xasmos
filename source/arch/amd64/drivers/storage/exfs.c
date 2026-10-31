/******************************************************************************
 * XASMOS - EXFS Filesystem Driver (native '|' delimiter)
 * Path: source|arch|amd64|drivers|storage|exfs.c
 *
 * File data now lives in 4 KiB frames sealed by the token ledger, not in
 * kernel-static buffers. A file owns a frame only after its first write;
 * an empty file costs no memory. Overwrites reuse the same frame, and
 * exfs_delete_object() hands it back via xtok_release().
 *
 * Requires phys_init_from_handoff() and xtok_init() to have run first.
 *
 * KNOWN LIMIT: the frame's physical address is dereferenced directly. That
 * is only valid while the firmware's identity map is active. Once xeret
 * installs its own page tables, access must go through the token's MAP
 * right instead of a raw physical pointer.
 ******************************************************************************/
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

static EXFSNode exfs_table[MAX_NODES];
static int      exfs_initialized = 0;

void exfs_init(void) {
    if (exfs_initialized) return;
    for (int i = 0; i < MAX_NODES; i++) {
        exfs_table[i].active   = 0;
        exfs_table[i].data_tok = XTOKEN_INVALID;
        exfs_table[i].data_len = 0;
    }
    exfs_table[0].active = 1;
    exfs_table[0].type   = TYPE_DIR;
    exfs_table[0].path[0] = '|';
    exfs_table[0].path[1] = '\0';
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

/* Active node with exactly this path, or 0. */
static EXFSNode *exfs_find(const char *path) {
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

    if (n->data_tok != XTOKEN_INVALID) {
        if (xtok_release(n->data_tok) != 0) return -1;
        n->data_tok = XTOKEN_INVALID;
    }
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

/* Replaces the file's whole content with text (up to EXFS_DATA_MAX-1 bytes).
 * Returns 0 on success; EXFS_ERR_TRUNC if the text was cut to fit (the part
 * that fits IS stored); EXFS_ERR_NOTFILE, EXFS_ERR_NOMEM or EXFS_ERR_DENIED
 * on failure, in which case the file's previous content is untouched. */
int exfs_write_data(const char *path, const char *text) {
    exfs_init();
    EXFSNode *n = exfs_find(path);
    if (!n || n->type != TYPE_FILE) return EXFS_ERR_NOTFILE;

    if (n->data_tok == XTOKEN_INVALID) {
        n->data_tok = xtok_alloc_ram(XTOK_OWNER_KERNEL, 1, XTOK_RIGHTS_RAM_DEFAULT);
        if (n->data_tok == XTOKEN_INVALID) return EXFS_ERR_NOMEM;
        n->data_len = 0;
    }

    UINT64 phys = 0;
    if (!xtok_validate(n->data_tok, XTOK_RIGHT_WRITE) ||
        xtok_ram_info(n->data_tok, &phys, 0) != 0) {
        return EXFS_ERR_DENIED;
    }

    char *page = (char *)(UINTN)phys;
    int i = 0;
    while (text[i] != '\0' && i < EXFS_DATA_MAX - 1) {
        page[i] = text[i];
        i++;
    }
    page[i] = '\0';
    n->data_len = i;
    return (text[i] != '\0') ? EXFS_ERR_TRUNC : 0;
}

/* Copies up to max-1 bytes into out and always NUL-terminates it.
 * Returns the number of bytes copied (0 for a never-written file), or
 * EXFS_ERR_NOTFILE / EXFS_ERR_DENIED. Reads are bounded by data_len, so
 * stale bytes left in a recycled frame past that length are never exposed. */
int exfs_read_data(const char *path, char *out, int max) {
    exfs_init();
    if (max < 1) return EXFS_ERR_NOTFILE;

    EXFSNode *n = exfs_find(path);
    if (!n || n->type != TYPE_FILE) return EXFS_ERR_NOTFILE;

    if (n->data_tok == XTOKEN_INVALID) {
        out[0] = '\0';
        return 0;
    }

    UINT64 phys = 0;
    if (!xtok_validate(n->data_tok, XTOK_RIGHT_READ) ||
        xtok_ram_info(n->data_tok, &phys, 0) != 0) {
        return EXFS_ERR_DENIED;
    }

    const char *page = (const char *)(UINTN)phys;
    int i = 0;
    while (i < n->data_len && i < max - 1) {
        out[i] = page[i];
        i++;
    }
    out[i] = '\0';
    return i;
}

/* Byte length of a file's data (0 if never written), or -1 if there is no file. */
int exfs_size(const char *raw_path) {
    exfs_init();
    EXFSNode *n = exfs_find(raw_path);
    if (!n || n->type != TYPE_FILE) return -1;
    return n->data_len;
}
