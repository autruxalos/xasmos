#ifndef XASMOS_USB_MSC_Q_H
#define XASMOS_USB_MSC_Q_H

#include <stdint.h>

#define USB_MSC_CBW_SIG        0x43425355u
#define USB_MSC_CSW_SIG        0x53425355u
#define USB_MSC_CBW_LEN        31u
#define USB_MSC_CSW_LEN        13u
#define USB_MSC_DIR_OUT        0x00u
#define USB_MSC_DIR_IN         0x80u

#define USB_CLASS_MSC          0x08u
#define USB_SUBCLASS_SCSI      0x06u
#define USB_PROTO_BOT          0x50u

#define SCSI_CMD_TEST_UNIT_READY 0x00u
#define SCSI_CMD_REQUEST_SENSE   0x03u
#define SCSI_CMD_INQUIRY         0x12u
#define SCSI_CMD_READ_CAPACITY10 0x25u
#define SCSI_CMD_READ10          0x28u
#define SCSI_CMD_WRITE10         0x2Au

typedef struct {
    uint8_t  cfg_val;
    uint8_t  iface_num;
    uint8_t  ep_in;
    uint8_t  ep_out;
    uint16_t ep_in_mps;
    uint16_t ep_out_mps;
} UsbMscEndpoints;

void     usb_msc_build_cbw(uint8_t out[31], uint32_t tag, uint32_t data_len, uint8_t dir, uint8_t lun, const uint8_t *cdb, uint8_t cdb_len);
int      usb_msc_parse_csw(const uint8_t in[13], uint32_t expected_tag, uint32_t *residue_out);
void     usb_scsi_cdb_inquiry(uint8_t cdb[16], uint8_t alloc_len);
void     usb_scsi_cdb_test_unit_ready(uint8_t cdb[16]);
void     usb_scsi_cdb_read_capacity10(uint8_t cdb[16]);
void     usb_scsi_cdb_read10(uint8_t cdb[16], uint32_t lba, uint16_t blocks);
void     usb_scsi_cdb_write10(uint8_t cdb[16], uint32_t lba, uint16_t blocks);
int      usb_scsi_parse_capacity10(const uint8_t resp[8], uint64_t *blocks_out, uint32_t *blk_size_out);
int      usb_msc_find_endpoints(const uint8_t *cfg_desc, uint32_t total_len, UsbMscEndpoints *out);

#endif
