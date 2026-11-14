/******************************************************************************
 * XASMOS - boot self-test: one read of nd0 through a kernel submission ring
 * Path: source/arch/amd64/kernel/xeret/xring_boot.c
 *
 * A kernel diagnostic, not a program: it runs once at boot, owned by the first
 * application's identity, and bypasses XSYS_REQUEST_BLOCKS and the device
 * grants on purpose. Programs will reach the same path through their own rings.
 ******************************************************************************/
#include "xring.h"
#include "dev.h"
#include "blk.h"
extern void xk_print(const char *s);
extern int  xk_println(const char *s);

/* NVMe adapter (nvme.c) */
int      nvme_route_bind(uint32_t ring, void (*fn)(void *arg, uint32_t slot, int32_t st), void *arg);
uint32_t nvme_io_poll(void);

#define BOOT_RING  0u   /* ring id in the NVMe cid; unique per controller */
#define BOOT_OWNER XTOK_OWNER_KERNEL   /* never shares a count with a program */

static KRing         g_boot_ring;
static uint8_t       g_boot_ring_mem[4096] __attribute__((aligned(64)));
static volatile int  g_boot_done;
static volatile int32_t g_boot_st;

static void boot_ring_done(void *arg, uint32_t slot, int32_t st) {
    if (kr_complete((KRing *)arg, slot, st) == 0) {
        g_boot_st   = st;
        g_boot_done = 1;
    }
}



int kr_boot_selftest_nvme(uint8_t *first_byte) {
    BlkDev *nd0 = dev_lookup("|dev|nd0");
    XToken blk, ram;
    UINT64 phys = 0, frames = 0;
    XqHeader *h;
    XqSqe *slots;
    XqSqe e;
    uint8_t *p = (uint8_t *)&e;
    uint32_t spins;

    if (!nd0) return -1;

    blk = xtok_alloc_blocks(BOOT_OWNER, nd0, 0, 8, XTOK_RIGHTS_BLOCKS_DEFAULT);
    ram = xtok_alloc_ram(BOOT_OWNER, 1, XTOK_RIGHTS_RAM_DEFAULT);
    if (blk == XTOKEN_INVALID || ram == XTOKEN_INVALID) return -2;
    if (xtok_ram_info(ram, &phys, &frames) != 0) return -2;

    xq_init(g_boot_ring_mem, 16);
    kr_init(&g_boot_ring, BOOT_OWNER, g_boot_ring_mem, 16, kr_device_submit, &g_boot_ring);
    g_boot_ring.id = BOOT_RING;
    if (nvme_route_bind(BOOT_RING, boot_ring_done, &g_boot_ring) != 0) return -5;
    g_boot_done = 0;

    for (uint32_t i = 0; i < sizeof e; i++) p[i] = 0;
    e.cookie    = 0x52494E47ULL;
    e.blk_token = blk;
    e.ram_token = ram;
    e.lba       = 0;
    e.count     = 1;
    e.op        = XQ_OP_READ;

    h     = (XqHeader *)g_boot_ring_mem;
    slots = (XqSqe *)(g_boot_ring_mem + xq_sq_off());
    slots[h->sq_tail & h->mask] = e;
    h->sq_tail = h->sq_tail + 1u;

    if (kr_process(&g_boot_ring) != 1) return -3;

    for (spins = 0; spins < 50000000u && !g_boot_done; spins++) {
        nvme_io_poll();
    }
    if (!g_boot_done) return -4;

    if (first_byte) *first_byte = *(volatile uint8_t *)(uintptr_t)phys;
    /* The pins are already gone (kr_complete). Release the tokens too, so the
     * owner's counts return to zero and the program's teardown does not
     * reclaim them. */
    if (g_boot_st == 0) {
        xtok_release(ram);
        xtok_release(blk);
    }
    return (int)g_boot_st;
}
