/******************************************************************************
 * XASMOS - PCI bus enumeration and BAR access (amd64, configuration mechanism 1)
 * Path: source/arch/amd64/drivers/pci/pci.h
 *
 * Reads the bus through ports 0xCF8/0xCFC, follows PCI-PCI bridges to their
 * secondary buses, and sizes every memory BAR. A BAR address is never written
 * with a new value: sizing writes all ones and restores the firmware's value.
 ******************************************************************************/
#ifndef XASMOS_PCI_H
#define XASMOS_PCI_H

#include <stdint.h>

#define PCI_MAX_DEVICES  64
#define PCI_ANY_PROGIF   0xFF

typedef struct {
    uint8_t  bus, dev, fn;
    uint8_t  class_code, subclass, prog_if;
    uint8_t  header_type;          /* 0 endpoint, 1 bridge (bit 7 masked off) */
    uint16_t vendor, device;
    uint64_t bar[6];               /* memory BAR base; 0 if unused or I/O     */
    uint64_t bar_size[6];          /* bytes; 0 if unused                      */
} PciDev;

/* Enumerates the whole tree from bus 0. Returns the number of devices, or -1
 * if the table overflowed (the devices found so far are still usable). */
int  pci_scan(void);
int  pci_count(void);
const PciDev *pci_at(int index);

/* The skip-th device with this class (and prog_if, unless PCI_ANY_PROGIF),
 * or 0 if there is none. */
const PciDev *pci_find(uint8_t class_code, uint8_t subclass, uint8_t prog_if, int skip);

/* Enables memory decoding and bus mastering. Returns 0. */
int  pci_enable(const PciDev *d);

/* Raw configuration access, for registers outside the BARs. */
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v);

/* Decodes one BAR from its raw value and its value after sizing.
 *   raw_lo, raw_hi  the BAR as the firmware left it (raw_hi only for 64-bit)
 *   m_lo, m_hi      the same registers read back after writing all ones
 * Returns -1 for an I/O or unused BAR, 0 for a 32-bit memory BAR, and 1 for a
 * 64-bit memory BAR (which also occupies the next BAR slot). */
int  pci_decode_bar(uint32_t raw_lo, uint32_t raw_hi, uint32_t m_lo, uint32_t m_hi,
                    uint64_t *base, uint64_t *size);

#endif /* XASMOS_PCI_H */
