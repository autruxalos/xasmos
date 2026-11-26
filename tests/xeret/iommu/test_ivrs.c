#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ivrs_q.h"

int main(void) {
    uint8_t tbl[72];
    memset(tbl, 0, sizeof(tbl));
    tbl[0] = 'I'; tbl[1] = 'V'; tbl[2] = 'R'; tbl[3] = 'S';
    tbl[4] = 72u; /* length */
    tbl[48] = IVHD_TYPE_11;
    tbl[50] = 24u; /* entry len */
    tbl[52] = 0x02u; /* device_id 00:00.2 */
    tbl[56] = 0x00u; tbl[57] = 0x00u; tbl[58] = 0x80u; tbl[59] = 0xFEu; /* 0xFE800000 */
    uint8_t sum = 0u;
    for (int i = 0; i < 72; i++) sum = (uint8_t)(sum + tbl[i]);
    tbl[9] = (uint8_t)(0u - sum);

    IvrsTableInfo info;
    assert(ivrs_parse(tbl, sizeof(tbl), &info) == 0);
    assert(info.unit_count == 1u);
    assert(info.units[0].ivhd_type == IVHD_TYPE_11);
    assert(info.units[0].device_id == 0x0002u);
    assert(info.units[0].mmio_phys == 0xFE800000ULL);
    puts("PASS: test_ivrs");
    return 0;
}
