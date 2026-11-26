#ifndef XASMOS_E1000_Q_H
#define XASMOS_E1000_Q_H
#include <stdint.h>

#define E1000_VENDOR_INTEL   0x8086u
#define E1000_DEV_82540EM    0x100Eu
#define E1000_DEV_82545EM    0x100Fu
#define E1000_DEV_82577LM    0x10EAu
#define E1000_DEV_I217LM     0x153Au
#define E1000_DEV_I219LM     0x15B7u

#define E1000_REG_CTRL       0x0000u
#define E1000_REG_STATUS     0x0008u
#define E1000_REG_EERD       0x0014u
#define E1000_REG_ICR        0x00C0u
#define E1000_REG_IMS        0x00D0u
#define E1000_REG_IMC        0x00D8u
#define E1000_REG_RCTL       0x0100u
#define E1000_REG_TCTL       0x0400u
#define E1000_REG_TIPG       0x0410u
#define E1000_REG_RDBAL      0x2800u
#define E1000_REG_RDBAH      0x2804u
#define E1000_REG_RDLEN      0x2808u
#define E1000_REG_RDH        0x2810u
#define E1000_REG_RDT        0x2818u
#define E1000_REG_TDBAL      0x3800u
#define E1000_REG_TDBAH      0x3804u
#define E1000_REG_TDLEN      0x3808u
#define E1000_REG_TDH        0x3810u
#define E1000_REG_TDT        0x3818u
#define E1000_REG_MTA        0x5200u
#define E1000_REG_RAL0       0x5400u
#define E1000_REG_RAH0       0x5404u

#define E1000_CTRL_SLU       (1u << 6)
#define E1000_CTRL_RST       (1u << 26)
#define E1000_RCTL_EN        (1u << 1)
#define E1000_RCTL_UPE       (1u << 3)
#define E1000_RCTL_MPE       (1u << 4)
#define E1000_RCTL_BAM       (1u << 15)
#define E1000_RCTL_BSIZE_2K  (0u << 16)
#define E1000_RCTL_SECRC     (1u << 26)
#define E1000_TCTL_EN        (1u << 1)
#define E1000_TCTL_PSP       (1u << 3)
#define E1000_TXD_CMD_EOP    (1u << 0)
#define E1000_TXD_CMD_IFCS   (1u << 1)
#define E1000_TXD_CMD_RS     (1u << 3)
#define E1000_TXD_STAT_DD    (1u << 0)
#define E1000_RXD_STAT_DD    (1u << 0)
#define E1000_RXD_STAT_EOP   (1u << 1)

#define E1000_RING_DESCS     32u
#define E1000_PKT_BUF_SIZE   2048u

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} E1000RxDesc;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} E1000TxDesc;

int e1000_is_supported_device(uint16_t vendor, uint16_t device);
void e1000_decode_mac_ral_rah(uint32_t ral, uint32_t rah, uint8_t mac_out[6]);
int e1000_prepare_tx_desc(E1000TxDesc *desc, uint64_t buf_phys, uint16_t len);
int e1000_check_rx_desc(const E1000RxDesc *desc, uint16_t *len_out);

#endif
