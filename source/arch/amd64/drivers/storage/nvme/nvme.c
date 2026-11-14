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
#include "pci.h"
#include "irqvec.h"
#include "lapic.h"
#include "nvme_xq.h"
#include "nvme_q.h"
#include "capability.h"

#define QD            64u                   /* entries per queue               */
#define SQE_BYTES     64u
#define CQE_BYTES     16u
#define BOUNCE_BYTES  4096u
#ifndef SPIN_LIMIT
#ifndef SPIN_LIMIT
#define SPIN_LIMIT 50000000u   /* tests override this */
#endif
#endif

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
    uint32_t sq_head;          /* controller's consumed SQ position */
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

static int g_io_irq;      /* 1 when the I/O CQ asks for an interrupt */
static int g_irq_vec = -1;

static void nvme_notice(void *ctx) { (void)ctx; }

/* Programs MSI-X entry 0 of the controller: this CPU, the reserved vector,
 * unmasked. The function stays masked until the entry is written. */
static int msix_program(const PciDev *d) {
    PciMsix mx;
    UINT64 virt = 0;
    if (pci_msix_info(d, &mx) != 0) return -1;
    if (xpt_map_uncached(d->bar[mx.bir] + mx.table_off,
                         (UINT64)mx.entries * 16u, &virt) != 0) return -1;

    int vec = irq_vector_alloc(nvme_notice, 0);
    if (vec < 0) return -1;

    pci_msix_control(d, mx.cap, 1, 1);
    volatile uint32_t *e = (volatile uint32_t *)(UINTN)virt;
    uint32_t addr = 0xFEE00000u | (lapic_id() << 12);
    e[0] = addr;
    e[1] = 0;
    e[2] = (uint32_t)vec;
    e[3] = 0;
    pci_msix_control(d, mx.cap, 1, 0);

    if (e[0] != addr || e[2] != (uint32_t)vec) return -1;
    g_irq_vec = vec;
    return 0;
}

/* Vector of the I/O completion interrupt, or -1 if polling only. */
int nvme_irq_vector(void) { return g_irq_vec; }

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
/* Submits one command: writes the SQE and rings the doorbell. Returns the slot
 * the completion will use, so the caller can reap it later. No waiting here. */
static int queue_reap(Queue *q, uint32_t *dw3);

/* ---- queue mechanics: nvme_q does the work, Queue keeps the state ----------
 * Each call builds a view of the Queue, runs the tested mechanics, and stores
 * the indices back. Doorbells are derived from q->qid, so callers' db arguments
 * are no longer needed (they stay in the signatures until the cleanup step). */
static void nq_ring_sq(void *arg, uint32_t tail) {
    Queue *q = (Queue *)arg;
    __asm__ volatile ("sfence" ::: "memory");      /* SQE visible before the doorbell */
    wr32(sq_doorbell(q->qid), tail);
}

static void nq_ring_cq(void *arg, uint32_t head) {
    Queue *q = (Queue *)arg;
    wr32(cq_doorbell(q->qid), head);
}

static void q_view(Queue *q, NvmeQ *n) {
    n->sq = q->sq;
    n->cq = q->cq;
    n->depth = QD;
    n->tail = q->tail;
    n->head = q->head;
    n->phase = q->phase;
    n->sq_head = q->sq_head;
    n->db_arg = q;
    n->ring_sq = nq_ring_sq;
    n->ring_cq = nq_ring_cq;
}

static void q_store(Queue *q, const NvmeQ *n) {
    q->tail = n->tail;
    q->head = n->head;
    q->phase = n->phase;
    q->sq_head = n->sq_head;
}

/* Waits for a free SQ entry, reaping while it waits, then submits. Asynchronous
 * completions that arrive during the wait are routed, not lost. A synchronous
 * completion here is a protocol error: io_exec never has one pending. */
/* Non-blocking: writes one SQE and rings the doorbell if the SQ has room.
 * Returns BLK_OK, or BLK_E_BUSY with nothing written and no doorbell. It never
 * reaps, so it is the only submission primitive the asynchronous path may use. */
static int queue_try_submit(Queue *q, const uint8_t *cmd) {
    NvmeQ n;
    q_view(q, &n);
    if (nvme_q_submit(&n, cmd) != 0) return BLK_E_BUSY;
    q_store(q, &n);
    return BLK_OK;
}

/* Synchronous policy: waits for a free SQ entry, reaping while it waits.
 * Asynchronous completions that arrive during the wait are routed, not lost.
 * A synchronous completion here is a protocol error: io_exec never has one
 * pending. Same codes as before: BLK_E_TIMEOUT, or BLK_E_IO on protocol error. */
static int queue_submit(Queue *q, const uint8_t *cmd) {
    uint32_t spins = 0;
    for (;;) {
        int rc = queue_try_submit(q, cmd);
        if (rc == BLK_OK) return BLK_OK;
        if (spins++ >= SPIN_LIMIT) return BLK_E_TIMEOUT;
        uint32_t dw3 = 0;
        if (queue_reap(q, &dw3) == BLK_OK) return BLK_E_IO;
    }
}

/* ---- completion routes: one callback per ring, selected by the cid -------
 * An asynchronous cid carries its ring (bits 14:10) and its slot (bits 9:0).
 * A completion is delivered only to the callback bound to its ring. A ring with
 * no route drops the completion and counts it: it is never handed to another
 * ring. */
#define NVME_ROUTES 32u
typedef void (*NvmeRouteFn)(void *arg, uint32_t slot, int32_t st);
static struct { NvmeRouteFn fn; void *arg; } g_route[NVME_ROUTES];
static uint32_t g_route_dropped;
uint32_t nvme_route_dropped(void) { return g_route_dropped; }

int nvme_route_bind(uint32_t ring, NvmeRouteFn fn, void *arg) {
    if (ring >= NVME_ROUTES || fn == 0) return -1;
    if (g_route[ring].fn != 0) return -1;        /* never steal a live route */
    g_route[ring].fn  = fn;
    g_route[ring].arg = arg;
    return 0;
}

void nvme_route_unbind(uint32_t ring) {
    if (ring >= NVME_ROUTES) return;
    g_route[ring].fn  = 0;
    g_route[ring].arg = 0;
}

static void nvme_route_deliver(uint32_t ring, uint32_t slot, int32_t st) {
    if (ring >= NVME_ROUTES || g_route[ring].fn == 0) {
        g_route_dropped++;
        return;
    }
    g_route[ring].fn(g_route[ring].arg, slot, st);
}

/* Reaps the completion at the head if it is new, and advances the head. Returns
 * BLK_OK with DW3 in *dw3 for a synchronous completion, 1 when an asynchronous
 * completion was consumed and routed, or BLK_E_TIMEOUT when nothing new has
 * arrived. Also takes the controller's SQ head from DW2, which frees SQ slots. */
static int queue_reap(Queue *q, uint32_t *dw3) {
    NvmeQ n;
    uint16_t cid = 0;
    uint32_t w3 = 0;
    q_view(q, &n);
    if (nvme_q_reap(&n, &cid, &w3) != 0) return BLK_E_TIMEOUT;
    q_store(q, &n);
    if (q->qid == 1u && (cid & NVME_XQ_CID_BASE) != 0) {
        uint32_t st = nvme_cqe_status(w3);
        nvme_route_deliver((cid >> 10) & 0x1Fu, cid & 0x3FFu,
                           st == 0 ? XQ_ST_OK : -(int32_t)st);
        return 1;
    }
    *dw3 = w3;
    return BLK_OK;
}

/* Submits one command and waits for its completion. Same contract as before:
 * BLK_OK with DW3 in *dw3, or BLK_E_TIMEOUT after SPIN_LIMIT polls. */
static int queue_exec(Queue *q, const uint8_t *cmd, uint32_t *dw3) {
    if (queue_submit(q, cmd) != BLK_OK) return BLK_E_TIMEOUT;
    for (uint32_t spins = 0; spins < SPIN_LIMIT; spins++) {
        if (queue_reap(q, dw3) == BLK_OK) return BLK_OK;
    }
    return BLK_E_TIMEOUT;
}

static int admin_exec(uint8_t *cmd, uint32_t *dw3) {
    uint16_t cid = g_nvme.admin.next_cid++;
    cmd[2] = (uint8_t)cid;
    cmd[3] = (uint8_t)(cid >> 8);
    return queue_exec(&g_nvme.admin, cmd, dw3);
}

static int io_exec(uint8_t *cmd, uint32_t *dw3) {
    uint16_t cid = (uint16_t)(g_nvme.io.next_cid++ & 0x7FFFu);
    if (cid == 0) cid = 1;                 /* cid 0 is never used */
    cmd[2] = (uint8_t)cid;
    cmd[3] = (uint8_t)(cid >> 8);
    return queue_exec(&g_nvme.io, cmd, dw3);
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
    q->sq_head = 0;
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
    put32(cmd, 11, 1u | (g_io_irq ? 2u : 0u));             /* PC | IEN; IV = MSI-X entry 0 */
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

/* ---- asynchronous I/O for core/xqueue -----------------------------------
 * The adapter works on a NvmeXqQueue view of the I/O queue. The view is loaded
 * from g_nvme.io before each submission, and only the tail is stored back, so
 * the synchronous path and the asynchronous path share one tail. Head and phase
 * belong to queue_reap, the single consumer of the completion queue. */
static NvmeXqQueue g_xq;

static void xq_ring_sq(void *arg, uint32_t tail) {
    (void)arg;
    __asm__ volatile ("sfence" ::: "memory");     /* SQE visible before the doorbell */
    wr32(sq_doorbell(1), tail);
}

static void xq_ring_cq(void *arg, uint32_t head) {
    (void)arg;
    wr32(cq_doorbell(1), head);
}

static void xq_view_load(void) {
    g_xq.sq       = (uint8_t *)g_nvme.io.sq;
    g_xq.cq       = (uint8_t *)g_nvme.io.cq;
    g_xq.depth    = QD;
    g_xq.sq_tail  = g_nvme.io.tail;
    g_xq.sq_head  = g_nvme.io.sq_head;
    g_xq.cq_head  = g_nvme.io.head;
    g_xq.cq_phase = g_nvme.io.phase;
    g_xq.db_arg   = 0;
    g_xq.ring_sq  = xq_ring_sq;
    g_xq.ring_cq  = xq_ring_cq;
}

static void xq_view_store_tail(void) {
    g_nvme.io.tail = g_xq.sq_tail;
}

/* Submit function for the kernel ring (KrSubmitFn). The ring has already
 * checked and pinned both tokens: this only translates the entry into a
 * command. The device must be this controller's block device, and the frame
 * address comes from the RAM token. Returns 0, XQ_ST_BUSY if the SQ is full,
 * or BLK_E_IO if the entry does not name this controller. */
int nvme_blk_submit(void *ctx, const BlkReq *req) {
    int rc;
    (void)ctx;
    if (req->ring >= NVME_ROUTES) return BLK_E_IO;   /* no route can exist for it */
    xq_view_load();
    rc = nvme_xq_submit_raw(&g_xq, (req->ring << 10) | (req->slot & 0x3FFu),
                            req->op == BLK_OP_WRITE ? OP_WRITE : OP_READ,
                            1u, req->phys, req->lba, req->count);
    xq_view_store_tail();
    return rc == 0 ? 0 : XQ_ST_BUSY;
}


/* Consumes every completion already posted. Must not run while a synchronous
 * command waits for its own completion: the poll would take that completion
 * and no waiter would see it. The kernel runs with interrupts off outside the
 * controlled windows, so the only caller is the run loop. Returns the number
 * of asynchronous completions routed. */
uint32_t nvme_io_poll(void) {
    uint32_t dw3 = 0, n = 0;
    while (queue_reap(&g_nvme.io, &dw3) == 1) n++;
    return n;
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

static const BlkOps g_nvme_ops = { op_read, op_write, op_flush, nvme_blk_submit };

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
    g_io_irq = (msix_program(d) == 0);
    rc = create_io_queues();
    if (rc != BLK_OK) return rc;

    out->ctx = &g_nvme;
    out->ops = &g_nvme_ops;
    out->sector_size = g_nvme.sector_size;
    out->sector_count = g_nvme.sector_count;
    return BLK_OK;
}

/* Serial number of the controller (Identify Controller, bytes 4..23), with
 * trailing blanks removed. `out` must hold at least 21 bytes. */
int nvme_controller_serial(char *out, uint32_t max) {
    if (!out || max < 21u) return BLK_E_ARG;
    if (g_nvme.dma == 0) return BLK_E_IO;

    uint8_t cmd[SQE_BYTES];
    for (uint32_t i = 0; i < SQE_BYTES; i++) cmd[i] = 0;
    cmd[0] = ADM_IDENTIFY;
    put32(cmd, 10, 1u);                                /* CNS 1: controller */
    put64(cmd, 6, g_nvme.dma_phys);

    uint32_t dw3;
    if (admin_exec(cmd, &dw3) != BLK_OK) return BLK_E_TIMEOUT;
    if (nvme_cqe_status(dw3) != 0) return BLK_E_IO;

    static uint8_t ctl[BOUNCE_BYTES];   /* static: see identify_namespace */
    copy_out(ctl, g_nvme.dma, BOUNCE_BYTES);

    uint32_t n = 0;
    for (uint32_t i = 0; i < 20u; i++) out[n++] = (char)ctl[4 + i];
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
    return BLK_OK;
}



