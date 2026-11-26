#include "ahci_q.h"

uint32_t ahci_num_ports(uint32_t cap) { return (cap & 0x1Fu) + 1u; }
uint32_t ahci_num_cmd_slots(uint32_t cap) { return ((cap >> 8) & 0x1Fu) + 1u; }
int      ahci_supports_64bit(uint32_t cap) { return (cap & (1u << 31)) != 0u; }
int      ahci_port_present(uint32_t ssts) { return ((ssts & 0x0Fu) == 3u) && (((ssts >> 8) & 0x0Fu) == 1u); }
int      ahci_port_is_ata(uint32_t ssts, uint32_t sig) { return ahci_port_present(ssts) && (sig == AHCI_SIG_ATA); }

void ahci_build_fis_h2d(AhciFisH2D *fis, uint8_t cmd, uint64_t lba, uint16_t count) {
    uint8_t *p = (uint8_t *)fis;
    for (uint32_t i = 0; i < sizeof(AhciFisH2D); i++) p[i] = 0u;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80u;
    fis->command  = cmd;
    fis->lba0     = (uint8_t)(lba & 0xFFu);
    fis->lba1     = (uint8_t)((lba >> 8) & 0xFFu);
    fis->lba2     = (uint8_t)((lba >> 16) & 0xFFu);
    fis->device   = 1u << 6;
    fis->lba3     = (uint8_t)((lba >> 24) & 0xFFu);
    fis->lba4     = (uint8_t)((lba >> 32) & 0xFFu);
    fis->lba5     = (uint8_t)((lba >> 40) & 0xFFu);
    fis->countl   = (uint8_t)(count & 0xFFu);
    fis->counth   = (uint8_t)((count >> 8) & 0xFFu);
}

void ahci_build_cmd_hdr(AhciCmdHeader *hdr, uint16_t prdtl, int write, uint64_t ctba) {
    hdr->flags = (uint16_t)((sizeof(AhciFisH2D) / 4u) | (write ? (1u << 6) : 0u));
    hdr->prdtl = prdtl;
    hdr->prdbc = 0u;
    hdr->ctba  = ctba;
    hdr->rsvd[0] = hdr->rsvd[1] = hdr->rsvd[2] = hdr->rsvd[3] = 0u;
}

void ahci_build_prdt(AhciPrdtEntry *prdt, uint64_t dba, uint32_t bytes, int interrupt) {
    prdt->dba  = dba;
    prdt->rsvd = 0u;
    prdt->dbc  = ((bytes - 1u) & 0x3FFFFFu) | (interrupt ? (1u << 31) : 0u);
}

uint64_t ahci_parse_identify_sectors(const uint16_t id_words[256]) {
    uint64_t lba48 = (uint64_t)id_words[100] | ((uint64_t)id_words[101] << 16) |
                     ((uint64_t)id_words[102] << 32) | ((uint64_t)id_words[103] << 48);
    if (lba48 > 0ULL) return lba48;
    return (uint64_t)id_words[60] | ((uint64_t)id_words[61] << 16);
}

void ahci_parse_identify_string(const uint16_t *words, uint32_t word_count, char *out, uint32_t max) {
    if (!out || max == 0u) return;
    uint32_t pos = 0u;
    for (uint32_t i = 0; i < word_count && pos + 1u < max; i++) {
        char hi = (char)(words[i] >> 8), lo = (char)(words[i] & 0xFFu);
        if (pos + 1u < max) out[pos++] = hi ? hi : ' ';
        if (pos + 1u < max) out[pos++] = lo ? lo : ' ';
    }
    while (pos > 0u && out[pos - 1u] == ' ') pos--;
    out[pos] = 0;
}
