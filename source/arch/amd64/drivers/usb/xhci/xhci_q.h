#ifndef XASMOS_XHCI_Q_H
#define XASMOS_XHCI_Q_H

#include <stdint.h>

/* Capability register offsets */
#define XHCI_CAP_CAPLENGTH   0x00u
#define XHCI_CAP_HCSPARAMS1  0x04u
#define XHCI_CAP_HCSPARAMS2  0x08u
#define XHCI_CAP_HCSPARAMS3  0x0Cu
#define XHCI_CAP_HCCPARAMS1  0x10u
#define XHCI_CAP_DBOFF       0x14u
#define XHCI_CAP_RTSOFF      0x18u

/* Operational register offsets (relative to op_base = bar0 + caplength) */
#define XHCI_OP_USBCMD       0x00u
#define XHCI_OP_USBSTS       0x04u
#define XHCI_OP_PAGESIZE     0x08u
#define XHCI_OP_DNCTRL       0x14u
#define XHCI_OP_CRCR         0x18u
#define XHCI_OP_DCBAAP       0x30u
#define XHCI_OP_CONFIG       0x38u
#define XHCI_OP_PORTSC_BASE  0x400u
#define XHCI_OP_PORT_STRIDE  0x10u

/* USBCMD / USBSTS bits */
#define XHCI_CMD_RS          (1u << 0)
#define XHCI_CMD_HCRST       (1u << 1)
#define XHCI_CMD_INTE        (1u << 2)
#define XHCI_CMD_HSEE        (1u << 3)
#define XHCI_STS_HCH         (1u << 0)
#define XHCI_STS_HSE         (1u << 2)
#define XHCI_STS_EINT        (1u << 3)
#define XHCI_STS_PCD         (1u << 4)
#define XHCI_STS_CNR         (1u << 11)

/* PORTSC bits */
#define XHCI_PORTSC_CCS      (1u << 0)
#define XHCI_PORTSC_PED      (1u << 1)
#define XHCI_PORTSC_PR       (1u << 4)
#define XHCI_PORTSC_PP       (1u << 9)
#define XHCI_PORTSC_CSC      (1u << 17)
#define XHCI_PORTSC_PEC      (1u << 18)
#define XHCI_PORTSC_PRC      (1u << 21)
#define XHCI_PORTSC_RW1C_MASK ((1u<<17)|(1u<<18)|(1u<<19)|(1u<<20)|(1u<<21)|(1u<<22)|(1u<<23))

/* Interrupter 0 register offsets (relative to rt_base + 0x20) */
#define XHCI_IR_IMAN         0x00u
#define XHCI_IR_IMOD         0x04u
#define XHCI_IR_ERSTSZ       0x08u
#define XHCI_IR_ERSTBA       0x10u
#define XHCI_IR_ERDP         0x18u
#define XHCI_IMAN_IP         (1u << 0)
#define XHCI_IMAN_IE         (1u << 1)
#define XHCI_ERDP_EHB        (1ULL << 3)

/* Extended capability IDs */
#define XHCI_XECP_LEGACY     1u
#define XHCI_USBLEGSUP_BIOS  (1u << 16)
#define XHCI_USBLEGSUP_OS    (1u << 24)

/* TRB types (xHCI 1.2 Table 6-91) */
#define XHCI_TRB_NORMAL          1u
#define XHCI_TRB_SETUP_STAGE     2u
#define XHCI_TRB_DATA_STAGE      3u
#define XHCI_TRB_STATUS_STAGE    4u
#define XHCI_TRB_LINK            6u
#define XHCI_TRB_ENABLE_SLOT     9u
#define XHCI_TRB_DISABLE_SLOT    10u
#define XHCI_TRB_ADDRESS_DEV     11u
#define XHCI_TRB_CONFIG_EP       12u
#define XHCI_TRB_CMD_NOOP        23u
#define XHCI_TRB_TRANSFER_EVT    32u
#define XHCI_TRB_CMD_COMPLETION  33u
#define XHCI_TRB_PORT_STATUS_CHG 34u

/* TRB control flags & completion codes */
#define XHCI_TRB_CYCLE       (1u << 0)
#define XHCI_TRB_TC          (1u << 1)
#define XHCI_TRB_IOC         (1u << 5)
#define XHCI_CC_SUCCESS      1u

typedef struct {
    uint64_t param;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) XhciTrb;

typedef struct {
    uint64_t seg_addr;
    uint32_t seg_size;
    uint32_t rsvd;
} __attribute__((packed)) XhciErstEntry;

typedef struct {
    XhciTrb *trbs;
    uint64_t phys;
    uint32_t cap;
    uint32_t enq;
    uint32_t pcs;
} XhciCmdRing;

typedef struct {
    XhciTrb *trbs;
    uint64_t phys;
    uint32_t cap;
    uint32_t deq;
    uint32_t ccs;
} XhciEvtRing;

uint32_t xhci_cap_length(uint32_t cap0);
uint16_t xhci_hci_version(uint32_t cap0);
uint32_t xhci_max_slots(uint32_t hcsparams1);
uint32_t xhci_max_intrs(uint32_t hcsparams1);
uint32_t xhci_max_ports(uint32_t hcsparams1);
uint32_t xhci_scratchpad_count(uint32_t hcsparams2);
uint32_t xhci_context_size(uint32_t hccparams1);
uint32_t xhci_xecp_offset(uint32_t hccparams1);

int      xhci_port_connected(uint32_t portsc);
int      xhci_port_enabled(uint32_t portsc);
int      xhci_port_reset_active(uint32_t portsc);
uint32_t xhci_port_speed(uint32_t portsc);
const char *xhci_port_speed_name(uint32_t speed);
uint32_t xhci_portsc_write_val(uint32_t cur_portsc, uint32_t set_bits, uint32_t clear_rw1c);

void     xhci_trb_pack(XhciTrb *t, uint64_t param, uint32_t status, uint32_t type, uint32_t flags, uint32_t cycle);
uint32_t xhci_trb_type(const XhciTrb *t);
uint32_t xhci_trb_cycle(const XhciTrb *t);
uint32_t xhci_evt_cc(const XhciTrb *evt);
uint32_t xhci_evt_slot(const XhciTrb *evt);
uint32_t xhci_evt_port(const XhciTrb *evt);

int      xhci_cmd_ring_init(XhciCmdRing *r, XhciTrb *buf, uint64_t phys, uint32_t cap);
uint32_t xhci_cmd_ring_push(XhciCmdRing *r, uint64_t param, uint32_t status, uint32_t type, uint32_t flags);
int      xhci_evt_ring_init(XhciEvtRing *r, XhciTrb *buf, uint64_t phys, uint32_t cap);
int      xhci_evt_ring_poll(XhciEvtRing *r, XhciTrb *out, uint64_t *new_erdp);

#endif
