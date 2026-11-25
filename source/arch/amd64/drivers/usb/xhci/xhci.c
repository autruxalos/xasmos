#include "xhci.h"
#include "kmemory.h"
#include "paging.h"
#include "irqvec.h"
#include "lapic.h"

#ifndef SPIN_LIMIT
#define SPIN_LIMIT 50000000u
#endif

typedef struct {
    volatile uint8_t *regs;
    uint32_t cap_len, rt_off, db_off;
    uint64_t *dcbaa, dcbaa_phys;
    XhciErstEntry *erst;
    uint64_t erst_phys;
    XhciCmdRing cmd;
    XhciEvtRing evt;
    XhciInfo info;
} XhciCtrl;

static XhciCtrl g_xhci;
static volatile int g_xhci_irq_fired;
static void xhci_notice(void *ctx) { (void)ctx; g_xhci_irq_fired = 1; }
static uint32_t rd32(uint32_t off) { return *(volatile uint32_t *)(g_xhci.regs + off); }
static void wr32(uint32_t off, uint32_t v) { *(volatile uint32_t *)(g_xhci.regs + off) = v; }
static void wr64(uint32_t off, uint64_t v) { wr32(off, (uint32_t)v); wr32(off + 4u, (uint32_t)(v >> 32)); }
static uint32_t op_rd32(uint32_t off) { return rd32(g_xhci.cap_len + off); }
static void op_wr32(uint32_t off, uint32_t v) { wr32(g_xhci.cap_len + off, v); }
static void op_wr64(uint32_t off, uint64_t v) { wr64(g_xhci.cap_len + off, v); }
static uint32_t ir0_rd32(uint32_t off) { return rd32(g_xhci.rt_off + 0x20u + off); }
static void ir0_wr32(uint32_t off, uint32_t v) { wr32(g_xhci.rt_off + 0x20u + off, v); }
static void ir0_wr64(uint32_t off, uint64_t v) { wr64(g_xhci.rt_off + 0x20u + off, v); }
static void ring_doorbell(uint32_t slot, uint32_t target) { wr32(g_xhci.db_off + slot * 4u, target); }
static int msix_program(const PciDev *d) {
    PciMsix mx; UINT64 virt = 0;
    if (pci_msix_info(d, &mx) != 0) return -1;
    if (xpt_map_uncached(d->bar[mx.bir] + mx.table_off, (UINT64)mx.entries * 16u, &virt) != 0) return -1;
    int vec = irq_vector_alloc(xhci_notice, 0);
    if (vec < 0) return -1;
    pci_msix_control(d, mx.cap, 1, 1);
    volatile uint32_t *e = (volatile uint32_t *)(UINTN)virt;
    uint32_t addr = 0xFEE00000u | (lapic_id() << 12);
    e[0] = addr; e[1] = 0; e[2] = (uint32_t)vec; e[3] = 0;
    pci_msix_control(d, mx.cap, 1, 0);
    if (e[0] != addr || e[2] != (uint32_t)vec) return -1;
    return vec;
}
static void bios_handoff(uint32_t xecp_off) {
    uint32_t off = xecp_off;
    for (uint32_t hops = 0; off != 0u && hops < 64u; hops++) {
        uint32_t hdr = rd32(off), id = hdr & 0xFFu, next = (hdr >> 8) & 0xFFu;
        if (id == XHCI_XECP_LEGACY) {
            if (hdr & XHCI_USBLEGSUP_BIOS) {
                wr32(off, hdr | XHCI_USBLEGSUP_OS);
                for (uint32_t i = 0; i < SPIN_LIMIT && (rd32(off) & XHCI_USBLEGSUP_BIOS); i++) __asm__ volatile("pause");
            }
            break;
        }
        if (next == 0u) break;
        off += next << 2;
    }
}
static int alloc_zero_page(uint64_t *phys_out, void **virt_out) {
    UINT64 pa = phys_alloc_frames(1); if (!pa) return -1;
    uint8_t *p = (uint8_t *)(UINTN)pa;
    for (uint32_t i = 0; i < 4096u; i++) p[i] = 0;
    *phys_out = (uint64_t)pa; *virt_out = p; return 0;
}
int xhci_send_cmd(uint64_t param, uint32_t status, uint32_t type, uint32_t flags, XhciTrb *evt_out) {
    if (!g_xhci.regs) return XHCI_E_NODEV;
    xhci_cmd_ring_push(&g_xhci.cmd, param, status, type, flags | XHCI_TRB_IOC);
    __asm__ volatile("mfence" ::: "memory");
    ring_doorbell(0u, 0u);
    for (uint32_t i = 0; i < SPIN_LIMIT; i++) {
        uint64_t erdp = 0; XhciTrb ev;
        if (xhci_evt_ring_poll(&g_xhci.evt, &ev, &erdp)) {
            ir0_wr64(XHCI_IR_ERDP, erdp);
            uint32_t iman = ir0_rd32(XHCI_IR_IMAN);
            if (iman & XHCI_IMAN_IP) ir0_wr32(XHCI_IR_IMAN, iman | XHCI_IMAN_IP);
            if (evt_out) *evt_out = ev;
            return (xhci_evt_cc(&ev) == XHCI_CC_SUCCESS) ? XHCI_OK : XHCI_E_CMD;
        }
        __asm__ volatile("pause");
    }
    return XHCI_E_TIMEOUT;
}

int xhci_init(const PciDev *d, XhciInfo *out) {
    if (!d || d->bar[0] == 0) return XHCI_E_NODEV;
    pci_enable(d);
    UINT64 mmio = 0;
    if (xpt_map_uncached(d->bar[0], 0x10000u, &mmio) != 0) return XHCI_E_MMIO;
    g_xhci.regs = (volatile uint8_t *)(UINTN)mmio;
    uint32_t cap0 = rd32(XHCI_CAP_CAPLENGTH), hcs1 = rd32(XHCI_CAP_HCSPARAMS1);
    uint32_t hcs2 = rd32(XHCI_CAP_HCSPARAMS2), hcc1 = rd32(XHCI_CAP_HCCPARAMS1);
    g_xhci.cap_len = xhci_cap_length(cap0);
    g_xhci.db_off  = rd32(XHCI_CAP_DBOFF) & ~0x3u;
    g_xhci.rt_off  = rd32(XHCI_CAP_RTSOFF) & ~0x1Fu;
    uint32_t xecp = xhci_xecp_offset(hcc1);
    if (xecp) bios_handoff(xecp);
    op_wr32(XHCI_OP_USBCMD, op_rd32(XHCI_OP_USBCMD) & ~XHCI_CMD_RS);
    uint32_t t = 0;
    while ((op_rd32(XHCI_OP_USBSTS) & XHCI_STS_HCH) == 0u) { if (++t >= SPIN_LIMIT) return XHCI_E_TIMEOUT; __asm__ volatile("pause"); }
    op_wr32(XHCI_OP_USBCMD, op_rd32(XHCI_OP_USBCMD) | XHCI_CMD_HCRST);
    t = 0;
    while ((op_rd32(XHCI_OP_USBCMD) & XHCI_CMD_HCRST) != 0u || (op_rd32(XHCI_OP_USBSTS) & XHCI_STS_CNR) != 0u) { if (++t >= SPIN_LIMIT) return XHCI_E_TIMEOUT; __asm__ volatile("pause"); }
    uint32_t slots = xhci_max_slots(hcs1); if (slots > XHCI_MAX_SLOTS_CAP) slots = XHCI_MAX_SLOTS_CAP;
    op_wr32(XHCI_OP_CONFIG, slots);
    void *dcbaa_v = 0, *cmd_v = 0, *evt_v = 0, *erst_v = 0;
    if (alloc_zero_page(&g_xhci.dcbaa_phys, &dcbaa_v) != 0) return XHCI_E_NOMEM;
    if (alloc_zero_page(&g_xhci.cmd.phys, &cmd_v) != 0) return XHCI_E_NOMEM;
    if (alloc_zero_page(&g_xhci.evt.phys, &evt_v) != 0) return XHCI_E_NOMEM;
    if (alloc_zero_page(&g_xhci.erst_phys, &erst_v) != 0) return XHCI_E_NOMEM;
    g_xhci.dcbaa = (uint64_t *)dcbaa_v; g_xhci.erst = (XhciErstEntry *)erst_v;
    uint32_t sp_cnt = xhci_scratchpad_count(hcs2);
    if (sp_cnt > 0u) {
        uint64_t sp_arr_pa = 0; void *sp_arr_v = 0;
        if (alloc_zero_page(&sp_arr_pa, &sp_arr_v) != 0) return XHCI_E_NOMEM;
        uint64_t *sp_arr = (uint64_t *)sp_arr_v;
        uint32_t cap_sp = sp_cnt > 512u ? 512u : sp_cnt;
        for (uint32_t i = 0; i < cap_sp; i++) { uint64_t bpa = 0; void *bv = 0; if (alloc_zero_page(&bpa, &bv) != 0) return XHCI_E_NOMEM; sp_arr[i] = bpa; }
        g_xhci.dcbaa[0] = sp_arr_pa;
    }
    op_wr64(XHCI_OP_DCBAAP, g_xhci.dcbaa_phys);
    xhci_cmd_ring_init(&g_xhci.cmd, (XhciTrb *)cmd_v, g_xhci.cmd.phys, XHCI_RING_TRBS);
    op_wr64(XHCI_OP_CRCR, g_xhci.cmd.phys | 1ULL);
    xhci_evt_ring_init(&g_xhci.evt, (XhciTrb *)evt_v, g_xhci.evt.phys, XHCI_RING_TRBS);
    g_xhci.erst[0].seg_addr = g_xhci.evt.phys; g_xhci.erst[0].seg_size = XHCI_RING_TRBS; g_xhci.erst[0].rsvd = 0u;
    ir0_wr32(XHCI_IR_ERSTSZ, 1u); ir0_wr64(XHCI_IR_ERDP, g_xhci.evt.phys | XHCI_ERDP_EHB);
    ir0_wr64(XHCI_IR_ERSTBA, g_xhci.erst_phys); ir0_wr32(XHCI_IR_IMOD, 0u); ir0_wr32(XHCI_IR_IMAN, XHCI_IMAN_IE | XHCI_IMAN_IP);
    int vec = msix_program(d);
    op_wr32(XHCI_OP_USBCMD, XHCI_CMD_RS | XHCI_CMD_INTE);
    t = 0;
    while ((op_rd32(XHCI_OP_USBSTS) & XHCI_STS_HCH) != 0u) { if (++t >= SPIN_LIMIT) return XHCI_E_TIMEOUT; __asm__ volatile("pause"); }
    XhciTrb noop_ev;
    int noop_rc = xhci_send_cmd(0ULL, 0u, XHCI_TRB_CMD_NOOP, 0u, &noop_ev);
    uint32_t max_ports = xhci_max_ports(hcs1), conn = 0u;
    for (uint32_t p = 0; p < max_ports; p++) if (xhci_port_connected(op_rd32(XHCI_OP_PORTSC_BASE + p * XHCI_OP_PORT_STRIDE))) conn++;
    g_xhci.info.active = 1; g_xhci.info.hci_version = xhci_hci_version(cap0); g_xhci.info.max_slots = (uint8_t)slots;
    g_xhci.info.max_ports = (uint8_t)max_ports; g_xhci.info.ctx_size = (uint8_t)xhci_context_size(hcc1);
    g_xhci.info.scratchpads = sp_cnt; g_xhci.info.irq_vec = vec; g_xhci.info.connected_ports = conn;
    g_xhci.info.noop_cc = (noop_rc == XHCI_OK) ? xhci_evt_cc(&noop_ev) : 0u;
    if (out) *out = g_xhci.info;
    return noop_rc;
}
int xhci_probe_and_init(XhciInfo *out) { pci_scan(); const PciDev *d = pci_find(0x0Cu, 0x03u, 0x30u, 0u); return d ? xhci_init(d, out) : XHCI_E_NODEV; }
int xhci_get_info(XhciInfo *out) { if (!g_xhci.info.active) return XHCI_E_NODEV; if (out) *out = g_xhci.info; return XHCI_OK; }
int xhci_irq_vector(void) { return g_xhci.info.active ? g_xhci.info.irq_vec : -1; }
