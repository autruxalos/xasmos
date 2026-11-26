#ifndef XASMOS_HDA_Q_H
#define XASMOS_HDA_Q_H
#include <stdint.h>

#define HDA_REG_GCAP        0x00u
#define HDA_REG_VMIN        0x02u
#define HDA_REG_VMAJ        0x03u
#define HDA_REG_GCTL        0x08u
#define HDA_REG_STATESTS    0x0Eu
#define HDA_REG_INTCTL      0x20u
#define HDA_REG_CORBLBASE   0x40u
#define HDA_REG_CORBUBASE   0x44u
#define HDA_REG_CORBWP      0x48u
#define HDA_REG_CORBRP      0x4Au
#define HDA_REG_CORBCTL     0x4Cu
#define HDA_REG_RIRBLBASE   0x50u
#define HDA_REG_RIRBUBASE   0x54u
#define HDA_REG_RIRBWP      0x58u
#define HDA_REG_RIRBCTL     0x5Cu

#define HDA_GCTL_CRST       (1u << 0)

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t length;
    uint32_t ioc;
} HdaBdlEntry;

int hda_is_pci_hda(uint8_t class_code, uint8_t subclass);
uint32_t hda_encode_verb(uint8_t cad, uint8_t nid, uint16_t verb_id, uint16_t payload);
int hda_prepare_bdl_entry(HdaBdlEntry *e, uint64_t buf_phys, uint32_t len, int ioc);
#endif
