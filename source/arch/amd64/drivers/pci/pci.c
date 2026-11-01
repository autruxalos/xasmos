/******************************************************************************
 * XASMOS - PCI bus enumeration (amd64, configuration mechanism 1)
 * Path: source/arch/amd64/drivers/pci/pci.c
 *
 * Sizing a BAR briefly disables memory decoding and turns interrupts off, so
 * no other code can reach the device while its address register holds all
 * ones. Command-register writes leave the status half at zero: status bits
 * are write-one-to-clear, so writing back what was read would erase them.
 ******************************************************************************/
#include "pci.h"

#define PCI_CFG_ADDR  0xCF8
#define PCI_CFG_DATA  0xCFC
#define PCI_CMD       0x04
#define PCI_BAR0      0x10
#define PCI_IF_BIT    (1ULL << 9)

static PciDev  g_devs[PCI_MAX_DEVICES];
static int     g_count;
static int     g_overflow;
static uint8_t g_bus_seen[256];

static inline void outl(uint16_t port, uint32_t v) {
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static uint32_t cfg_addr(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)(dev & 0x1F) << 11)
         | ((uint32_t)(fn & 0x07) << 8) | (off & 0xFCu);
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    outl(PCI_CFG_ADDR, cfg_addr(bus, dev, fn, off));
    return inl(PCI_CFG_DATA);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v) {
    outl(PCI_CFG_ADDR, cfg_addr(bus, dev, fn, off));
    outl(PCI_CFG_DATA, v);
}

/* Interrupt flag save/restore, so sizing is safe even if IF was set. */
static uint64_t irq_save(void) {
    uint64_t f;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(f) : : "memory");
    return f;
}

static void irq_restore(uint64_t f) {
    if (f & PCI_IF_BIT) __asm__ volatile ("sti" : : : "memory");
}

int pci_decode_bar(uint32_t raw_lo, uint32_t raw_hi, uint32_t m_lo, uint32_t m_hi,
                   uint64_t *base, uint64_t *size) {
    if (raw_lo & 1u) return -1;                       /* I/O space: not supported */

    int is64 = ((raw_lo >> 1) & 3u) == 2u;
    if (is64) {
        uint64_t mask = ((uint64_t)m_hi << 32) | (uint64_t)(m_lo & ~0xFu);
        if (mask == 0) return -1;
        *size = (~mask) + 1u;
        *base = ((uint64_t)raw_hi << 32) | (uint64_t)(raw_lo & ~0xFu);
        return 1;
    }

    uint32_t mask = m_lo & ~0xFu;
    if (mask == 0) return -1;
    *size = (uint64_t)(uint32_t)(~mask + 1u);
    *base = (uint64_t)(raw_lo & ~0xFu);
    return 0;
}

static void read_bars(PciDev *d, int count) {
    for (int i = 0; i < count; i++) {
        uint8_t off = (uint8_t)(PCI_BAR0 + 4 * i);
        uint32_t raw_lo = pci_read32(d->bus, d->dev, d->fn, off);
        if (raw_lo & 1u) continue;                    /* I/O BAR */

        int is64 = ((raw_lo >> 1) & 3u) == 2u;
        if (is64 && i == count - 1) continue;         /* no slot for the high half */

        uint32_t raw_hi = is64 ? pci_read32(d->bus, d->dev, d->fn, (uint8_t)(off + 4)) : 0;

        uint32_t cmd = pci_read32(d->bus, d->dev, d->fn, PCI_CMD);
        uint64_t irq = irq_save();

        pci_write32(d->bus, d->dev, d->fn, PCI_CMD, cmd & 0xFFFDu);  /* memory decode off */
        pci_write32(d->bus, d->dev, d->fn, off, 0xFFFFFFFFu);
        uint32_t m_lo = pci_read32(d->bus, d->dev, d->fn, off);
        pci_write32(d->bus, d->dev, d->fn, off, raw_lo);
        uint32_t m_hi = 0;
        if (is64) {
            pci_write32(d->bus, d->dev, d->fn, (uint8_t)(off + 4), 0xFFFFFFFFu);
            m_hi = pci_read32(d->bus, d->dev, d->fn, (uint8_t)(off + 4));
            pci_write32(d->bus, d->dev, d->fn, (uint8_t)(off + 4), raw_hi);
        }
        pci_write32(d->bus, d->dev, d->fn, PCI_CMD, cmd & 0xFFFFu);  /* restore; status = 0 */

        irq_restore(irq);

        uint64_t base = 0, size = 0;
        int r = pci_decode_bar(raw_lo, raw_hi, m_lo, m_hi, &base, &size);
        if (r < 0) continue;
        d->bar[i] = base;
        d->bar_size[i] = size;
        if (r == 1) i++;                              /* high half used this slot */
    }
}

static void scan_function(uint8_t bus, uint8_t dev, uint8_t fn);

static void scan_bus(uint8_t bus) {
    if (g_bus_seen[bus]) return;                      /* also breaks unconfigured-bridge loops */
    g_bus_seen[bus] = 1;

    for (uint8_t dev = 0; dev < 32; dev++) {
        uint32_t id = pci_read32(bus, dev, 0, 0x00);
        if ((id & 0xFFFFu) == 0xFFFFu) continue;

        uint8_t ht = (uint8_t)(pci_read32(bus, dev, 0, 0x0C) >> 16);
        scan_function(bus, dev, 0);
        if (ht & 0x80u) {                             /* multi-function device */
            for (uint8_t fn = 1; fn < 8; fn++) scan_function(bus, dev, fn);
        }
    }
}

static void scan_function(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint32_t id = pci_read32(bus, dev, fn, 0x00);
    if ((id & 0xFFFFu) == 0xFFFFu) return;

    if (g_count >= PCI_MAX_DEVICES) { g_overflow = 1; return; }

    uint32_t cls = pci_read32(bus, dev, fn, 0x08);
    uint8_t  ht  = (uint8_t)((pci_read32(bus, dev, fn, 0x0C) >> 16) & 0x7Fu);

    PciDev *d = &g_devs[g_count++];
    d->bus = bus;
    d->dev = dev;
    d->fn  = fn;
    d->vendor      = (uint16_t)(id & 0xFFFFu);
    d->device      = (uint16_t)(id >> 16);
    d->class_code  = (uint8_t)(cls >> 24);
    d->subclass    = (uint8_t)(cls >> 16);
    d->prog_if     = (uint8_t)(cls >> 8);
    d->header_type = ht;
    for (int i = 0; i < 6; i++) {
        d->bar[i] = 0;
        d->bar_size[i] = 0;
    }

    if (ht == 1 && d->class_code == 0x06 && d->subclass == 0x04) {
        uint8_t secondary = (uint8_t)(pci_read32(bus, dev, fn, 0x18) >> 8);
        scan_bus(secondary);
    }

    if (ht == 0)      read_bars(d, 6);
    else if (ht == 1) read_bars(d, 2);
}

int pci_scan(void) {
    g_count = 0;
    g_overflow = 0;
    for (int b = 0; b < 256; b++) g_bus_seen[b] = 0;
    scan_bus(0);
    return g_overflow ? -1 : g_count;
}

int pci_count(void) {
    return g_count;
}

const PciDev *pci_at(int index) {
    if (index < 0 || index >= g_count) return 0;
    return &g_devs[index];
}

const PciDev *pci_find(uint8_t class_code, uint8_t subclass, uint8_t prog_if, int skip) {
    for (int i = 0; i < g_count; i++) {
        const PciDev *d = &g_devs[i];
        if (d->class_code != class_code || d->subclass != subclass) continue;
        if (prog_if != PCI_ANY_PROGIF && d->prog_if != prog_if) continue;
        if (skip > 0) { skip--; continue; }
        return d;
    }
    return 0;
}

int pci_enable(const PciDev *d) {
    if (!d) return -1;
    uint32_t cmd = pci_read32(d->bus, d->dev, d->fn, PCI_CMD);
    cmd = (cmd & 0xFFFFu) | 0x6u;                     /* memory space + bus master */
    pci_write32(d->bus, d->dev, d->fn, PCI_CMD, cmd);
    return 0;
}
