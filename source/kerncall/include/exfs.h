/******************************************************************************
 * XASMOS - EXFS public interface
 * Path: source/kerncall/include/exfs.h
 *
 * Single source of truth for the per-file capacity and the return codes.
 * The implementation lives in source/arch/amd64/drivers/storage/exfs/:
 *   table.c  nodes: create, delete, stat, lookup
 *   path.c   string helpers and directory listing
 *   data.c   file content on token frames: write, read, size
 ******************************************************************************/
#ifndef XASMOS_EXFS_H
#define XASMOS_EXFS_H

/* One 4 KiB frame per file: 4095 text bytes + the terminating NUL. */
#define EXFS_DATA_MAX  4096

/* exfs_write_data / exfs_read_data return values (negative = failure). */
#define EXFS_ERR_NOTFILE  (-1)  /* path absent, or not a regular file       */
#define EXFS_ERR_TRUNC    (-2)  /* write stored, but cut to EXFS_DATA_MAX-1 */
#define EXFS_ERR_NOMEM    (-3)  /* no frame / no token slot available       */
#define EXFS_ERR_DENIED   (-4)  /* token lacks the right, or is stale       */

void exfs_init(void);
int  exfs_create_object(const char *raw_path, int type);
int  exfs_delete_object(const char *raw_path);
int  exfs_stat(const char *raw_path);   /* -1 absent, else TYPE_FILE/TYPE_DIR */
int  exfs_enum(const char *dir, void (*cb)(const char *name, int type));
int  exfs_write_data(const char *path, const char *text);
int  exfs_read_data(const char *path, char *out, int max);
int  exfs_size(const char *raw_path);   /* bytes stored, 0 if never written, -1 if no file */

#endif /* XASMOS_EXFS_H */
