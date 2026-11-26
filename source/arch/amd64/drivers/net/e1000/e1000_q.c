#include "e1000_q.h"

int e1000_is_supported_device(uint16_t vendor, uint16_t device) {
    if (vendor != E1000_VENDOR_INTEL) return 0;
    switch (device) {
        case E1000_DEV_82540EM:
        case E1000_DEV_82545EM:
        case E1000_DEV_82577LM:
        case E1000_DEV_I217LM:
        case E1000_DEV_I219LM:
            return 1;
        default:
            return 0;
    }
}

void e1000_decode_mac_ral_rah(uint32_t ral, uint32_t rah, uint8_t mac_out[6]) {
    if (!mac_out) return;
    mac_out[0] = (uint8_t)(ral & 0xFFu);
    mac_out[1] = (uint8_t)((ral >> 8) & 0xFFu);
    mac_out[2] = (uint8_t)((ral >> 16) & 0xFFu);
    mac_out[3] = (uint8_t)((ral >> 24) & 0xFFu);
    mac_out[4] = (uint8_t)(rah & 0xFFu);
    mac_out[5] = (uint8_t)((rah >> 8) & 0xFFu);
}

int e1000_prepare_tx_desc(E1000TxDesc *desc, uint64_t buf_phys, uint16_t len) {
    if (!desc || buf_phys == 0ULL || len == 0u || len > E1000_PKT_BUF_SIZE) return -1;
    desc->addr = buf_phys;
    desc->length = len;
    desc->cso = 0u;
    desc->cmd = (uint8_t)(E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS);
    desc->status = 0u;
    desc->css = 0u;
    desc->special = 0u;
    return 0;
}

int e1000_check_rx_desc(const E1000RxDesc *desc, uint16_t *len_out) {
    if (!desc) return -1;
    if (!(desc->status & E1000_RXD_STAT_DD)) return 0;
    if (desc->errors != 0u || !(desc->status & E1000_RXD_STAT_EOP)) return -2;
    if (len_out) *len_out = desc->length;
    return 1;
}
