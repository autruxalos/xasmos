#include "part.h"

static uint16_t rd_le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd_le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd_le64(const uint8_t *p) { return (uint64_t)rd_le32(p) | ((uint64_t)rd_le32(p + 4u) << 32); }

static int guid_is_zero(const uint8_t g[16]) {
    for (int i = 0; i < 16; i++) if (g[i] != 0u) return 0;
    return 1;
}

int part_probe(BlkDev *parent, PartTable *out) {
    if (!parent || !out) return BLK_E_ARG;
    out->format = PART_FMT_NONE; out->count = 0u;
    for (int i = 0; i < PART_MAX_ENTRIES; i++) out->entries[i].active = 0u;
    if (parent->sector_size < 512u || parent->sector_count < 2ULL) return BLK_E_RANGE;
    uint8_t sec[512];
    if (blk_read(parent, 1ULL, 1u, sec) != BLK_OK) return BLK_E_IO;
    if (rd_le64(&sec[0]) == 0x5452415020494645ULL) {
        out->format = PART_FMT_GPT;
        uint64_t ent_lba  = rd_le64(&sec[72]);
        uint32_t num_ents = rd_le32(&sec[80]);
        uint32_t ent_sz   = rd_le32(&sec[84]);
        if (ent_sz < 128u || ent_sz > 512u) return BLK_E_IO;
        uint32_t per_sec = 512u / ent_sz;
        uint32_t limit = num_ents > 128u ? 128u : num_ents;
        for (uint32_t idx = 0; idx < limit && out->count < PART_MAX_ENTRIES; idx++) {
            if ((idx % per_sec) == 0u) {
                if (blk_read(parent, ent_lba + (idx / per_sec), 1u, sec) != BLK_OK) return BLK_E_IO;
            }
            const uint8_t *e = &sec[(idx % per_sec) * ent_sz];
            if (guid_is_zero(e)) continue;
            uint64_t first = rd_le64(&e[32]), last = rd_le64(&e[40]);
            if (last < first || last >= parent->sector_count) continue;
            PartEntry *pe = &out->entries[out->count++];
            pe->active = 1u; pe->mbr_type = 0u;
            for (int g = 0; g < 16; g++) pe->type_guid[g] = e[g];
            pe->start_lba = first; pe->sector_count = (last - first) + 1ULL;
            uint32_t np = 0u;
            for (uint32_t u = 0; u < 36u && np + 1u < sizeof(pe->name); u++) {
                uint16_t ch = rd_le16(&e[56u + u * 2u]);
                if (ch == 0u) break;
                pe->name[np++] = (ch < 128u) ? (char)ch : '?';
            }
            pe->name[np] = 0;
        }
        return BLK_OK;
    }
    if (blk_read(parent, 0ULL, 1u, sec) != BLK_OK) return BLK_E_IO;
    if (sec[510] == 0x55u && sec[511] == 0xAAu) {
        out->format = PART_FMT_MBR;
        for (uint32_t i = 0; i < 4u; i++) {
            const uint8_t *e = &sec[446u + i * 16u];
            uint8_t ptype = e[4];
            uint32_t start = rd_le32(&e[8]), count = rd_le32(&e[12]);
            if (ptype == 0u || count == 0u) continue;
            if ((uint64_t)start + (uint64_t)count > parent->sector_count) continue;
            PartEntry *pe = &out->entries[out->count++];
            pe->active = 1u; pe->mbr_type = ptype;
            for (int g = 0; g < 16; g++) pe->type_guid[g] = 0u;
            pe->start_lba = (uint64_t)start; pe->sector_count = (uint64_t)count;
            pe->name[0] = 'm'; pe->name[1] = 'b'; pe->name[2] = 'r'; pe->name[3] = (char)('0' + i); pe->name[4] = 0;
        }
        return BLK_OK;
    }
    return BLK_OK;
}

static int slice_read(void *ctx, uint64_t lba, uint32_t count, void *buf) {
    PartSlice *s = (PartSlice *)ctx;
    if (lba + count > s->sector_count) return BLK_E_RANGE;
    return blk_read(s->parent, s->start_lba + lba, count, buf);
}
static int slice_write(void *ctx, uint64_t lba, uint32_t count, const void *buf) {
    PartSlice *s = (PartSlice *)ctx;
    if (lba + count > s->sector_count) return BLK_E_RANGE;
    return blk_write(s->parent, s->start_lba + lba, count, buf);
}
static int slice_flush(void *ctx) { return blk_flush(((PartSlice *)ctx)->parent); }
static const BlkOps g_slice_ops = { slice_read, slice_write, slice_flush, 0 };

int part_open_slice(BlkDev *parent, const PartEntry *pe, PartSlice *slice, BlkDev *out) {
    if (!parent || !pe || !slice || !out || !pe->active) return BLK_E_ARG;
    if (pe->start_lba + pe->sector_count > parent->sector_count) return BLK_E_RANGE;
    slice->parent = parent; slice->start_lba = pe->start_lba; slice->sector_count = pe->sector_count;
    out->ctx = slice; out->ops = &g_slice_ops;
    out->sector_size = parent->sector_size; out->sector_count = pe->sector_count;
    return BLK_OK;
}
