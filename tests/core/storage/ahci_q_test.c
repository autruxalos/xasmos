#include <stdio.h>
#include <string.h>
#include "ahci_q.h"
static int checks = 0, failures = 0;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
int main(void) {
    uint32_t cap = (1u << 31) | (31u << 8) | 5u;
    CHECK(ahci_num_ports(cap) == 6u && ahci_num_cmd_slots(cap) == 32u && ahci_supports_64bit(cap) == 1);
    CHECK(ahci_port_present(0x113u) == 1 && ahci_port_present(0x110u) == 0);
    CHECK(ahci_port_is_ata(0x113u, AHCI_SIG_ATA) == 1 && ahci_port_is_ata(0x113u, AHCI_SIG_ATAPI) == 0);
    AhciFisH2D fis;
    ahci_build_fis_h2d(&fis, ATA_CMD_READ_DMA_EXT, 0x0000112233445566ULL, 8u);
    CHECK(fis.fis_type == FIS_TYPE_REG_H2D && fis.pmport_c == 0x80u && fis.command == ATA_CMD_READ_DMA_EXT);
    CHECK(fis.lba0 == 0x66u && fis.lba2 == 0x44u && fis.lba3 == 0x33u && fis.lba5 == 0x11u && fis.countl == 8u);
    AhciCmdHeader hdr; AhciPrdtEntry prdt;
    ahci_build_cmd_hdr(&hdr, 1u, 1, 0x400000ULL);
    CHECK(hdr.prdtl == 1u && (hdr.flags & (1u << 6)) != 0u && hdr.ctba == 0x400000ULL);
    ahci_build_prdt(&prdt, 0x500000ULL, 4096u, 1);
    CHECK(prdt.dba == 0x500000ULL && (prdt.dbc & 0x3FFFFFu) == 4095u && (prdt.dbc & (1u << 31)) != 0u);
    uint16_t idw[256]; for (int i = 0; i < 256; i++) idw[i] = 0u;
    idw[100] = 0x4000u; idw[101] = 0x0001u;
    idw[27] = (uint16_t)(('X' << 8) | 'A'); idw[28] = (uint16_t)(('S' << 8) | 'M');
    CHECK(ahci_parse_identify_sectors(idw) == 0x14000ULL);
    char mod[16]; ahci_parse_identify_string(&idw[27], 2u, mod, sizeof(mod));
    CHECK(strcmp(mod, "XASM") == 0);
    printf("ahci_q: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
