#include "ide_q.h"

int ide_status_ready(uint8_t st) { return (st & IDE_SR_BSY) == 0u; }
int ide_status_drq(uint8_t st)   { return ((st & IDE_SR_BSY) == 0u) && ((st & IDE_SR_DRQ) != 0u); }
int ide_status_err(uint8_t st)   { return (st & (IDE_SR_ERR | IDE_SR_DF)) != 0u; }

void ide_pack_lba28(IdeTaskfile *tf, uint8_t slave, uint32_t lba, uint8_t count) {
    tf->drive_head  = (uint8_t)(0xE0u | ((slave & 1u) << 4) | ((lba >> 24) & 0x0Fu));
    tf->seccount_hi = 0u;
    tf->seccount_lo = count;
    tf->lba[0] = (uint8_t)(lba & 0xFFu);
    tf->lba[1] = (uint8_t)((lba >> 8) & 0xFFu);
    tf->lba[2] = (uint8_t)((lba >> 16) & 0xFFu);
    tf->lba[3] = tf->lba[4] = tf->lba[5] = 0u;
}

void ide_pack_lba48(IdeTaskfile *tf, uint8_t slave, uint64_t lba, uint16_t count) {
    tf->drive_head  = (uint8_t)(0x40u | ((slave & 1u) << 4));
    tf->seccount_hi = (uint8_t)((count >> 8) & 0xFFu);
    tf->seccount_lo = (uint8_t)(count & 0xFFu);
    tf->lba[0] = (uint8_t)(lba & 0xFFu);
    tf->lba[1] = (uint8_t)((lba >> 8) & 0xFFu);
    tf->lba[2] = (uint8_t)((lba >> 16) & 0xFFu);
    tf->lba[3] = (uint8_t)((lba >> 24) & 0xFFu);
    tf->lba[4] = (uint8_t)((lba >> 32) & 0xFFu);
    tf->lba[5] = (uint8_t)((lba >> 40) & 0xFFu);
}

void ide_build_prd(IdePrdEntry *prd, uint32_t phys_addr, uint16_t bytes, int eot) {
    prd->phys_addr = phys_addr;
    prd->byte_count_eot = (uint32_t)bytes | (eot ? IDE_PRD_EOT : 0u);
}

uint64_t ide_parse_identify_sectors(const uint16_t id_words[256]) {
    if (id_words[83] & (1u << 10)) {
        uint64_t lba48 = (uint64_t)id_words[100] | ((uint64_t)id_words[101] << 16) |
                         ((uint64_t)id_words[102] << 32) | ((uint64_t)id_words[103] << 48);
        if (lba48 > 0ULL) return lba48;
    }
    return (uint64_t)id_words[60] | ((uint64_t)id_words[61] << 16);
}

void ide_parse_identify_model(const uint16_t id_words[256], char *out, uint32_t max) {
    if (!out || max == 0u) return;
    uint32_t pos = 0u;
    for (uint32_t i = 0; i < 20u && pos + 1u < max; i++) {
        uint16_t w = id_words[27u + i];
        char hi = (char)(w >> 8), lo = (char)(w & 0xFFu);
        if (pos + 1u < max) out[pos++] = hi ? hi : ' ';
        if (pos + 1u < max) out[pos++] = lo ? lo : ' ';
    }
    while (pos > 0u && out[pos - 1u] == ' ') pos--;
    out[pos] = 0;
}
