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
            if (xhci_trb_type(&ev) != XHCI_TRB_CMD_COMPLETION) continue; if (evt_out) *evt_out = ev;
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

typedef struct {
    uint8_t     *out_ctx;
    uint64_t     out_ctx_pa;
    uint8_t     *in_ctx;
    uint64_t     in_ctx_pa;
    uint32_t     speed;
    uint32_t     port;
    XhciCmdRing  ep_ring[32];
} XhciDevState;
static XhciDevState g_xdev[XHCI_MAX_SLOTS_CAP + 1u];
static uint32_t *ctx_dw(uint8_t *base, uint32_t idx) { return (uint32_t *)(base + idx * g_xhci.info.ctx_size); }
static uint32_t ep_dci(uint8_t ep_addr) { uint32_t n = ep_addr & 0x0Fu; return n * 2u + ((ep_addr & 0x80u) ? 1u : 0u); }
static uint16_t default_ep0_mps(uint32_t speed) { if (speed == 4u || speed == 5u) return 512u; if (speed == 2u) return 8u; return 64u; }
static int xhci_wait_xfer(uint32_t slot, uint32_t dci) {
    for (uint32_t i = 0; i < SPIN_LIMIT; i++) {
        uint64_t erdp = 0; XhciTrb ev;
        if (xhci_evt_ring_poll(&g_xhci.evt, &ev, &erdp)) {
            ir0_wr64(XHCI_IR_ERDP, erdp);
            uint32_t iman = ir0_rd32(XHCI_IR_IMAN);
            if (iman & XHCI_IMAN_IP) ir0_wr32(XHCI_IR_IMAN, iman | XHCI_IMAN_IP);
            if (xhci_trb_type(&ev) == XHCI_TRB_TRANSFER_EVT && xhci_evt_slot(&ev) == slot) {
                uint32_t cc = xhci_evt_cc(&ev);
                (void)dci;
                return (cc == XHCI_CC_SUCCESS || cc == 13u) ? XHCI_OK : XHCI_E_CMD;
            }
        }
        __asm__ volatile("pause");
    }
    return XHCI_E_TIMEOUT;
}
int xhci_find_connected_port(uint32_t *port_out, uint32_t *speed_out) {
    if (!g_xhci.regs) return XHCI_E_NODEV;
    for (uint32_t p = 0; p < g_xhci.info.max_ports; p++) {
        uint32_t psc = op_rd32(XHCI_OP_PORTSC_BASE + p * XHCI_OP_PORT_STRIDE);
        if (xhci_port_connected(psc)) {
            if (port_out) *port_out = p + 1u;
            if (speed_out) *speed_out = xhci_port_speed(psc);
            return XHCI_OK;
        }
    }
    return XHCI_E_NODEV;
}
int xhci_port_reset(uint32_t port, uint32_t *speed_out) {
    if (!g_xhci.regs || port == 0u || port > g_xhci.info.max_ports) return XHCI_E_NODEV;
    uint32_t off = XHCI_OP_PORTSC_BASE + (port - 1u) * XHCI_OP_PORT_STRIDE;
    uint32_t psc = op_rd32(off);
    if (!xhci_port_connected(psc)) return XHCI_E_NODEV;
    op_wr32(off, xhci_portsc_write_val(psc, XHCI_PORTSC_PR, XHCI_PORTSC_CSC | XHCI_PORTSC_PRC));
    for (uint32_t i = 0; i < SPIN_LIMIT; i++) {
        psc = op_rd32(off);
        if ((psc & XHCI_PORTSC_PRC) != 0u && (psc & XHCI_PORTSC_PR) == 0u) {
            op_wr32(off, xhci_portsc_write_val(psc, 0u, XHCI_PORTSC_PRC | XHCI_PORTSC_CSC));
            if (speed_out) *speed_out = xhci_port_speed(op_rd32(off));
            return xhci_port_enabled(op_rd32(off)) ? XHCI_OK : XHCI_E_CMD;
        }
        __asm__ volatile("pause");
    }
    return XHCI_E_TIMEOUT;
}
int xhci_dev_open(uint32_t port, uint32_t speed, uint32_t *slot_out) {
    XhciTrb ev;
    if (xhci_send_cmd(0ULL, 0u, XHCI_TRB_ENABLE_SLOT, 0u, &ev) != XHCI_OK) return XHCI_E_CMD;
    uint32_t slot = xhci_evt_slot(&ev);
    if (slot == 0u || slot > XHCI_MAX_SLOTS_CAP) return XHCI_E_CMD;
    XhciDevState *ds = &g_xdev[slot];
    void *out_v = 0, *in_v = 0, *ep0_v = 0; uint64_t ep0_pa = 0;
    if (alloc_zero_page(&ds->out_ctx_pa, &out_v) != 0) return XHCI_E_NOMEM;
    if (alloc_zero_page(&ds->in_ctx_pa, &in_v) != 0) return XHCI_E_NOMEM;
    if (alloc_zero_page(&ep0_pa, &ep0_v) != 0) return XHCI_E_NOMEM;
    ds->out_ctx = (uint8_t *)out_v; ds->in_ctx = (uint8_t *)in_v; ds->speed = speed; ds->port = port;
    xhci_cmd_ring_init(&ds->ep_ring[1], (XhciTrb *)ep0_v, ep0_pa, XHCI_RING_TRBS);
    uint32_t *icc = ctx_dw(ds->in_ctx, 0u);
    icc[1] = 0x3u;
    uint32_t *sc = ctx_dw(ds->in_ctx, 1u);
    sc[0] = (1u << 27) | ((speed & 0xFu) << 20);
    sc[1] = (port & 0xFFu) << 16;
    uint32_t *ep0 = ctx_dw(ds->in_ctx, 2u);
    ep0[1] = (3u << 1) | (4u << 3) | ((uint32_t)default_ep0_mps(speed) << 16);
    ep0[2] = (uint32_t)(ep0_pa | 1ULL);
    ep0[3] = (uint32_t)(ep0_pa >> 32);
    ep0[4] = 8u;
    g_xhci.dcbaa[slot] = ds->out_ctx_pa;
    if (xhci_send_cmd(ds->in_ctx_pa, 0u, XHCI_TRB_ADDRESS_DEV, (slot << 24), &ev) != XHCI_OK) return XHCI_E_CMD;
    if (slot_out) *slot_out = slot;
    return XHCI_OK;
}
int xhci_ctrl_xfer(uint32_t slot, uint8_t req_type, uint8_t req, uint16_t val, uint16_t idx, uint16_t len, uint64_t data_phys) {
    if (slot == 0u || slot > XHCI_MAX_SLOTS_CAP) return XHCI_E_NODEV;
    XhciCmdRing *r = &g_xdev[slot].ep_ring[1];
    uint64_t setup = (uint64_t)req_type | ((uint64_t)req << 8) | ((uint64_t)val << 16) | ((uint64_t)idx << 32) | ((uint64_t)len << 48);
    uint32_t trt = (len == 0u) ? 0u : ((req_type & 0x80u) ? 3u : 2u);
    xhci_cmd_ring_push(r, setup, 8u, XHCI_TRB_SETUP_STAGE, (1u << 6) | (trt << 16));
    if (len > 0u) {
        uint32_t dir = (req_type & 0x80u) ? (1u << 16) : 0u;
        xhci_cmd_ring_push(r, data_phys, (uint32_t)len, XHCI_TRB_DATA_STAGE, dir);
    }
    uint32_t sdir = (len > 0u && (req_type & 0x80u)) ? 0u : (1u << 16);
    xhci_cmd_ring_push(r, 0ULL, 0u, XHCI_TRB_STATUS_STAGE, sdir | XHCI_TRB_IOC);
    __asm__ volatile("mfence" ::: "memory");
    ring_doorbell(slot, 1u);
    return xhci_wait_xfer(slot, 1u);
}
int xhci_configure_bulk(uint32_t slot, uint8_t ep_in, uint16_t mps_in, uint8_t ep_out, uint16_t mps_out) {
    if (slot == 0u || slot > XHCI_MAX_SLOTS_CAP) return XHCI_E_NODEV;
    XhciDevState *ds = &g_xdev[slot];
    uint32_t dci_in = ep_dci(ep_in), dci_out = ep_dci(ep_out);
    uint32_t max_dci = dci_in > dci_out ? dci_in : dci_out;
    for (uint32_t i = 0; i < 4096u; i++) ds->in_ctx[i] = 0u;
    uint32_t *icc = ctx_dw(ds->in_ctx, 0u);
    icc[1] = 1u | (1u << dci_in) | (1u << dci_out);
    uint32_t *sc = ctx_dw(ds->in_ctx, 1u);
    sc[0] = (max_dci << 27) | ((ds->speed & 0xFu) << 20);
    sc[1] = (ds->port & 0xFFu) << 16;
    void *rin_v = 0, *rout_v = 0; uint64_t rin_pa = 0, rout_pa = 0;
    if (alloc_zero_page(&rin_pa, &rin_v) != 0 || alloc_zero_page(&rout_pa, &rout_v) != 0) return XHCI_E_NOMEM;
    xhci_cmd_ring_init(&ds->ep_ring[dci_in], (XhciTrb *)rin_v, rin_pa, XHCI_RING_TRBS);
    xhci_cmd_ring_init(&ds->ep_ring[dci_out], (XhciTrb *)rout_v, rout_pa, XHCI_RING_TRBS);
    uint32_t *ecin = ctx_dw(ds->in_ctx, dci_in + 1u);
    ecin[1] = (3u << 1) | (6u << 3) | ((uint32_t)mps_in << 16);
    ecin[2] = (uint32_t)(rin_pa | 1ULL); ecin[3] = (uint32_t)(rin_pa >> 32); ecin[4] = 512u;
    uint32_t *ecout = ctx_dw(ds->in_ctx, dci_out + 1u);
    ecout[1] = (3u << 1) | (2u << 3) | ((uint32_t)mps_out << 16);
    ecout[2] = (uint32_t)(rout_pa | 1ULL); ecout[3] = (uint32_t)(rout_pa >> 32); ecout[4] = 512u;
    XhciTrb ev;
    return xhci_send_cmd(ds->in_ctx_pa, 0u, XHCI_TRB_CONFIG_EP, (slot << 24), &ev);
}
int xhci_bulk_xfer(uint32_t slot, uint8_t ep_addr, uint64_t buf_phys, uint32_t len) {
    if (slot == 0u || slot > XHCI_MAX_SLOTS_CAP) return XHCI_E_NODEV;
    uint32_t dci = ep_dci(ep_addr);
    XhciCmdRing *r = &g_xdev[slot].ep_ring[dci];
    xhci_cmd_ring_push(r, buf_phys, len, XHCI_TRB_NORMAL, XHCI_TRB_IOC);
    __asm__ volatile("mfence" ::: "memory");
    ring_doorbell(slot, dci);
    return xhci_wait_xfer(slot, dci);
}
