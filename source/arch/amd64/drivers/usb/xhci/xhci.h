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
int xhci_find_connected_port(uint32_t *port_out, uint32_t *speed_out);
int xhci_port_reset(uint32_t port, uint32_t *speed_out);
int xhci_dev_open(uint32_t port, uint32_t speed, uint32_t *slot_out);
int xhci_ctrl_xfer(uint32_t slot, uint8_t req_type, uint8_t req, uint16_t val, uint16_t idx, uint16_t len, uint64_t data_phys);
int xhci_configure_bulk(uint32_t slot, uint8_t ep_in, uint16_t mps_in, uint8_t ep_out, uint16_t mps_out);
int xhci_bulk_xfer(uint32_t slot, uint8_t ep_addr, uint64_t buf_phys, uint32_t len);
#endif
