#include "hda.h"
#include "pci.h"
#include "kmemory.h"

static struct {
    HdaInfo info;
    volatile uint8_t *mmio;
} g_hda;

static inline uint16_t hda_r16(uint32_t r) { return *(volatile uint16_t *)(g_hda.mmio + r); }
static inline uint32_t hda_r32(uint32_t r) { return *(volatile uint32_t *)(g_hda.mmio + r); }
static inline void hda_w32(uint32_t r, uint32_t v) { *(volatile uint32_t *)(g_hda.mmio + r) = v; }

int hda_init(HdaInfo *out) {
    const PciDev *d = pci_find(0x04u, 0x03u, PCI_ANY_PROG_IF, 0);
    if (!d || d->bar[0] == 0ULL) return -1;
    (void)pci_enable(d);
    g_hda.mmio = (volatile uint8_t *)(UINTN)d->bar[0];
    g_hda.info.vendor = d->vendor;
    g_hda.info.device = d->device;
    g_hda.info.mmio_phys = d->bar[0];

    hda_w32(HDA_REG_GCTL, 0u);
    for (volatile uint32_t i = 0; i < 20000u; i++) { if (!(hda_r32(HDA_REG_GCTL) & HDA_GCTL_CRST)) break; }
    hda_w32(HDA_REG_GCTL, HDA_GCTL_CRST);
    for (volatile uint32_t i = 0; i < 50000u; i++) { if (hda_r32(HDA_REG_GCTL) & HDA_GCTL_CRST) break; }
    for (volatile uint32_t i = 0; i < 20000u; i++) __asm__ volatile("pause");

    uint16_t gcap = hda_r16(HDA_REG_GCAP);
    g_hda.info.oss_count = (uint8_t)((gcap >> 12) & 0x0Fu);
    g_hda.info.iss_count = (uint8_t)((gcap >> 8) & 0x0Fu);
    g_hda.info.codec_mask = hda_r16(HDA_REG_STATESTS);

    UINT64 ring_pa = phys_alloc_frames(1);
    if (!ring_pa) return -2;
    uint8_t *rv = (uint8_t *)(UINTN)ring_pa;
    for (uint32_t i = 0; i < 4096u; i++) rv[i] = 0u;
    hda_w32(HDA_REG_CORBLBASE, (uint32_t)(ring_pa & 0xFFFFFFFFULL));
    hda_w32(HDA_REG_CORBUBASE, (uint32_t)(ring_pa >> 32));
    uint64_t rirb_pa = (uint64_t)ring_pa + 1024ULL;
    hda_w32(HDA_REG_RIRBLBASE, (uint32_t)(rirb_pa & 0xFFFFFFFFULL));
    hda_w32(HDA_REG_RIRBUBASE, (uint32_t)(rirb_pa >> 32));

    g_hda.info.dma_ring_phys = (uint64_t)ring_pa;
    g_hda.info.active = 1;
    if (out) *out = g_hda.info;
    return 0;
}

int hda_get_info(HdaInfo *out) { if (!g_hda.info.active) return -1; if (out) *out = g_hda.info; return 0; }
