/******************************************************************************
 * XASMOS - EXFS file content on token frames
 * Path: source/arch/amd64/drivers/storage/exfs/data.c
 *
 * A file owns one 4 KiB frame, sealed by the token ledger, only after its
 * first write. An empty file costs no memory. Overwrites reuse the frame.
 *
 * KNOWN LIMIT: the frame's physical address is dereferenced directly. That
 * is valid while the kernel identity map is active (it is, in xeret). A
 * process must never reach these bytes directly: it uses its own mapping.
 ******************************************************************************/
#include "exfs_internal.h"

int exfs_data_release(EXFSNode *n) {
    if (n->data_tok == XTOKEN_INVALID) return 0;
    if (xtok_release(n->data_tok) != 0) return -1;
    n->data_tok = XTOKEN_INVALID;
    return 0;
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
