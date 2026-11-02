/******************************************************************************
 * XASMOS - EXFS internal interface (shared by table.c, path.c, data.c, store.c)
 * Path: source/arch/amd64/drivers/storage/exfs/exfs_internal.h
 ******************************************************************************/
#ifndef XASMOS_EXFS_INTERNAL_H
#define XASMOS_EXFS_INTERNAL_H

#include <stdint.h>
#include "capability.h"
#include "exfs.h"

#define MAX_NODES    128
#define MAX_PATH_LEN 256
#define TYPE_FILE    0
#define TYPE_DIR     1

typedef struct {
    char     path[MAX_PATH_LEN];
    int      type;
    int      active;
    XToken   data_tok;       /* XTOKEN_INVALID until the first write        */
    int      data_len;
    int      data_dirty;     /* content changed since the last commit       */
    uint32_t disk_block;     /* block holding the content on disk, 0 = none */
} EXFSNode;

extern EXFSNode exfs_table[MAX_NODES];
extern int      exfs_table_dirty;   /* the node table changed since the last commit */

/* table.c */
void      exfs_clear(void);          /* drops every node and token; root only   */
void      exfs_reset_system(void);   /* clear, then the four system directories */
void      exfs_init_mark(void);      /* table is loaded: do not reseed          */
EXFSNode *exfs_find(const char *path);

/* path.c */
void exfs_strcpy(char *dest, const char *src);
int  exfs_strequal(const char *s1, const char *s2);

/* data.c */
int  exfs_data_release(EXFSNode *n);  /* 0 on success or nothing to release; -1 if refused */

#endif /* XASMOS_EXFS_INTERNAL_H */
