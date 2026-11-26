#include "usb_hid.h"
#include "xhci.h"
#include "kmemory.h"
static struct { UsbHidInfo info; UsbHidEndpoint kbd_ep, mouse_ep; UsbHidKbdState kbd_st; uint8_t *dma; uint64_t dma_pa; char rbuf[32]; uint32_t rhead, rtail; } g_hid;
int usb_hid_init(UsbHidInfo *out) {
    XhciInfo xi;
    if (xhci_get_info(&xi) != XHCI_OK || !xi.active) return -1;
    UINT64 pa = phys_alloc_frames(1);
    if (!pa) return -2;
    g_hid.dma = (uint8_t *)(UINTN)pa; g_hid.dma_pa = (uint64_t)pa;
    for (uint32_t i = 0; i < 4096u; i++) g_hid.dma[i] = 0u;
    uint32_t p = 1u;
    while (p <= (uint32_t)xi.max_ports) {
        uint32_t port = 0u, speed = 0u, slot = 0u;
        if (xhci_find_connected_port_from(p, &port, &speed) != XHCI_OK) break;
        p = port + 1u;
        if (xhci_port_reset(port, &speed) != XHCI_OK) continue;
        if (xhci_dev_open(port, speed, &slot) != XHCI_OK) continue;
        if (xhci_ctrl_xfer(slot, 0x80u, 6u, 0x0100u, 0u, 18u, g_hid.dma_pa) != XHCI_OK) continue;
        if (xhci_ctrl_xfer(slot, 0x80u, 6u, 0x0200u, 0u, 9u, g_hid.dma_pa) != XHCI_OK) continue;
        uint16_t tot = (uint16_t)g_hid.dma[2] | ((uint16_t)g_hid.dma[3] << 8);
        if (tot < 9u || tot > 512u) continue;
        if (xhci_ctrl_xfer(slot, 0x80u, 6u, 0x0200u, 0u, tot, g_hid.dma_pa) != XHCI_OK) continue;
        UsbHidEndpoint ep;
        if (!g_hid.info.kbd_active && usb_hid_find_boot_iface(g_hid.dma, tot, USB_PROTO_KEYBOARD, &ep) == 0) {
            if (xhci_ctrl_xfer(slot, 0x00u, 9u, ep.cfg_val, 0u, 0u, 0ULL) == XHCI_OK &&
                xhci_configure_intr_in(slot, ep.ep_in, ep.ep_in_mps, ep.interval) == XHCI_OK) {
                (void)xhci_ctrl_xfer(slot, 0x21u, USB_HID_REQ_SET_PROTOCOL, 0u, ep.iface_num, 0u, 0ULL);
                (void)xhci_ctrl_xfer(slot, 0x21u, USB_HID_REQ_SET_IDLE, 0u, ep.iface_num, 0u, 0ULL);
                g_hid.kbd_ep = ep; g_hid.info.kbd_active = 1; g_hid.info.kbd_slot = slot; g_hid.info.kbd_port = (uint8_t)port; g_hid.info.kbd_ep = ep.ep_in;
            }
        } else if (!g_hid.info.mouse_active && usb_hid_find_boot_iface(g_hid.dma, tot, USB_PROTO_MOUSE, &ep) == 0) {
            if (xhci_ctrl_xfer(slot, 0x00u, 9u, ep.cfg_val, 0u, 0u, 0ULL) == XHCI_OK &&
                xhci_configure_intr_in(slot, ep.ep_in, ep.ep_in_mps, ep.interval) == XHCI_OK) {
                (void)xhci_ctrl_xfer(slot, 0x21u, USB_HID_REQ_SET_PROTOCOL, 0u, ep.iface_num, 0u, 0ULL);
                g_hid.mouse_ep = ep; g_hid.info.mouse_active = 1; g_hid.info.mouse_slot = slot; g_hid.info.mouse_port = (uint8_t)port; g_hid.info.mouse_ep = ep.ep_in;
            }
        }
    }
    if (out) *out = g_hid.info;
    return (g_hid.info.kbd_active || g_hid.info.mouse_active) ? 0 : -3;
}
int usb_hid_get_info(UsbHidInfo *out) { if (!g_hid.info.kbd_active && !g_hid.info.mouse_active) return -1; if (out) *out = g_hid.info; return 0; }
int usb_hid_poll_char(char *ch_out) {
    if (!ch_out) return 0;
    if (g_hid.rhead != g_hid.rtail) { *ch_out = g_hid.rbuf[g_hid.rtail & 31u]; g_hid.rtail++; return 1; }
    if (!g_hid.info.kbd_active) return 0;
    uint8_t *rep = g_hid.dma + 512u;
    if (xhci_intr_xfer(g_hid.info.kbd_slot, g_hid.info.kbd_ep, g_hid.dma_pa + 512ULL, 8u) == XHCI_OK) {
        char tmp[6]; int n = usb_hid_kbd_decode(&g_hid.kbd_st, rep, tmp, 6u);
        for (int i = 0; i < n; i++) { g_hid.rbuf[g_hid.rhead & 31u] = tmp[i]; g_hid.rhead++; }
    }
    if (g_hid.rhead != g_hid.rtail) { *ch_out = g_hid.rbuf[g_hid.rtail & 31u]; g_hid.rtail++; return 1; }
    return 0;
}
