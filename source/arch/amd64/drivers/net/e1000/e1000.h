#ifndef XASMOS_E1000_H
#define XASMOS_E1000_H
#include <stdint.h>
#include "e1000_q.h"

typedef struct {
    int      active;
    uint16_t vendor;
    uint16_t device;
    uint64_t mmio_phys;
    uint64_t rx_ring_phys;
    uint64_t tx_ring_phys;
    uint64_t buf_pool_phys;
    uint8_t  mac[6];
} E1000Info;

int e1000_init(E1000Info *out);
int e1000_get_info(E1000Info *out);
int e1000_tx_raw(const void *frame, uint16_t len);
int e1000_rx_raw(void *buf, uint16_t max_len, uint16_t *len_out);
#endif
