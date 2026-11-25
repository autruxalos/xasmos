#ifndef XASMOS_XHCI_H
#define XASMOS_XHCI_H

#include <stdint.h>
#include "pci.h"
#include "xhci_q.h"

#define XHCI_OK          0
#define XHCI_E_NODEV    -1
#define XHCI_E_MMIO     -2
#define XHCI_E_TIMEOUT  -3
#define XHCI_E_NOMEM    -4
#define XHCI_E_CMD      -5

#define XHCI_MAX_SLOTS_CAP 32u
#define XHCI_RING_TRBS     64u

typedef struct {
    int      active;
    uint16_t hci_version;
    uint8_t  max_slots;
    uint8_t  max_ports;
    uint8_t  ctx_size;
    uint32_t scratchpads;
    int      irq_vec;
    uint32_t connected_ports;
    uint32_t noop_cc;
} XhciInfo;

int xhci_probe_and_init(XhciInfo *out);
int xhci_init(const PciDev *d, XhciInfo *out);
int xhci_get_info(XhciInfo *out);
int xhci_irq_vector(void);
int xhci_send_cmd(uint64_t param, uint32_t status, uint32_t type, uint32_t flags, XhciTrb *evt_out);

#endif
