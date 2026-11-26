#include "xhci_q.h"

uint32_t xhci_cap_length(uint32_t cap0) { return cap0 & 0xFFu; }
uint16_t xhci_hci_version(uint32_t cap0) { return (uint16_t)((cap0 >> 16) & 0xFFFFu); }
uint32_t xhci_max_slots(uint32_t hcsparams1) { return hcsparams1 & 0xFFu; }
uint32_t xhci_max_intrs(uint32_t hcsparams1) { return (hcsparams1 >> 8) & 0x7FFu; }
uint32_t xhci_max_ports(uint32_t hcsparams1) { return (hcsparams1 >> 24) & 0xFFu; }
uint32_t xhci_scratchpad_count(uint32_t hcsparams2) {
    uint32_t hi = (hcsparams2 >> 21) & 0x1Fu;
    uint32_t lo = (hcsparams2 >> 27) & 0x1Fu;
    return (hi << 5) | lo;
}
uint32_t xhci_context_size(uint32_t hccparams1) { return (hccparams1 & (1u << 2)) ? 64u : 32u; }
uint32_t xhci_xecp_offset(uint32_t hccparams1) { return ((hccparams1 >> 16) & 0xFFFFu) << 2; }

int xhci_port_connected(uint32_t portsc) { return (portsc & XHCI_PORTSC_CCS) != 0u; }
int xhci_port_enabled(uint32_t portsc) { return (portsc & XHCI_PORTSC_PED) != 0u; }
int xhci_port_reset_active(uint32_t portsc) { return (portsc & XHCI_PORTSC_PR) != 0u; }
uint32_t xhci_port_speed(uint32_t portsc) { return (portsc >> 10) & 0xFu; }
const char *xhci_port_speed_name(uint32_t speed) {
    switch (speed) {
    case 1u: return "Full-Speed (12 Mb/s)";
    case 2u: return "Low-Speed (1.5 Mb/s)";
    case 3u: return "High-Speed (480 Mb/s)";
    case 4u: return "SuperSpeed (5 Gb/s)";
    case 5u: return "SuperSpeed+ (10 Gb/s)";
    default: return "Unknown";
    }
}
uint32_t xhci_portsc_write_val(uint32_t cur_portsc, uint32_t set_bits, uint32_t clear_rw1c) {
    uint32_t base = cur_portsc & ~XHCI_PORTSC_RW1C_MASK;
    return base | set_bits | (clear_rw1c & XHCI_PORTSC_RW1C_MASK);
}

void xhci_trb_pack(XhciTrb *t, uint64_t param, uint32_t status, uint32_t type, uint32_t flags, uint32_t cycle) {
    t->param = param;
    t->status = status;
    t->control = ((type & 0x3Fu) << 10) | (flags & ~0xFC01u) | (cycle & 1u);
}
uint32_t xhci_trb_type(const XhciTrb *t) { return (t->control >> 10) & 0x3Fu; }
uint32_t xhci_trb_cycle(const XhciTrb *t) { return t->control & 1u; }
uint32_t xhci_evt_cc(const XhciTrb *evt) { return (evt->status >> 24) & 0xFFu; }
uint32_t xhci_evt_slot(const XhciTrb *evt) { return (evt->control >> 24) & 0xFFu; }
uint32_t xhci_evt_port(const XhciTrb *evt) { return (uint32_t)((evt->param >> 24) & 0xFFu); }

int xhci_cmd_ring_init(XhciCmdRing *r, XhciTrb *buf, uint64_t phys, uint32_t cap) {
    if (!r || !buf || cap < 2u) return -1;
    r->trbs = buf;
    r->phys = phys;
    r->cap = cap;
    r->enq = 0u;
    r->pcs = 1u;
    for (uint32_t i = 0; i < cap; i++) {
        buf[i].param = 0; buf[i].status = 0; buf[i].control = 0;
    }
    xhci_trb_pack(&buf[cap - 1u], phys, 0u, XHCI_TRB_LINK, XHCI_TRB_TC, 0u);
    return 0;
}

uint32_t xhci_cmd_ring_push(XhciCmdRing *r, uint64_t param, uint32_t status, uint32_t type, uint32_t flags) {
    uint32_t idx = r->enq;
    xhci_trb_pack(&r->trbs[idx], param, status, type, flags, r->pcs);
    r->enq++;
    if (r->enq >= r->cap - 1u) {
        xhci_trb_pack(&r->trbs[r->cap - 1u], r->phys, 0u, XHCI_TRB_LINK, XHCI_TRB_TC, r->pcs);
        r->enq = 0u;
        r->pcs ^= 1u;
    }
    return idx;
}

int xhci_evt_ring_init(XhciEvtRing *r, XhciTrb *buf, uint64_t phys, uint32_t cap) {
    if (!r || !buf || cap < 2u) return -1;
    r->trbs = buf;
    r->phys = phys;
    r->cap = cap;
    r->deq = 0u;
    r->ccs = 1u;
    for (uint32_t i = 0; i < cap; i++) {
        buf[i].param = 0; buf[i].status = 0; buf[i].control = 0;
    }
    return 0;
}

int xhci_evt_ring_poll(XhciEvtRing *r, XhciTrb *out, uint64_t *new_erdp) {
    XhciTrb *cur = &r->trbs[r->deq];
    if (xhci_trb_cycle(cur) != r->ccs) return 0;
    if (out) *out = *cur;
    r->deq++;
    if (r->deq >= r->cap) {
        r->deq = 0u;
        r->ccs ^= 1u;
    }
    if (new_erdp) {
        *new_erdp = (r->phys + (uint64_t)r->deq * sizeof(XhciTrb)) | XHCI_ERDP_EHB;
    }
    return 1;
}
