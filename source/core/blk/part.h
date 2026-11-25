#ifndef XASMOS_BLK_PART_H
#define XASMOS_BLK_PART_H
#include <stdint.h>
#include "blk.h"

#define PART_FMT_NONE  0
#define PART_FMT_MBR   1
#define PART_FMT_GPT   2
#define PART_MAX_ENTRIES 16

typedef struct {
    uint8_t  active;
    uint8_t  mbr_type;
    uint8_t  type_guid[16];
    uint64_t start_lba;
    uint64_t sector_count;
    char     name[40];
} PartEntry;

typedef struct {
    int       format;
    uint32_t  count;
    PartEntry entries[PART_MAX_ENTRIES];
} PartTable;

typedef struct {
    BlkDev  *parent;
    uint64_t start_lba;
    uint64_t sector_count;
} PartSlice;

int part_probe(BlkDev *parent, PartTable *out);
int part_open_slice(BlkDev *parent, const PartEntry *pe, PartSlice *slice, BlkDev *out);
#endif
