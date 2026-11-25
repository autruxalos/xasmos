#include "usb_msc_q.h"
static void put_le32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static uint32_t get_le32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint32_t get_be32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|(uint32_t)p[3]; }
static void put_be32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
static void put_be16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
void usb_msc_build_cbw(uint8_t out[31], uint32_t tag, uint32_t data_len, uint8_t dir, uint8_t lun, const uint8_t *cdb, uint8_t cdb_len) {
    for (uint32_t i = 0; i < 31u; i++) out[i] = 0u;
    put_le32(&out[0], USB_MSC_CBW_SIG);
    put_le32(&out[4], tag);
    put_le32(&out[8], data_len);
    out[12] = dir;
    out[13] = lun & 0x0Fu;
    uint8_t clen = cdb_len > 16u ? 16u : cdb_len;
    out[14] = clen;
    for (uint8_t i = 0; i < clen; i++) out[15u + i] = cdb[i];
}
int usb_msc_parse_csw(const uint8_t in[13], uint32_t expected_tag, uint32_t *residue_out) {
    if (get_le32(&in[0]) != USB_MSC_CSW_SIG) return -1;
    if (get_le32(&in[4]) != expected_tag) return -2;
    if (residue_out) *residue_out = get_le32(&in[8]);
    return (in[12] == 0u) ? 0 : -(int)in[12] - 10;
}
void usb_scsi_cdb_inquiry(uint8_t cdb[16], uint8_t alloc_len) {
    for (int i = 0; i < 16; i++) cdb[i] = 0u;
    cdb[0] = SCSI_CMD_INQUIRY; cdb[4] = alloc_len;
}
void usb_scsi_cdb_test_unit_ready(uint8_t cdb[16]) {
    for (int i = 0; i < 16; i++) cdb[i] = 0u;
    cdb[0] = SCSI_CMD_TEST_UNIT_READY;
}
void usb_scsi_cdb_read_capacity10(uint8_t cdb[16]) {
    for (int i = 0; i < 16; i++) cdb[i] = 0u;
    cdb[0] = SCSI_CMD_READ_CAPACITY10;
}
void usb_scsi_cdb_read10(uint8_t cdb[16], uint32_t lba, uint16_t blocks) {
    for (int i = 0; i < 16; i++) cdb[i] = 0u;
    cdb[0] = SCSI_CMD_READ10; put_be32(&cdb[2], lba); put_be16(&cdb[7], blocks);
}
void usb_scsi_cdb_write10(uint8_t cdb[16], uint32_t lba, uint16_t blocks) {
    for (int i = 0; i < 16; i++) cdb[i] = 0u;
    cdb[0] = SCSI_CMD_WRITE10; put_be32(&cdb[2], lba); put_be16(&cdb[7], blocks);
}
int usb_scsi_parse_capacity10(const uint8_t resp[8], uint64_t *blocks_out, uint32_t *blk_size_out) {
    uint32_t last_lba = get_be32(&resp[0]);
    uint32_t bsz = get_be32(&resp[4]);
    if (bsz == 0u) return -1;
    if (blocks_out) *blocks_out = (uint64_t)last_lba + 1ULL;
    if (blk_size_out) *blk_size_out = bsz;
    return 0;
}
int usb_msc_find_endpoints(const uint8_t *cfg_desc, uint32_t total_len, UsbMscEndpoints *out) {
    if (!cfg_desc || !out || total_len < 9u) return -1;
    uint8_t cfg_val = cfg_desc[5];
    int in_msc = 0; uint8_t iface_num = 0, ep_in = 0, ep_out = 0; uint16_t mps_in = 0, mps_out = 0;
    uint32_t off = 0u;
    while (off + 2u <= total_len) {
        uint8_t len = cfg_desc[off], dtype = cfg_desc[off + 1u];
        if (len < 2u || off + len > total_len) break;
        if (dtype == 4u && len >= 9u) {
            in_msc = (cfg_desc[off+5u] == USB_CLASS_MSC && cfg_desc[off+6u] == USB_SUBCLASS_SCSI && cfg_desc[off+7u] == USB_PROTO_BOT);
            if (in_msc) { iface_num = cfg_desc[off+2u]; ep_in = 0; ep_out = 0; }
        } else if (dtype == 5u && len >= 7u && in_msc) {
            uint8_t addr = cfg_desc[off+2u], attr = cfg_desc[off+3u];
            uint16_t mps = (uint16_t)cfg_desc[off+4u] | ((uint16_t)cfg_desc[off+5u] << 8);
            if ((attr & 0x03u) == 2u) {
                if (addr & 0x80u) { ep_in = addr; mps_in = mps; } else { ep_out = addr; mps_out = mps; }
                if (ep_in && ep_out) {
                    out->cfg_val = cfg_val; out->iface_num = iface_num;
                    out->ep_in = ep_in; out->ep_out = ep_out;
                    out->ep_in_mps = mps_in; out->ep_out_mps = mps_out;
                    return 0;
                }
            }
        }
        off += len;
    }
    return -1;
}
