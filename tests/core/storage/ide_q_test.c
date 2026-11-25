#include <stdio.h>
#include <string.h>
#include "ide_q.h"
static int checks = 0, failures = 0;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
int main(void) {
    CHECK(ide_status_ready(IDE_SR_DRDY) == 1 && ide_status_ready(IDE_SR_BSY) == 0);
    CHECK(ide_status_drq(IDE_SR_DRDY | IDE_SR_DRQ) == 1 && ide_status_err(IDE_SR_ERR) == 1);
    IdeTaskfile tf;
    ide_pack_lba28(&tf, 1u, 0x05ABCDEFu, 8u);
    CHECK(tf.drive_head == 0xF5u && tf.seccount_lo == 8u && tf.lba[0] == 0xEFu && tf.lba[2] == 0xABu);
    ide_pack_lba48(&tf, 0u, 0x0000112233445566ULL, 0x0102u);
    CHECK(tf.drive_head == 0x40u && tf.seccount_hi == 0x01u && tf.seccount_lo == 0x02u);
    CHECK(tf.lba[0] == 0x66u && tf.lba[3] == 0x33u && tf.lba[5] == 0x11u);
    IdePrdEntry prd;
    ide_build_prd(&prd, 0x600000u, 4096u, 1);
    CHECK(prd.phys_addr == 0x600000u && prd.byte_count_eot == (4096u | IDE_PRD_EOT));
    uint16_t idw[256]; for (int i = 0; i < 256; i++) idw[i] = 0u;
    idw[83] = 1u << 10; idw[100] = 0x2000u;
    idw[27] = (uint16_t)(('I' << 8) | 'D'); idw[28] = (uint16_t)(('E' << 8) | '0');
    CHECK(ide_parse_identify_sectors(idw) == 0x2000ULL);
    char mod[16]; ide_parse_identify_model(idw, mod, sizeof(mod));
    CHECK(strcmp(mod, "IDE0") == 0);
    printf("ide_q: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
