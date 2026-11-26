#include "ivrs_q.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) | ((uint32_t)rd16(p + 2u) << 16); }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4u) << 32); }

int acpi_checksum_ok(const uint8_t *tbl, uint32_t len) {
    if (!tbl || len < 36u) return 0;
    uint8_t sum = 0u;
    for (uint32_t i = 0u; i < len; i++) sum = (uint8_t)(sum + tbl[i]);
    return (sum == 0u) ? 1 : 0;
}

int ivrs_parse(const uint8_t *tbl, uint32_t len, IvrsTableInfo *out) {
    if (!tbl || !out || len < 48u) return -1;
    if (tbl[0] != 'I' || tbl[1] != 'V' || tbl[2] != 'R' || tbl[3] != 'S') return -2;
    uint32_t tlen = rd32(tbl + 4u);
    if (tlen < 48u || tlen > len || !acpi_checksum_ok(tbl, tlen)) return -3;

    out->unit_count = 0u;
    out->iv_info = rd32(tbl + 36u);
    uint32_t off = 48u;
    while (off + 24u <= tlen) {
        uint8_t etype = tbl[off];
        uint16_t elen = rd16(tbl + off + 2u);
        if (elen < 24u || off + elen > tlen) break;
        if (etype == IVHD_TYPE_10 || etype == IVHD_TYPE_11 || etype == IVHD_TYPE_40) {
            if (out->unit_count < IVRS_MAX_UNITS) {
                IvrsIommuUnit *u = &out->units[out->unit_count++];
                u->ivhd_type  = etype;
                u->flags      = tbl[off + 1u];
                u->device_id  = rd16(tbl + off + 4u);
                u->cap_offset = rd16(tbl + off + 6u);
                u->mmio_phys  = rd64(tbl + off + 8u);
                u->pci_segment = rd16(tbl + off + 16u);
            }
        }
        off += elen;
    }
    return 0;
}
