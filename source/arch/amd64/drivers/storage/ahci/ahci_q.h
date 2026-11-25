#ifndef XASMOS_AHCI_Q_H
#define XASMOS_AHCI_Q_H
#include <stdint.h>

#define AHCI_GHC_AE          (1u << 31)
#define AHCI_GHC_IE          (1u << 1)
#define AHCI_GHC_HR          (1u << 0)
#define AHCI_BOHC_BOS        (1u << 0)
#define AHCI_BOHC_OOS        (1u << 1)
#define AHCI_BOHC_BB         (1u << 4)

#define AHCI_PORT_CMD_ST     (1u << 0)
#define AHCI_PORT_CMD_FRE    (1u << 4)
#define AHCI_PORT_CMD_FR     (1u << 14)
#define AHCI_PORT_CMD_CR     (1u << 15)
#define AHCI_PORT_TFD_DRQ    (1u << 3)
#define AHCI_PORT_TFD_BSY    (1u << 7)
#define AHCI_PORT_TFD_ERR    (1u << 0)
#define AHCI_SIG_ATA         0x00000101u
#define AHCI_SIG_ATAPI       0xEB140101u

#define FIS_TYPE_REG_H2D     0x27u
#define ATA_CMD_IDENTIFY     0xECu
#define ATA_CMD_READ_DMA_EXT 0x25u
#define ATA_CMD_WRITE_DMA_EXT 0x35u
#define ATA_CMD_FLUSH_EXT    0xEAu

typedef struct {
    uint16_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint64_t ctba;
    uint32_t rsvd[4];
} __attribute__((packed)) AhciCmdHeader;

typedef struct {
    uint64_t dba;
    uint32_t rsvd;
    uint32_t dbc;
} __attribute__((packed)) AhciPrdtEntry;

typedef struct {
    uint8_t  fis_type;
    uint8_t  pmport_c;
    uint8_t  command;
    uint8_t  featurel;
    uint8_t  lba0, lba1, lba2, device;
    uint8_t  lba3, lba4, lba5, featureh;
    uint8_t  countl, counth, icc, control;
    uint8_t  rsvd[4];
} __attribute__((packed)) AhciFisH2D;

uint32_t ahci_num_ports(uint32_t cap);
uint32_t ahci_num_cmd_slots(uint32_t cap);
int      ahci_supports_64bit(uint32_t cap);
int      ahci_port_present(uint32_t ssts);
int      ahci_port_is_ata(uint32_t ssts, uint32_t sig);
void     ahci_build_fis_h2d(AhciFisH2D *fis, uint8_t cmd, uint64_t lba, uint16_t count);
void     ahci_build_cmd_hdr(AhciCmdHeader *hdr, uint16_t prdtl, int write, uint64_t ctba);
void     ahci_build_prdt(AhciPrdtEntry *prdt, uint64_t dba, uint32_t bytes, int interrupt);
uint64_t ahci_parse_identify_sectors(const uint16_t id_words[256]);
void     ahci_parse_identify_string(const uint16_t *words, uint32_t word_count, char *out, uint32_t max);

#endif
