#include "md0.h"

static int md0_read(void *ctx, uint64_t lba, uint32_t count, void *buf) {
    Md0Array *a = (Md0Array *)ctx;
    if (lba + count > a->total_sectors) return BLK_E_RANGE;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t bsz = a->sector_size;
    if (a->mode == MD0_MODE_MIRROR) {
        for (uint32_t m = 0; m < a->member_count; m++) {
            if (blk_read(a->members[m], lba, count, buf) == BLK_OK) return BLK_OK;
        }
        return BLK_E_IO;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint64_t cur = lba + i;
        if (a->mode == MD0_MODE_STRIPE) {
            uint64_t stripe_idx = cur / a->stripe_sectors;
            uint64_t off_in_st  = cur % a->stripe_sectors;
            uint32_t disk       = (uint32_t)(stripe_idx % a->member_count);
            uint64_t disk_lba   = (stripe_idx / a->member_count) * a->stripe_sectors + off_in_st;
            if (blk_read(a->members[disk], disk_lba, 1u, dst + i * bsz) != BLK_OK) return BLK_E_IO;
        } else {
            uint64_t rem = cur;
            int done = 0;
            for (uint32_t m = 0; m < a->member_count; m++) {
                if (rem < a->members[m]->sector_count) {
                    if (blk_read(a->members[m], rem, 1u, dst + i * bsz) != BLK_OK) return BLK_E_IO;
                    done = 1; break;
                }
                rem -= a->members[m]->sector_count;
            }
            if (!done) return BLK_E_RANGE;
        }
    }
    return BLK_OK;
}

static int md0_write(void *ctx, uint64_t lba, uint32_t count, const void *buf) {
    Md0Array *a = (Md0Array *)ctx;
    if (lba + count > a->total_sectors) return BLK_E_RANGE;
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t bsz = a->sector_size;
    if (a->mode == MD0_MODE_MIRROR) {
        int ok_any = 0;
        for (uint32_t m = 0; m < a->member_count; m++) {
            if (blk_write(a->members[m], lba, count, buf) == BLK_OK) ok_any = 1;
        }
        return ok_any ? BLK_OK : BLK_E_IO;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint64_t cur = lba + i;
        if (a->mode == MD0_MODE_STRIPE) {
            uint64_t stripe_idx = cur / a->stripe_sectors;
            uint64_t off_in_st  = cur % a->stripe_sectors;
            uint32_t disk       = (uint32_t)(stripe_idx % a->member_count);
            uint64_t disk_lba   = (stripe_idx / a->member_count) * a->stripe_sectors + off_in_st;
            if (blk_write(a->members[disk], disk_lba, 1u, src + i * bsz) != BLK_OK) return BLK_E_IO;
        } else {
            uint64_t rem = cur;
            int done = 0;
            for (uint32_t m = 0; m < a->member_count; m++) {
                if (rem < a->members[m]->sector_count) {
                    if (blk_write(a->members[m], rem, 1u, src + i * bsz) != BLK_OK) return BLK_E_IO;
                    done = 1; break;
                }
                rem -= a->members[m]->sector_count;
            }
            if (!done) return BLK_E_RANGE;
        }
    }
    return BLK_OK;
}

static int md0_flush(void *ctx) {
    Md0Array *a = (Md0Array *)ctx;
    for (uint32_t m = 0; m < a->member_count; m++) {
        if (blk_flush(a->members[m]) != BLK_OK) return BLK_E_IO;
    }
    return BLK_OK;
}

static const BlkOps g_md0_ops = { md0_read, md0_write, md0_flush, 0 };

int md0_init(Md0Array *arr, int mode, uint32_t stripe_sectors, BlkDev *const *members, uint32_t count, BlkDev *out) {
    if (!arr || !members || !out || count == 0u || count > MD0_MAX_MEMBERS) return BLK_E_ARG;
    uint32_t bsz = members[0]->sector_size;
    if (bsz == 0u) return BLK_E_ARG;
    uint64_t min_secs = members[0]->sector_count, sum_secs = 0ULL;
    for (uint32_t i = 0; i < count; i++) {
        if (!members[i] || members[i]->sector_size != bsz || members[i]->sector_count == 0ULL) return BLK_E_ARG;
        arr->members[i] = members[i];
        if (members[i]->sector_count < min_secs) min_secs = members[i]->sector_count;
        sum_secs += members[i]->sector_count;
    }
    arr->mode = mode; arr->member_count = count;
    arr->stripe_sectors = stripe_sectors ? stripe_sectors : 8u;
    arr->sector_size = bsz;
    if (mode == MD0_MODE_MIRROR) arr->total_sectors = min_secs;
    else if (mode == MD0_MODE_STRIPE) arr->total_sectors = (min_secs / arr->stripe_sectors) * arr->stripe_sectors * count;
    else arr->total_sectors = sum_secs;
    if (arr->total_sectors == 0ULL) return BLK_E_RANGE;
    out->ctx = arr; out->ops = &g_md0_ops; out->sector_size = bsz; out->sector_count = arr->total_sectors;
    return BLK_OK;
}
