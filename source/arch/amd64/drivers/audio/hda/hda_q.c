#include "hda_q.h"

int hda_is_pci_hda(uint8_t class_code, uint8_t subclass) {
    return (class_code == 0x04u && subclass == 0x03u) ? 1 : 0;
}

uint32_t hda_encode_verb(uint8_t cad, uint8_t nid, uint16_t verb_id, uint16_t payload) {
    return (((uint32_t)(cad & 0x0Fu)) << 28) |
           (((uint32_t)(nid & 0x7Fu)) << 20) |
           (((uint32_t)(verb_id & 0x0FFFu)) << 8) |
           ((uint32_t)(payload & 0xFFu));
}

int hda_prepare_bdl_entry(HdaBdlEntry *e, uint64_t buf_phys, uint32_t len, int ioc) {
    if (!e || buf_phys == 0ULL || len == 0u) return -1;
    e->addr = buf_phys;
    e->length = len;
    e->ioc = ioc ? 1u : 0u;
    return 0;
}
