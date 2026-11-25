#include "ide.h"

#ifndef SPIN_LIMIT
#define SPIN_LIMIT 50000000u
#endif

typedef struct {
    IdeInfo info;
    BlkDev  dev;
} IdeState;

static IdeState g_ide;

static inline void outb(uint16_t p, uint8_t v)  { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p)          { uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p)         { uint16_t v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outw(uint16_t p, uint16_t v){ __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(p)); }

static void io_delay(uint16_t ctrl) { (void)inb(ctrl); (void)inb(ctrl); (void)inb(ctrl); (void)inb(ctrl); }

static int wait_not_busy(uint16_t cmd, uint8_t *st_out) {
    for (uint32_t i = 0; i < SPIN_LIMIT; i++) {
        uint8_t st = inb(cmd + IDE_REG_STATUS);
        if (ide_status_ready(st)) { if (st_out) *st_out = st; return 0; }
        __asm__ volatile("pause");
    }
    return -1;
}

static int wait_drq(uint16_t cmd) {
    uint8_t st = 0;
    if (wait_not_busy(cmd, &st) != 0) return -1;
    if (ide_status_err(st)) return -2;
    return ide_status_drq(st) ? 0 : -3;
}

static void load_taskfile(uint16_t cmd, uint16_t ctrl, const IdeTaskfile *tf, int lba48) {
    outb(cmd + IDE_REG_HDDEVSEL, tf->drive_head);
    io_delay(ctrl);
    if (lba48) {
        outb(cmd + IDE_REG_SECCOUNT, tf->seccount_hi);
        outb(cmd + IDE_REG_LBA0, tf->lba[3]);
        outb(cmd + IDE_REG_LBA1, tf->lba[4]);
        outb(cmd + IDE_REG_LBA2, tf->lba[5]);
    }
    outb(cmd + IDE_REG_SECCOUNT, tf->seccount_lo);
    outb(cmd + IDE_REG_LBA0, tf->lba[0]);
    outb(cmd + IDE_REG_LBA1, tf->lba[1]);
    outb(cmd + IDE_REG_LBA2, tf->lba[2]);
}

static int ide_rw_sectors(uint64_t lba, uint32_t count, void *buf, int write) {
    uint16_t cmd = g_ide.info.cmd_base, ctrl = g_ide.info.ctrl_base;
    uint8_t *p = (uint8_t *)buf;
    while (count > 0u) {
        uint32_t n = count > 255u ? 255u : count;
        int lba48 = (lba + n) >= 0x0FFFFFFFULL;
        IdeTaskfile tf;
        if (lba48) ide_pack_lba48(&tf, g_ide.info.slave, lba, (uint16_t)n);
        else       ide_pack_lba28(&tf, g_ide.info.slave, (uint32_t)lba, (uint8_t)n);
        if (wait_not_busy(cmd, 0) != 0) return BLK_E_IO;
        load_taskfile(cmd, ctrl, &tf, lba48);
        uint8_t op = write ? (lba48 ? IDE_CMD_WRITE_PIO_EXT : IDE_CMD_WRITE_PIO)
                           : (lba48 ? IDE_CMD_READ_PIO_EXT  : IDE_CMD_READ_PIO);
        outb(cmd + IDE_REG_COMMAND, op);
        for (uint32_t s = 0; s < n; s++) {
            if (wait_drq(cmd) != 0) return BLK_E_IO;
            if (write) {
                for (uint32_t w = 0; w < 256u; w++) {
                    uint16_t v = (uint16_t)p[w*2u] | ((uint16_t)p[w*2u+1u] << 8);
                    outw(cmd + IDE_REG_DATA, v);
                }
            } else {
                for (uint32_t w = 0; w < 256u; w++) {
                    uint16_t v = inw(cmd + IDE_REG_DATA);
                    p[w*2u] = (uint8_t)(v & 0xFFu); p[w*2u+1u] = (uint8_t)(v >> 8);
                }
            }
            p += 512u;
        }
        if (write) outb(cmd + IDE_REG_COMMAND, IDE_CMD_CACHE_FLUSH);
        lba += n; count -= n;
    }
    return BLK_OK;
}

static int ide_blk_read(void *ctx, uint64_t lba, uint32_t count, void *buf) { (void)ctx; return ide_rw_sectors(lba, count, buf, 0); }
static int ide_blk_write(void *ctx, uint64_t lba, uint32_t count, const void *buf) { (void)ctx; return ide_rw_sectors(lba, count, (void *)buf, 1); }
static int ide_blk_flush(void *ctx) {
    (void)ctx; if (!g_ide.info.active) return BLK_E_IO;
    outb(g_ide.info.cmd_base + IDE_REG_COMMAND, IDE_CMD_CACHE_FLUSH);
    return wait_not_busy(g_ide.info.cmd_base, 0) == 0 ? BLK_OK : BLK_E_IO;
}
static const BlkOps g_ide_ops = { ide_blk_read, ide_blk_write, ide_blk_flush, 0 };

int ide_init_channel(uint16_t cmd_base, uint16_t ctrl_base, uint8_t slave, BlkDev *out, IdeInfo *info_out) {
    outb(ctrl_base, 0x02u); /* nIEN = 1 (polled mode) */
    outb(cmd_base + IDE_REG_HDDEVSEL, (uint8_t)(0xA0u | ((slave & 1u) << 4)));
    io_delay(ctrl_base);
    uint8_t st = inb(cmd_base + IDE_REG_STATUS);
    if (st == 0xFFu || st == 0x00u) return -1;
    outb(cmd_base + IDE_REG_SECCOUNT, 0u);
    outb(cmd_base + IDE_REG_LBA0, 0u);
    outb(cmd_base + IDE_REG_LBA1, 0u);
    outb(cmd_base + IDE_REG_LBA2, 0u);
    outb(cmd_base + IDE_REG_COMMAND, IDE_CMD_IDENTIFY);
    if (inb(cmd_base + IDE_REG_STATUS) == 0u) return -2;
    if (wait_drq(cmd_base) != 0) return -3;
    uint16_t idw[256];
    for (uint32_t i = 0; i < 256u; i++) idw[i] = inw(cmd_base + IDE_REG_DATA);
    uint64_t secs = ide_parse_identify_sectors(idw);
    if (secs == 0ULL) return -4;
    g_ide.info.active = 1; g_ide.info.cmd_base = cmd_base; g_ide.info.ctrl_base = ctrl_base;
    g_ide.info.slave = slave & 1u; g_ide.info.sectors = secs;
    ide_parse_identify_model(idw, g_ide.info.model, sizeof(g_ide.info.model));
    g_ide.dev.ctx = &g_ide; g_ide.dev.ops = &g_ide_ops;
    g_ide.dev.sector_size = 512u; g_ide.dev.sector_count = secs;
    if (out) *out = g_ide.dev;
    if (info_out) *info_out = g_ide.info;
    return 0;
}

int ide_probe_and_init(IdeInfo *out) {
    pci_scan();
    const PciDev *d = pci_find(0x01u, 0x01u, PCI_ANY_PROGIF, 0u);
    if (d) pci_enable(d);
    if (ide_init_channel(IDE_PRI_CMD_BASE, IDE_PRI_CTRL_BASE, 0u, 0, out) == 0) return 0;
    if (ide_init_channel(IDE_PRI_CMD_BASE, IDE_PRI_CTRL_BASE, 1u, 0, out) == 0) return 0;
    if (ide_init_channel(IDE_SEC_CMD_BASE, IDE_SEC_CTRL_BASE, 0u, 0, out) == 0) return 0;
    return ide_init_channel(IDE_SEC_CMD_BASE, IDE_SEC_CTRL_BASE, 1u, 0, out);
}

BlkDev *storage_ide_device(void) { return g_ide.info.active ? &g_ide.dev : 0; }
