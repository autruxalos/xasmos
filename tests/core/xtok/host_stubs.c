/* Host replacements for what the kernel gets from physical memory and devices. */
#include "capability.h"
#include <stdlib.h>
#include <string.h>
#include "kmemory.h"
#include "host_stubs.h"

UINT64 phys_alloc_frames(UINT64 n) {
    void *p = aligned_alloc(4096, (size_t)(n * 4096));
    if (!p) return 0;
    memset(p, 0x5A, (size_t)(n * 4096));
    return (UINT64)(uintptr_t)p;
}

static UINT64 g_phys_freed;
void phys_free_frames(UINT64 addr, UINT64 n) {
    g_phys_freed += n;
    free((void *)(uintptr_t)addr);
}
UINT64 host_phys_freed(void) { return g_phys_freed; }

UINT64 phys_alloc_huge(void) { return phys_alloc_frames(512); }

static uint8_t *g_disk;
static uint32_t g_ss;
static int      g_fail;

static int d_read(void *ctx, uint64_t lba, uint32_t n, void *buf) {
    (void)ctx;
    if (g_fail) return BLK_E_IO;
    memcpy(buf, g_disk + lba * g_ss, (size_t)n * g_ss);
    return BLK_OK;
}

static int d_write(void *ctx, uint64_t lba, uint32_t n, const void *buf) {
    (void)ctx;
    if (g_fail) return BLK_E_IO;
    memcpy(g_disk + lba * g_ss, buf, (size_t)n * g_ss);
    return BLK_OK;
}

static int d_flush(void *ctx) { (void)ctx; return BLK_OK; }

static const BlkOps g_ops = { d_read, d_write, d_flush, NULL };

void host_dev_init(BlkDev *d, uint32_t ss, uint64_t sc) {
    free(g_disk);
    g_disk = calloc((size_t)sc, ss);
    g_ss = ss;
    g_fail = 0;
    d->ctx          = NULL;
    d->ops          = &g_ops;
    d->sector_size  = ss;
    d->sector_count = sc;
}

void host_dev_fail(int on) { g_fail = on; }

/* Host stub: the address space is not built on the host. Revocation of
 * mappings is a no-op here; token_ledger's own revocation still runs. */
int xas_revoke_token(XToken tok) { (void)tok; return 0; }
