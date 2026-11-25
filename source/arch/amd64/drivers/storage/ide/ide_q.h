#ifndef XASMOS_IDE_Q_H
#define XASMOS_IDE_Q_H
#include <stdint.h>

#define IDE_PRI_CMD_BASE     0x1F0u
#define IDE_PRI_CTRL_BASE    0x3F6u
#define IDE_SEC_CMD_BASE     0x170u
#define IDE_SEC_CTRL_BASE    0x376u

#define IDE_REG_DATA         0x00u
#define IDE_REG_ERROR        0x01u
#define IDE_REG_FEATURES     0x01u
#define IDE_REG_SECCOUNT     0x02u
#define IDE_REG_LBA0         0x03u
#define IDE_REG_LBA1         0x04u
#define IDE_REG_LBA2         0x05u
#define IDE_REG_HDDEVSEL     0x06u
#define IDE_REG_COMMAND      0x07u
#define IDE_REG_STATUS       0x07u

#define IDE_SR_BSY           0x80u
#define IDE_SR_DRDY          0x40u
#define IDE_SR_DF            0x20u
#define IDE_SR_DRQ           0x08u
#define IDE_SR_ERR           0x01u

#define IDE_CMD_READ_PIO     0x20u
#define IDE_CMD_READ_PIO_EXT 0x24u
#define IDE_CMD_WRITE_PIO    0x30u
#define IDE_CMD_WRITE_PIO_EXT 0x34u
#define IDE_CMD_CACHE_FLUSH  0xE7u
#define IDE_CMD_IDENTIFY     0xECu

#define IDE_PRD_EOT          0x80000000u

typedef struct {
    uint32_t phys_addr;
    uint32_t byte_count_eot;
} __attribute__((packed)) IdePrdEntry;

typedef struct {
    uint8_t drive_head;
    uint8_t seccount_hi;
    uint8_t seccount_lo;
    uint8_t lba[6];
} IdeTaskfile;

int      ide_status_ready(uint8_t st);
int      ide_status_drq(uint8_t st);
int      ide_status_err(uint8_t st);
void     ide_pack_lba28(IdeTaskfile *tf, uint8_t slave, uint32_t lba, uint8_t count);
void     ide_pack_lba48(IdeTaskfile *tf, uint8_t slave, uint64_t lba, uint16_t count);
void     ide_build_prd(IdePrdEntry *prd, uint32_t phys_addr, uint16_t bytes, int eot);
uint64_t ide_parse_identify_sectors(const uint16_t id_words[256]);
void     ide_parse_identify_model(const uint16_t id_words[256], char *out, uint32_t max);

#endif
