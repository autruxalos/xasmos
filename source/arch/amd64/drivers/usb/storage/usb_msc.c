#include "usb_msc.h"
#include "usb_msc_q.h"
#include "xhci.h"
#include "kmemory.h"
typedef struct {
    UsbMscInfo       info;
    UsbMscEndpoints  ep;
    uint32_t         tag;
    uint8_t         *dma;
    uint64_t         dma_pa;
} UsbMscState;
static UsbMscState g_msc;
static int msc_bot_cmd(const uint8_t *cdb, uint8_t cdb_len, uint8_t dir, uint32_t len) {
    uint32_t tag = ++g_msc.tag;
    uint8_t *cbw = g_msc.dma;
    uint8_t *csw = g_msc.dma + 64u;
    uint64_t data_pa = g_msc.dma_pa + 512u;
    usb_msc_build_cbw(cbw, tag, len, dir, 0u, cdb, cdb_len);
    if (xhci_bulk_xfer(g_msc.info.slot, g_msc.ep.ep_out, g_msc.dma_pa, USB_MSC_CBW_LEN) != XHCI_OK) return -1;
    if (len > 0u) {
        uint8_t ep = (dir == USB_MSC_DIR_IN) ? g_msc.ep.ep_in : g_msc.ep.ep_out;
        if (xhci_bulk_xfer(g_msc.info.slot, ep, data_pa, len) != XHCI_OK) return -2;
    }
    if (xhci_bulk_xfer(g_msc.info.slot, g_msc.ep.ep_in, g_msc.dma_pa + 64ULL, USB_MSC_CSW_LEN) != XHCI_OK) return -3;
    return usb_msc_parse_csw(csw, tag, 0);
}
int usb_msc_init(UsbMscInfo *out) {
    uint32_t port = 0u, speed = 0u, slot = 0u;
    if (xhci_find_connected_port(&port, &speed) != XHCI_OK) return -1;
    if (xhci_port_reset(port, &speed) != XHCI_OK) return -2;
    if (xhci_dev_open(port, speed, &slot) != XHCI_OK) return -3;
    UINT64 pa = phys_alloc_frames(2);
    if (!pa) return -4;
    g_msc.dma = (uint8_t *)(UINTN)pa; g_msc.dma_pa = (uint64_t)pa;
    for (uint32_t i = 0; i < 8192u; i++) g_msc.dma[i] = 0u;
    if (xhci_ctrl_xfer(slot, 0x80u, 6u, 0x0100u, 0u, 18u, g_msc.dma_pa) != XHCI_OK) return -5;
    if (xhci_ctrl_xfer(slot, 0x80u, 6u, 0x0200u, 0u, 9u, g_msc.dma_pa) != XHCI_OK) return -6;
    uint16_t tot = (uint16_t)g_msc.dma[2] | ((uint16_t)g_msc.dma[3] << 8);
    if (tot < 9u || tot > 512u) tot = 64u;
    if (xhci_ctrl_xfer(slot, 0x80u, 6u, 0x0200u, 0u, tot, g_msc.dma_pa) != XHCI_OK) return -7;
    if (usb_msc_find_endpoints(g_msc.dma, tot, &g_msc.ep) != 0) return -8;
    if (xhci_ctrl_xfer(slot, 0x00u, 9u, g_msc.ep.cfg_val, 0u, 0u, 0ULL) != XHCI_OK) return -9;
    if (xhci_configure_bulk(slot, g_msc.ep.ep_in, g_msc.ep.ep_in_mps, g_msc.ep.ep_out, g_msc.ep.ep_out_mps) != XHCI_OK) return -10;
    g_msc.info.active = 1; g_msc.info.slot = slot; g_msc.info.port = (uint8_t)port; g_msc.info.speed = (uint8_t)speed;
    uint8_t cdb[16];
    usb_scsi_cdb_inquiry(cdb, 36u);
    if (msc_bot_cmd(cdb, 6u, USB_MSC_DIR_IN, 36u) != 0) return -11;
    for (int t = 0; t < 4; t++) {
        usb_scsi_cdb_test_unit_ready(cdb);
        if (msc_bot_cmd(cdb, 6u, USB_MSC_DIR_IN, 0u) == 0) break;
    }
    usb_scsi_cdb_read_capacity10(cdb);
    if (msc_bot_cmd(cdb, 10u, USB_MSC_DIR_IN, 8u) != 0) return -12;
    if (usb_scsi_parse_capacity10(g_msc.dma + 512u, &g_msc.info.blocks, &g_msc.info.block_size) != 0) return -13;
    if (out) *out = g_msc.info;
    return 0;
}
int usb_msc_read_blocks(uint64_t lba, uint16_t count, void *buf) {
    if (!g_msc.info.active || !buf || count == 0u) return -1;
    uint32_t bytes = (uint32_t)count * g_msc.info.block_size;
    if (bytes > 4096u) return -2;
    uint8_t cdb[16]; usb_scsi_cdb_read10(cdb, (uint32_t)lba, count);
    if (msc_bot_cmd(cdb, 10u, USB_MSC_DIR_IN, bytes) != 0) return -3;
    uint8_t *dst = (uint8_t *)buf, *src = g_msc.dma + 512u;
    for (uint32_t i = 0; i < bytes; i++) dst[i] = src[i];
    return 0;
}
int usb_msc_write_blocks(uint64_t lba, uint16_t count, const void *buf) {
    if (!g_msc.info.active || !buf || count == 0u) return -1;
    uint32_t bytes = (uint32_t)count * g_msc.info.block_size;
    if (bytes > 4096u) return -2;
    const uint8_t *src = (const uint8_t *)buf; uint8_t *dst = g_msc.dma + 512u;
    for (uint32_t i = 0; i < bytes; i++) dst[i] = src[i];
    uint8_t cdb[16]; usb_scsi_cdb_write10(cdb, (uint32_t)lba, count);
    return msc_bot_cmd(cdb, 10u, USB_MSC_DIR_OUT, bytes);
}
