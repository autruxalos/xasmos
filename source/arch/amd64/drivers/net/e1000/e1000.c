#include "e1000.h"
#include "pci.h"
#include "kmemory.h"

static struct {
    E1000Info     info;
    volatile uint8_t *mmio;
    E1000RxDesc  *rx_ring;
    E1000TxDesc  *tx_ring;
    uint8_t      *rx_bufs;
    uint8_t      *tx_bufs;
    uint64_t      rx_bufs_pa;
    uint64_t      tx_bufs_pa;
    uint32_t      rx_idx;
    uint32_t      tx_idx;
} g_nic;

static inline uint32_t nic_r32(uint32_t reg) { return *(volatile uint32_t *)(g_nic.mmio + reg); }
static inline void nic_w32(uint32_t reg, uint32_t val) { *(volatile uint32_t *)(g_nic.mmio + reg) = val; }

static uint16_t e1000_read_eeprom(uint8_t addr) {
    nic_w32(E1000_REG_EERD, 1u | ((uint32_t)addr << 8));
    for (uint32_t i = 0; i < 10000u; i++) {
        uint32_t v = nic_r32(E1000_REG_EERD);
        if (v & (1u << 4)) return (uint16_t)(v >> 16);
    }
    return 0xFFFFu;
}

int e1000_init(E1000Info *out) {
    const PciDev *d = 0;
    for (int skip = 0; ; skip++) {
        const PciDev *cand = pci_find(0x02u, 0x00u, 0xFFu, skip);
        if (!cand) break;
        if (e1000_is_supported_device(cand->vendor, cand->device) && cand->bar[0] != 0ULL) { d = cand; break; }
    }
    if (!d) return -1;
    (void)pci_enable(d);
    g_nic.mmio = (volatile uint8_t *)(UINTN)d->bar[0];
    g_nic.info.vendor = d->vendor;
    g_nic.info.device = d->device;
    g_nic.info.mmio_phys = d->bar[0];

    nic_w32(E1000_REG_IMC, 0xFFFFFFFFu);
    nic_w32(E1000_REG_CTRL, nic_r32(E1000_REG_CTRL) | E1000_CTRL_RST);
    for (volatile uint32_t i = 0; i < 50000u; i++) { if (!(nic_r32(E1000_REG_CTRL) & E1000_CTRL_RST)) break; }
    nic_w32(E1000_REG_IMC, 0xFFFFFFFFu);

    uint16_t w0 = e1000_read_eeprom(0u);
    if (w0 != 0xFFFFu && w0 != 0x0000u) {
        uint16_t w1 = e1000_read_eeprom(1u);
        uint16_t w2 = e1000_read_eeprom(2u);
        g_nic.info.mac[0] = (uint8_t)(w0 & 0xFFu); g_nic.info.mac[1] = (uint8_t)(w0 >> 8);
        g_nic.info.mac[2] = (uint8_t)(w1 & 0xFFu); g_nic.info.mac[3] = (uint8_t)(w1 >> 8);
        g_nic.info.mac[4] = (uint8_t)(w2 & 0xFFu); g_nic.info.mac[5] = (uint8_t)(w2 >> 8);
    } else {
        e1000_decode_mac_ral_rah(nic_r32(E1000_REG_RAL0), nic_r32(E1000_REG_RAH0), g_nic.info.mac);
    }

    for (uint32_t i = 0; i < 128u; i++) nic_w32(E1000_REG_MTA + i * 4u, 0u);

    UINT64 ring_pa = phys_alloc_frames(1);
    UINT64 rx_pa   = phys_alloc_frames(16);
    UINT64 tx_pa   = phys_alloc_frames(16);
    if (!ring_pa || !rx_pa || !tx_pa) return -2;

    uint8_t *ring_v = (uint8_t *)(UINTN)ring_pa;
    for (uint32_t i = 0; i < 4096u; i++) ring_v[i] = 0u;
    g_nic.rx_ring = (E1000RxDesc *)ring_v;
    g_nic.tx_ring = (E1000TxDesc *)(ring_v + 2048u);
    g_nic.rx_bufs = (uint8_t *)(UINTN)rx_pa;
    g_nic.tx_bufs = (uint8_t *)(UINTN)tx_pa;
    g_nic.rx_bufs_pa = (uint64_t)rx_pa;
    g_nic.tx_bufs_pa = (uint64_t)tx_pa;

    for (uint32_t i = 0; i < E1000_RING_DESCS; i++) {
        g_nic.rx_ring[i].addr = g_nic.rx_bufs_pa + (uint64_t)i * E1000_PKT_BUF_SIZE;
        g_nic.rx_ring[i].status = 0u;
        g_nic.tx_ring[i].addr = g_nic.tx_bufs_pa + (uint64_t)i * E1000_PKT_BUF_SIZE;
        g_nic.tx_ring[i].status = E1000_TXD_STAT_DD;
    }

    nic_w32(E1000_REG_RDBAL, (uint32_t)(ring_pa & 0xFFFFFFFFULL));
    nic_w32(E1000_REG_RDBAH, (uint32_t)(ring_pa >> 32));
    nic_w32(E1000_REG_RDLEN, E1000_RING_DESCS * (uint32_t)sizeof(E1000RxDesc));
    nic_w32(E1000_REG_RDH, 0u);
    nic_w32(E1000_REG_RDT, E1000_RING_DESCS - 1u);

    uint64_t tx_ring_pa = (uint64_t)ring_pa + 2048ULL;
    nic_w32(E1000_REG_TDBAL, (uint32_t)(tx_ring_pa & 0xFFFFFFFFULL));
    nic_w32(E1000_REG_TDBAH, (uint32_t)(tx_ring_pa >> 32));
    nic_w32(E1000_REG_TDLEN, E1000_RING_DESCS * (uint32_t)sizeof(E1000TxDesc));
    nic_w32(E1000_REG_TDH, 0u);
    nic_w32(E1000_REG_TDT, 0u);

    nic_w32(E1000_REG_CTRL, nic_r32(E1000_REG_CTRL) | E1000_CTRL_SLU);
    nic_w32(E1000_REG_TIPG, 10u | (8u << 10) | (6u << 20));
    nic_w32(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | (15u << 4) | (64u << 12));
    nic_w32(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_BAM | E1000_RCTL_BSIZE_2K | E1000_RCTL_SECRC);

    g_nic.info.rx_ring_phys = (uint64_t)ring_pa;
    g_nic.info.tx_ring_phys = tx_ring_pa;
    g_nic.info.buf_pool_phys = (uint64_t)rx_pa;
    g_nic.info.active = 1;
    if (out) *out = g_nic.info;
    return 0;
}

int e1000_get_info(E1000Info *out) { if (!g_nic.info.active) return -1; if (out) *out = g_nic.info; return 0; }

int e1000_tx_raw(const void *frame, uint16_t len) {
    if (!g_nic.info.active || !frame || len == 0u || len > E1000_PKT_BUF_SIZE) return -1;
    uint32_t idx = g_nic.tx_idx;
    E1000TxDesc *td = &g_nic.tx_ring[idx];
    if (!(td->status & E1000_TXD_STAT_DD)) return -2;
    uint8_t *dst = g_nic.tx_bufs + (uint64_t)idx * E1000_PKT_BUF_SIZE;
    const uint8_t *src = (const uint8_t *)frame;
    for (uint16_t i = 0; i < len; i++) dst[i] = src[i];
    (void)e1000_prepare_tx_desc(td, g_nic.tx_bufs_pa + (uint64_t)idx * E1000_PKT_BUF_SIZE, len);
    __asm__ volatile("mfence" ::: "memory");
    g_nic.tx_idx = (idx + 1u) % E1000_RING_DESCS;
    nic_w32(E1000_REG_TDT, g_nic.tx_idx);
    return 0;
}

int e1000_rx_raw(void *buf, uint16_t max_len, uint16_t *len_out) {
    if (!g_nic.info.active || !buf) return -1;
    uint32_t idx = g_nic.rx_idx;
    E1000RxDesc *rd = &g_nic.rx_ring[idx];
    uint16_t pkt_len = 0u;
    int rc = e1000_check_rx_desc(rd, &pkt_len);
    if (rc <= 0) return rc;
    uint16_t copy_len = (pkt_len < max_len) ? pkt_len : max_len;
    const uint8_t *src = g_nic.rx_bufs + (uint64_t)idx * E1000_PKT_BUF_SIZE;
    uint8_t *dst = (uint8_t *)buf;
    for (uint16_t i = 0; i < copy_len; i++) dst[i] = src[i];
    if (len_out) *len_out = copy_len;
    rd->status = 0u;
    __asm__ volatile("mfence" ::: "memory");
    nic_w32(E1000_REG_RDT, idx);
    g_nic.rx_idx = (idx + 1u) % E1000_RING_DESCS;
    return 1;
}
