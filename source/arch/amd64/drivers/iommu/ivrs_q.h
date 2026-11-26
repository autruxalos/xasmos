#ifndef XASMOS_IVRS_Q_H
#define XASMOS_IVRS_Q_H
#include <stdint.h>

#define IVRS_MAX_UNITS      8u
#define IVHD_TYPE_10        0x10u
#define IVHD_TYPE_11        0x11u
#define IVHD_TYPE_40        0x40u

typedef struct {
    uint8_t  ivhd_type;
    uint8_t  flags;
    uint16_t device_id;
    uint16_t cap_offset;
    uint16_t pci_segment;
    uint64_t mmio_phys;
} IvrsIommuUnit;

typedef struct {
    uint32_t      unit_count;
    uint32_t      iv_info;
    IvrsIommuUnit units[IVRS_MAX_UNITS];
} IvrsTableInfo;

int acpi_checksum_ok(const uint8_t *tbl, uint32_t len);
int ivrs_parse(const uint8_t *tbl, uint32_t len, IvrsTableInfo *out);
#endif
