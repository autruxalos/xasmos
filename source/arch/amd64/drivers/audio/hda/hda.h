#ifndef XASMOS_HDA_H
#define XASMOS_HDA_H
#include <stdint.h>
#include "hda_q.h"

typedef struct {
    int      active;
    uint16_t vendor;
    uint16_t device;
    uint16_t codec_mask;
    uint8_t  oss_count;
    uint8_t  iss_count;
    uint64_t mmio_phys;
    uint64_t dma_ring_phys;
} HdaInfo;

int hda_init(HdaInfo *out);
int hda_get_info(HdaInfo *out);
#endif
