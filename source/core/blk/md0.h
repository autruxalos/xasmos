#ifndef XASMOS_BLK_MD0_H
#define XASMOS_BLK_MD0_H
#include <stdint.h>
#include "blk.h"

#define MD0_MAX_MEMBERS  8u
#define MD0_MODE_LINEAR  0
#define MD0_MODE_STRIPE  1
#define MD0_MODE_MIRROR  2

typedef struct {
    int       mode;
    uint32_t  member_count;
    uint32_t  stripe_sectors;
    uint32_t  sector_size;
    uint64_t  total_sectors;
    BlkDev   *members[MD0_MAX_MEMBERS];
} Md0Array;

int md0_init(Md0Array *arr, int mode, uint32_t stripe_sectors, BlkDev *const *members, uint32_t count, BlkDev *out);
#endif
