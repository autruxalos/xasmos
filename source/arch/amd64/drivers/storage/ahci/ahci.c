#include "ahci.h"
#include "kmemory.h"
#include "paging.h"

#ifndef SPIN_LIMIT
#define SPIN_LIMIT 50000000u
#endif

typedef struct {
    volatile uint8_t *abar;
    uint32_t          port;
    AhciCmdHeader    *clb;
    uint64_t          clb_pa;
    uint8_t          *fb;
    uint64_t          fb_pa;
    uint8_t          *ctba;
    uint64_t          ctba_pa;
    uint8_t          *dma;
    uint64_t          dma_pa;
    BlkDev            dev;
    AhciInfo          info;
} AhciState;

static AhciState g_ahci;

static uint32_t hba_rd(uint32_t off) { return *(volatile uint32_t *)(g_ahci.abar + off); }
static void hba_wr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(g_ahci.abar + off) = v; }
static uint32_t prt_rd(uint32_t p, uint32_t off) { return hba_rd(0x100u + p * 0x80u + off); }
static void prt_wr(uint32_t p, uint32_t off, uint32_t v) { hba_wr(0x100u + p * 0x80u + off, v); }

static int alloc_zero_page(uint64_t *pa_out, void **v_out) {
    UINT64 pa = phys_alloc_frames(1); if (!pa) return -1;
    uint8_t *p = (uint8_t *)(UINTN)pa;
    for (uint32_t i = 0; i < 4096u; i++) p[i] = 0u;
    *pa_out = (uint64_t)pa; *v_out = p; return 0;
}

static void port_stop(uint32_t p) {
    uint32_t cmd = prt_rd(p, 0x18u);
    cmd &= ~(AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE);
    prt_wr(p, 0x18u, cmd);
    for (uint32_t i = 0; i < SPIN_LIMIT; i++) {
        uint32_t c = prt_rd(p, 0x18u);
        if ((c & (AHCI_PORT_CMD_FR | AHCI_PORT_CMD_CR)) == 0u) break;
        __asm__ volatile("pause");
    }
}

static void port_start(uint32_t p) {
    for (uint32_t i = 0; i < SPIN_LIMIT && (prt_rd(p, 0x18u) & AHCI_PORT_CMD_CR); i++) __asm__ volatile("pause");
    uint32_t cmd = prt_rd(p, 0x18u);
    prt_wr(p, 0x18u, cmd | AHCI_PORT_CMD_FRE);
    prt_wr(p, 0x18u, prt_rd(p, 0x18u) | AHCI_PORT_CMD_ST);
}

static int ahci_issue_cmd(uint8_t ata_cmd, uint64_t lba, uint16_t count, uint32_t bytes, int write) {
    uint32_t p = g_ahci.port;
    for (uint32_t i = 0; i < SPIN_LIMIT; i++) {
        if ((prt_rd(p, 0x20u) & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) == 0u) break;
        if (i + 1u == SPIN_LIMIT) return -1;
        __asm__ volatile("pause");
    }
    prt_wr(p, 0x10u, 0xFFFFFFFFu);
    uint16_t prdtl = bytes > 0u ? 1u : 0u;
    ahci_build_cmd_hdr(&g_ahci.clb[0], prdtl, write, g_ahci.ctba_pa);
    for (uint32_t i = 0; i < 256u; i++) g_ahci.ctba[i] = 0u;
    ahci_build_fis_h2d((AhciFisH2D *)g_ahci.ctba, ata_cmd, lba, count);
    if (prdtl) ahci_build_prdt((AhciPrdtEntry *)(g_ahci.ctba + 0x80u), g_ahci.dma_pa, bytes, 1);
    __asm__ volatile("mfence" ::: "memory");
    prt_wr(p, 0x38u, 1u);
    for (uint32_t i = 0; i < SPIN_LIMIT; i++) {
        uint32_t ci = prt_rd(p, 0x38u), is = prt_rd(p, 0x10u);
        if ((ci & 1u) == 0u) return (prt_rd(p, 0x20u) & AHCI_PORT_TFD_ERR) ? -2 : 0;
        if (is & (1u << 30)) return -3;
        __asm__ volatile("pause");
    }
    return -4;
}

static int ahci_blk_read(void *ctx, uint64_t lba, uint32_t count, void *buf) {
    (void)ctx; uint8_t *dst = (uint8_t *)buf;
    while (count > 0u) {
        uint32_t n = count > 8u ? 8u : count;
        if (ahci_issue_cmd(ATA_CMD_READ_DMA_EXT, lba, (uint16_t)n, n * 512u, 0) != 0) return BLK_E_IO;
        for (uint32_t i = 0; i < n * 512u; i++) dst[i] = g_ahci.dma[i];
        lba += n; count -= n; dst += n * 512u;
    }
    return BLK_OK;
}

static int ahci_blk_write(void *ctx, uint64_t lba, uint32_t count, const void *buf) {
    (void)ctx; const uint8_t *src = (const uint8_t *)buf;
    while (count > 0u) {
        uint32_t n = count > 8u ? 8u : count;
        for (uint32_t i = 0; i < n * 512u; i++) g_ahci.dma[i] = src[i];
        if (ahci_issue_cmd(ATA_CMD_WRITE_DMA_EXT, lba, (uint16_t)n, n * 512u, 1) != 0) return BLK_E_IO;
        lba += n; count -= n; src += n * 512u;
    }
    return BLK_OK;
}

static int ahci_blk_flush(void *ctx) {
    (void)ctx; if (!g_ahci.info.active) return BLK_E_IO;
    return ahci_issue_cmd(ATA_CMD_FLUSH_EXT, 0ULL, 0u, 0u, 0) == 0 ? BLK_OK : BLK_E_IO;
}

static const BlkOps g_ahci_ops = { ahci_blk_read, ahci_blk_write, ahci_blk_flush, 0 };

int ahci_init(const PciDev *d, BlkDev *out, AhciInfo *info_out) {
    if (!d || d->bar[5] == 0) return -1;
    pci_enable(d);
    UINT64 mmio = 0;
    if (xpt_map_uncached(d->bar[5], 0x2000u, &mmio) != 0) return -2;
    g_ahci.abar = (volatile uint8_t *)(UINTN)mmio;
    hba_wr(0x04u, hba_rd(0x04u) | AHCI_GHC_AE);
    uint32_t cap = hba_rd(0x00u), cap2 = hba_rd(0x24u), pi = hba_rd(0x0Cu);
    if (cap2 & 1u) {
        hba_wr(0x28u, hba_rd(0x28u) | AHCI_BOHC_OOS);
        for (uint32_t i = 0; i < SPIN_LIMIT && (hba_rd(0x28u) & AHCI_BOHC_BOS); i++) __asm__ volatile("pause");
    }
    uint32_t nports = ahci_num_ports(cap), found = 0xFFFFFFFFu;
    for (uint32_t p = 0; p < nports && p < 32u; p++) {
        if ((pi & (1u << p)) == 0u) continue;
        if (ahci_port_is_ata(prt_rd(p, 0x28u), prt_rd(p, 0x24u))) { found = p; break; }
    }
    if (found == 0xFFFFFFFFu) return -3;
    g_ahci.port = found;
    port_stop(found);
    void *clb_v = 0, *fb_v = 0, *ct_v = 0, *dm_v = 0;
    if (alloc_zero_page(&g_ahci.clb_pa, &clb_v) != 0) return -4;
    if (alloc_zero_page(&g_ahci.fb_pa, &fb_v) != 0) return -4;
    if (alloc_zero_page(&g_ahci.ctba_pa, &ct_v) != 0) return -4;
    if (alloc_zero_page(&g_ahci.dma_pa, &dm_v) != 0) return -4;
    g_ahci.clb = (AhciCmdHeader *)clb_v; g_ahci.fb = (uint8_t *)fb_v;
    g_ahci.ctba = (uint8_t *)ct_v; g_ahci.dma = (uint8_t *)dm_v;
    prt_wr(found, 0x00u, (uint32_t)g_ahci.clb_pa); prt_wr(found, 0x04u, (uint32_t)(g_ahci.clb_pa >> 32));
    prt_wr(found, 0x08u, (uint32_t)g_ahci.fb_pa);  prt_wr(found, 0x0Cu, (uint32_t)(g_ahci.fb_pa >> 32));
    prt_wr(found, 0x30u, 0xFFFFFFFFu); prt_wr(found, 0x10u, 0xFFFFFFFFu);
    port_start(found);
    if (ahci_issue_cmd(ATA_CMD_IDENTIFY, 0ULL, 1u, 512u, 0) != 0) return -5;
    const uint16_t *idw = (const uint16_t *)g_ahci.dma;
    g_ahci.info.active = 1; g_ahci.info.port = (uint8_t)found;
    g_ahci.info.slots = ahci_num_cmd_slots(cap);
    g_ahci.info.sectors = ahci_parse_identify_sectors(idw);
    ahci_parse_identify_string(&idw[27], 20u, g_ahci.info.model, sizeof(g_ahci.info.model));
    g_ahci.dev.ctx = &g_ahci; g_ahci.dev.ops = &g_ahci_ops;
    g_ahci.dev.sector_size = 512u; g_ahci.dev.sector_count = g_ahci.info.sectors;
    if (out) *out = g_ahci.dev;
    if (info_out) *info_out = g_ahci.info;
    return 0;
}

int ahci_probe_and_init(AhciInfo *out) {
    pci_scan();
    const PciDev *d = pci_find(0x01u, 0x06u, 0x01u, 0u);
    if (!d) return -1;
    return ahci_init(d, 0, out);
}

BlkDev *storage_ahci_device(void) { return g_ahci.info.active ? &g_ahci.dev : 0; }
