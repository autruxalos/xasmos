/******************************************************************************
 * XASMOS - EXFS file content on token frames
 * Path: source/arch/amd64/drivers/storage/exfs/data.c
 *
 * A file owns one 4 KiB frame, sealed by the token ledger, from its first
 * write on. An empty file costs no memory. A write marks the file dirty; the
 * content reaches the disk at the next sync (store.c).
 *
 * KNOWN LIMIT: the frame's physical address is dereferenced directly, which
 * is valid while the kernel identity map is active. Processes never reach
 * these bytes: they use their own mapping.
 ******************************************************************************/
#include "exfs_internal.h"

int exfs_data_release(EXFSNode *n) {
    if (n->data_tok == XTOKEN_INVALID) return 0;
    if (xtok_release(n->data_tok) != 0) return -1;
    n->data_tok = XTOKEN_INVALID;
    return 0;
}

/* Stores len bytes as the file's whole content. The file must be a regular
 * file; the frame is allocated on the first write. */
static int store_bytes(EXFSNode *n, const uint8_t *src, int len) {
    if (n->data_tok == XTOKEN_INVALID) {
        n->data_tok = xtok_alloc_ram(XTOK_OWNER_KERNEL, 1, XTOK_RIGHTS_RAM_DEFAULT);
        if (n->data_tok == XTOKEN_INVALID) return EXFS_ERR_NOMEM;
    }

    UINT64 phys = 0;
    if (!xtok_validate(n->data_tok, XTOK_RIGHT_WRITE) ||
        xtok_ram_info(n->data_tok, &phys, 0) != 0) {
        return EXFS_ERR_DENIED;
    }

    uint8_t *page = (uint8_t *)(UINTN)phys;
    for (int i = 0; i < len; i++) page[i] = src[i];
    n->data_len   = len;
    n->data_dirty = 1;
    exfs_table_dirty = 1;
    return 0;
}

/* Text content: stops at the first NUL or at EXFS_DATA_MAX-1 bytes. */
int exfs_write_data(const char *path, const char *text) {
    exfs_init();
    EXFSNode *n = exfs_find(path);
    if (!n || n->type != TYPE_FILE) return EXFS_ERR_NOTFILE;

    int len = 0;
    while (text[len] != '\0' && len < EXFS_DATA_MAX - 1) len++;
    int truncated = (text[len] != '\0');

    int rc = store_bytes(n, (const uint8_t *)text, len);
    if (rc != 0) return rc;
    return truncated ? EXFS_ERR_TRUNC : 0;
}

/* Binary content: len bytes exactly, NULs included. Refused if it does not fit. */
int exfs_write_bytes(const char *path, const void *buf, int len) {
    exfs_init();
    EXFSNode *n = exfs_find(path);
    if (!n || n->type != TYPE_FILE) return EXFS_ERR_NOTFILE;
    if (len < 0 || len > EXFS_DATA_MAX - 1) return EXFS_ERR_TRUNC;
    return store_bytes(n, (const uint8_t *)buf, len);
}

/* Copies up to max-1 bytes and NUL-terminates. Returns the bytes copied (0 for
 * a file never written), or EXFS_ERR_NOTFILE / EXFS_ERR_DENIED. */
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

int exfs_size(const char *raw_path) {
    exfs_init();
    EXFSNode *n = exfs_find(raw_path);
    if (!n || n->type != TYPE_FILE) return -1;
    return n->data_len;
}
