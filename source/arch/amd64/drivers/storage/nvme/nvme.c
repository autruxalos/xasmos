/******************************************************************************
 * XASMOS - NVMe block driver (PCIe, polled, one I/O queue pair)
 * Path: source/arch/amd64/drivers/storage/nvme/nvme.c
 *
 * Bring-up follows the NVMe base specification:
 *   1. map BAR0 uncached; read CAP (doorbell stride, timeout)
 *   2. disable the controller (CC.EN=0) and wait for CSTS.RDY=0
 *   3. admin submission/completion queues; AQA, ASQ, ACQ
 *   4. enable (CC.EN=1, 64-byte SQEs, 16-byte CQEs, 4 KiB pages); wait RDY=1
 *   5. Identify Namespace 1: size and sector size
 *   6. create I/O completion queue 1, then I/O submission queue 1
 * Every wait is bounded: a dead controller returns BLK_E_TIMEOUT.
 *
 * Queues and the bounce page are whole frames from the physical allocator;
 * the kernel identity map makes them addressable, and their physical address
 * is what the controller is given. Interrupts are not used.
 ******************************************************************************/
#include "nvme.h"
#include "kmemory.h"
#include "paging.h"

#define QD            64u                   /* entries per queue               */
#define SQE_BYTES     64u
#define CQE_BYTES     16u
#define BOUNCE_BYTES  4096u
#define SPIN_LIMIT    50000000u

#define REG_CAP   0x00u
#define REG_CC    0x14u
#define REG_CSTS  0x1Cu
#define REG_AQA   0x24u
#define REG_ASQ   0x28u
#define REG_ACQ   0x30u
#define REG_DBL   0x1000u

#define OP_FLUSH        0x00u
#define OP_WRITE        0x01u
#define OP_READ         0x02u
#define ADM_CREATE_SQ  0x01u
#define ADM_CREATE_CQ  0x05u
#define ADM_IDENTIFY   0x06u

typedef struct {
    volatile uint8_t *sq;      /* submission ring, uncached-safe: frame memory */
    volatile uint8_t *cq;      /* completion ring */
    uint64_t sq_phys, cq_phys;
    uint32_t qid;
    uint32_t tail;             /* next SQE slot */
    uint32_t head;             /* next CQE slot */
    uint32_t phase;            /* expected phase of a new CQE */
    uint16_t next_cid;
} Queue;

typedef struct {
    volatile uint8_t *regs;    /* BAR0, mapped uncached */
    uint32_t dstrd;            /* doorbell stride, from CAP */
    Queue    admin;
    Queue    io;
    volatile uint8_t *dma;     /* bounce page */
    uint64_t dma_phys;
    uint64_t sector_count;
    uint32_t sector_size;
    uint32_t sec_per_xfer;     /* sectors that fit in one bounce page */
} NvmeCtx;

static NvmeCtx g_nvme;

/* ---- register access ---------------------------------------------------- */

static uint32_t rd32(uint32_t off) {
    return *(volatile uint32_t *)(g_nvme.regs + off);
}
static void wr32(uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(g_nvme.regs + off) = v;
}
static uint64_t rd64(uint32_t off) {
    return (uint64_t)rd32(off) | ((uint64_t)rd32(off + 4) << 32);
}
static void wr64(uint32_t off, uint64_t v) {
    wr32(off, (uint32_t)v);
    wr32(off + 4, (uint32_t)(v >> 32));
}

static uint32_t sq_doorbell(uint32_t qid) {
    return REG_DBL + (2u * qid) * (4u << g_nvme.dstrd);
}
static uint32_t cq_doorbell(uint32_t qid) {
    return REG_DBL + (2u * qid + 1u) * (4u << g_nvme.dstrd);
}

/* ---- pure helpers ------------------------------------------------------- */

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint64_t nvme_nsze_from_identify(const uint8_t *id) {
    return (uint64_t)le32(id) | ((uint64_t)le32(id + 4) << 32);
}

uint32_t nvme_lbads_from_identify(const uint8_t *id) {
    uint32_t flbas = id[26] & 0x0Fu;                  /* index of the active LBA format */
    uint32_t lbads = id[128 + 4 * flbas + 2];         /* LBAF[flbas].LBADS */
    if (lbads < 9 || lbads > 12) return 0;
    return lbads;
}

int nvme_cqe_is_new(uint32_t dw3, uint32_t expect_phase) {
    return ((dw3 >> 16) & 1u) == (expect_phase & 1u);
}

uint32_t nvme_cqe_status(uint32_t dw3) {
    return (dw3 >> 17) & 0x7FFFu;
}

/* ---- queues ------------------------------------------------------------- */

static void put32(uint8_t *c, uint32_t dw, uint32_t v) {
    c[dw * 4 + 0] = (uint8_t)v;
    c[dw * 4 + 1] = (uint8_t)(v >> 8);
    c[dw * 4 + 2] = (uint8_t)(v >> 16);
    c[dw * 4 + 3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *c, uint32_t dw, uint64_t v) {
    put32(c, dw, (uint32_t)v);
    put32(c, dw + 1, (uint32_t)(v >> 32));
}

/* Submits one command and waits for its completion. Returns BLK_OK and the
 * completion's DW3 in *dw3, or BLK_E_TIMEOUT. The status is not judged here. */
static int queue_exec(Queue *q, const uint8_t *cmd, uint32_t *dw3, uint32_t db_sq, uint32_t db_cq) {
    volatile uint8_t *slot = q->sq + q->tail * SQE_BYTES;
    for (uint32_t i = 0; i < SQE_BYTES; i++) slot[i] = cmd[i];

    q->tail = (q->tail + 1u) % QD;
    wr32(db_sq, q->tail);

    volatile uint32_t *cqe = (volatile uint32_t *)(q->cq + q->head * CQE_BYTES);
    for (uint32_t spins = 0; spins < SPIN_LIMIT; spins++) {
        uint32_t w3 = cqe[3];
        if (nvme_cqe_is_new(w3, q->phase)) {
            q->head = (q->head + 1u) % QD;
            if (q->head == 0) q->phase ^= 1u;
            wr32(db_cq, q->head);
            *dw3 = w3;
            return BLK_OK;
        }
    }
    return BLK_E_TIMEOUT;
}

static int admin_exec(uint8_t *cmd, uint32_t *dw3) {
    uint16_t cid = g_nvme.admin.next_cid++;
    cmd[2] = (uint8_t)cid;
    cmd[3] = (uint8_t)(cid >> 8);
    return queue_exec(&g_nvme.admin, cmd, dw3,
                      sq_doorbell(0), cq_doorbell(0));
}

static int io_exec(uint8_t *cmd, uint32_t *dw3) {
    uint16_t cid = g_nvme.io.next_cid++;
    cmd[2] = (uint8_t)cid;
    cmd[3] = (uint8_t)(cid >> 8);
    return queue_exec(&g_nvme.io, cmd, dw3,
                      sq_doorbell(g_nvme.io.qid), cq_doorbell(g_nvme.io.qid));
}

/* ---- bring-up ----------------------------------------------------------- */

static int wait_rdy(uint32_t want) {
    for (uint32_t spins = 0; spins < SPIN_LIMIT; spins++) {
        if ((rd32(REG_CSTS) & 1u) == want) return BLK_OK;
    }
    return BLK_E_TIMEOUT;
}

static int alloc_zeroed(uint64_t *phys, volatile uint8_t **virt) {
    uint64_t p = phys_alloc_frames(1);
    if (p == 0) return BLK_E_IO;
    volatile uint8_t *v = (volatile uint8_t *)(UINTN)p;
    for (uint32_t i = 0; i < XERET_FRAME_SIZE; i++) v[i] = 0;
    *phys = p;
    *virt = v;
    return BLK_OK;
}

static int queue_alloc(Queue *q, uint32_t qid) {
    q->qid = qid;
    q->tail = q->head = 0;
    q->phase = 1;
    q->next_cid = 1;
    if (alloc_zeroed(&q->sq_phys, &q->sq) != BLK_OK) return BLK_E_IO;
    if (alloc_zeroed(&q->cq_phys, &q->cq) != BLK_OK) return BLK_E_IO;
    return BLK_OK;
}

/* Copies n bytes from the bounce page to a caller buffer. */
static void copy_out(uint8_t *dst, const volatile uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

static void copy_in(volatile uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

static int identify_namespace(void) {
    uint8_t cmd[SQE_BYTES] = {0};
    cmd[0] = ADM_IDENTIFY;
    put32(cmd, 1, 1);                                  /* NSID 1 */
    put64(cmd, 6, g_nvme.dma_phys);                    /* PRP1: bounce page */
    put32(cmd, 10, 0);                                 /* CNS 0: namespace */

    uint32_t dw3;
    if (admin_exec(cmd, &dw3) != BLK_OK) return BLK_E_TIMEOUT;
    if (nvme_cqe_status(dw3) != 0) return BLK_E_IO;

static uint8_t id[BOUNCE_BYTES];   /* static: a 4 KiB stack frame would need __chkstk */
    copy_out(id, g_nvme.dma, BOUNCE_BYTES);
    uint64_t nsze = nvme_nsze_from_identify(id);
    uint32_t lbads = nvme_lbads_from_identify(id);
    if (nsze == 0 || lbads == 0) return BLK_E_IO;

    g_nvme.sector_count = nsze;
    g_nvme.sector_size = 1u << lbads;
    g_nvme.sec_per_xfer = BOUNCE_BYTES / g_nvme.sector_size;
    return BLK_OK;
}

static int create_io_queues(void) {
    uint8_t cmd[SQE_BYTES];
    uint32_t dw3;

    for (uint32_t i = 0; i < SQE_BYTES; i++) cmd[i] = 0;   /* Create I/O CQ */
    cmd[0] = ADM_CREATE_CQ;
    put32(cmd, 10, ((QD - 1u) << 16) | g_nvme.io.qid);
    put32(cmd, 11, 1u);                                    /* PC: physically contiguous */
    put64(cmd, 6, g_nvme.io.cq_phys);
    if (admin_exec(cmd, &dw3) != BLK_OK) return BLK_E_TIMEOUT;
    if (nvme_cqe_status(dw3) != 0) return BLK_E_IO;

    for (uint32_t i = 0; i < SQE_BYTES; i++) cmd[i] = 0;   /* Create I/O SQ bound to that CQ */
    cmd[0] = ADM_CREATE_SQ;
    put32(cmd, 10, ((QD - 1u) << 16) | g_nvme.io.qid);
    put32(cmd, 11, 1u | (g_nvme.io.qid << 16));            /* PC | CQID */
    put64(cmd, 6, g_nvme.io.sq_phys);
    if (admin_exec(cmd, &dw3) != BLK_OK) return BLK_E_TIMEOUT;
    if (nvme_cqe_status(dw3) != 0) return BLK_E_IO;
    return BLK_OK;
}

/* ---- block operations --------------------------------------------------- */

static int io_rw(uint8_t opcode, uint64_t lba, uint32_t nlb) {
    uint8_t cmd[SQE_BYTES];
    for (uint32_t i = 0; i < SQE_BYTES; i++) cmd[i] = 0;
    cmd[0] = opcode;
    put32(cmd, 1, 1);                                  /* NSID */
    put64(cmd, 6, g_nvme.dma_phys);                    /* PRP1 */
    put32(cmd, 10, (uint32_t)lba);
    put32(cmd, 11, (uint32_t)(lba >> 32));
    put32(cmd, 12, nlb - 1u);                          /* NLB is zero-based */

    uint32_t dw3;
    if (io_exec(cmd, &dw3) != BLK_OK) return BLK_E_TIMEOUT;
    return nvme_cqe_status(dw3) == 0 ? BLK_OK : BLK_E_IO;
}

static int op_read(void *ctx, uint64_t lba, uint32_t count, void *buf) {
    NvmeCtx *n = (NvmeCtx *)ctx;
    uint8_t *dst = (uint8_t *)buf;
    while (count > 0) {
        uint32_t nlb = count < n->sec_per_xfer ? count : n->sec_per_xfer;
        int rc = io_rw(OP_READ, lba, nlb);
        if (rc != BLK_OK) return rc;
        copy_out(dst, n->dma, nlb * n->sector_size);
        dst += nlb * n->sector_size;
        lba += nlb;
        count -= nlb;
    }
    return BLK_OK;
}

static int op_write(void *ctx, uint64_t lba, uint32_t count, const void *buf) {
    NvmeCtx *n = (NvmeCtx *)ctx;
    const uint8_t *src = (const uint8_t *)buf;
    while (count > 0) {
        uint32_t nlb = count < n->sec_per_xfer ? count : n->sec_per_xfer;
        copy_in(n->dma, src, nlb * n->sector_size);
        int rc = io_rw(OP_WRITE, lba, nlb);
        if (rc != BLK_OK) return rc;
        src += nlb * n->sector_size;
        lba += nlb;
        count -= nlb;
    }
    return BLK_OK;
}

static int op_flush(void *ctx) {
    (void)ctx;
    uint8_t cmd[SQE_BYTES];
    for (uint32_t i = 0; i < SQE_BYTES; i++) cmd[i] = 0;
    cmd[0] = OP_FLUSH;
    put32(cmd, 1, 1);
    uint32_t dw3;
    if (io_exec(cmd, &dw3) != BLK_OK) return BLK_E_TIMEOUT;
    return nvme_cqe_status(dw3) == 0 ? BLK_OK : BLK_E_IO;
}

static const BlkOps g_nvme_ops = { op_read, op_write, op_flush };

/* ---- entry point -------------------------------------------------------- */

int nvme_init(const PciDev *d, BlkDev *out) {
    if (!d || !out) return BLK_E_ARG;
    if (d->class_code != 0x01 || d->subclass != 0x08) return BLK_E_ARG;
    if (d->bar[0] == 0 || d->bar_size[0] == 0) return BLK_E_ARG;

    if (pci_enable(d) != 0) return BLK_E_IO;

    UINT64 va = 0;
    if (xpt_map_uncached(d->bar[0], d->bar_size[0], &va) != 0) return BLK_E_IO;
    g_nvme.regs = (volatile uint8_t *)(UINTN)va;

    uint64_t cap = rd64(REG_CAP);
    g_nvme.dstrd = (uint32_t)((cap >> 32) & 0xFu);

    /* 2. disable, wait for not ready */
    wr32(REG_CC, 0);
    if (wait_rdy(0) != BLK_OK) return BLK_E_TIMEOUT;

    /* 3. queues: admin pair, then the I/O pair (its memory is needed later) */
    if (queue_alloc(&g_nvme.admin, 0) != BLK_OK) return BLK_E_IO;
    if (queue_alloc(&g_nvme.io, 1) != BLK_OK) return BLK_E_IO;
    if (alloc_zeroed(&g_nvme.dma_phys, &g_nvme.dma) != BLK_OK) return BLK_E_IO;

    wr32(REG_AQA, ((QD - 1u) << 16) | (QD - 1u));
    wr64(REG_ASQ, g_nvme.admin.sq_phys);
    wr64(REG_ACQ, g_nvme.admin.cq_phys);

    /* 4. enable: 64-byte SQEs (IOSQES 6), 16-byte CQEs (IOCQES 4), 4 KiB pages */
    wr32(REG_CC, 1u | (6u << 16) | (4u << 20));
    if (wait_rdy(1) != BLK_OK) return BLK_E_TIMEOUT;

    /* 5. namespace geometry */
    int rc = identify_namespace();
    if (rc != BLK_OK) return rc;

    /* 6. the I/O queue pair */
    rc = create_io_queues();
    if (rc != BLK_OK) return rc;

    out->ctx = &g_nvme;
    out->ops = &g_nvme_ops;
    out->sector_size = g_nvme.sector_size;
    out->sector_count = g_nvme.sector_count;
    return BLK_OK;
}
