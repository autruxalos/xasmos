#include <stdio.h>
#include "xhci_q.h"
static int checks = 0, failures = 0;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
int main(void) {
    uint32_t cap0 = (0x0110u << 16) | 0x20u;
    CHECK(xhci_cap_length(cap0) == 0x20u && xhci_hci_version(cap0) == 0x0110u);
    uint32_t hcs1 = (4u << 24) | (8u << 8) | 32u;
    CHECK(xhci_max_slots(hcs1) == 32u && xhci_max_intrs(hcs1) == 8u && xhci_max_ports(hcs1) == 4u);
    uint32_t hcs2 = (2u << 27) | (1u << 21);
    CHECK(xhci_scratchpad_count(hcs2) == ((1u << 5) | 2u));
    CHECK(xhci_context_size(0u) == 32u && xhci_context_size(1u << 2) == 64u);
    CHECK(xhci_xecp_offset(0x0040u << 16) == 0x100u);
    uint32_t psc = XHCI_PORTSC_CCS | XHCI_PORTSC_PED | (4u << 10) | XHCI_PORTSC_CSC;
    CHECK(xhci_port_connected(psc) == 1 && xhci_port_enabled(psc) == 1 && xhci_port_speed(psc) == 4u);
    uint32_t w = xhci_portsc_write_val(psc, XHCI_PORTSC_PR, XHCI_PORTSC_CSC);
    CHECK((w & XHCI_PORTSC_PR) != 0u && (w & XHCI_PORTSC_CSC) != 0u && (w & XHCI_PORTSC_PRC) == 0u);
    XhciTrb cmd_buf[4]; XhciCmdRing cr;
    CHECK(xhci_cmd_ring_init(&cr, cmd_buf, 0x200000ULL, 4u) == 0);
    CHECK(xhci_cmd_ring_push(&cr, 0x11ULL, 0u, XHCI_TRB_CMD_NOOP, 0u) == 0u && xhci_trb_cycle(&cmd_buf[0]) == 1u);
    CHECK(xhci_cmd_ring_push(&cr, 0x22ULL, 0u, XHCI_TRB_ENABLE_SLOT, 0u) == 1u);
    CHECK(xhci_cmd_ring_push(&cr, 0x33ULL, 0u, XHCI_TRB_CMD_NOOP, 0u) == 2u);
    CHECK(cr.enq == 0u && cr.pcs == 0u && xhci_trb_type(&cmd_buf[3]) == XHCI_TRB_LINK && xhci_trb_cycle(&cmd_buf[3]) == 1u);
    CHECK(xhci_cmd_ring_push(&cr, 0x44ULL, 0u, XHCI_TRB_CMD_NOOP, 0u) == 0u && xhci_trb_cycle(&cmd_buf[0]) == 0u);
    XhciTrb evt_buf[2]; XhciEvtRing er;
    CHECK(xhci_evt_ring_init(&er, evt_buf, 0x300000ULL, 2u) == 0);
    XhciTrb out; uint64_t erdp = 0;
    CHECK(xhci_evt_ring_poll(&er, &out, &erdp) == 0);
    xhci_trb_pack(&evt_buf[0], 0x200000ULL, (XHCI_CC_SUCCESS << 24), XHCI_TRB_CMD_COMPLETION, (5u << 24), 1u);
    CHECK(xhci_evt_ring_poll(&er, &out, &erdp) == 1 && xhci_evt_cc(&out) == XHCI_CC_SUCCESS && xhci_evt_slot(&out) == 5u);
    CHECK(erdp == ((0x300000ULL + 16ULL) | XHCI_ERDP_EHB));
    printf("xhci_q: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
