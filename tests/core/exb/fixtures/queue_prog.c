/* End-to-end test of the program ring ABI. Reads block 0 of |dev|rd0 through
 * the program's own ring. The data frame is mapped only after the completion
 * (xtok_ring_pin refuses mapped frames). On exit the program gives back every
 * token it asked for, so the teardown count is only its code and stack. */
#include "xqueue.h"

#define SYS_LOG            2
#define SYS_YIELD          3
#define SYS_ALLOC          4
#define SYS_MAP            5
#define SYS_UNMAP          6
#define SYS_FREE           7
#define SYS_REQUEST_BLOCKS 9
#define SYS_QUEUE_CREATE  10
#define SYS_QUEUE_RING    11
#define SYS_QUEUE_CLOSE   12
#define SYS_EXIT           1

#define RIGHTS_RW  (1ull | 2ull | 8ull | 32ull)   /* READ | WRITE | MAP | RELEASE */
#define PERM_RW    (1ull | 2ull)          /* XPERM_R | XPERM_W */
#define RING_VA    0x8000100000ull
#define DATA_VA    0x8000101000ull
#define RING_N     16ull
#define COOKIE     0x51554555ull          /* "QUEU" */

static long long sc4(long long nr, long long a, long long b, long long c,
                     long long d, long long *out) {
    long long rax, rdx;
    __asm__ volatile ("mov %6, %%rcx\n\tint $0x80"
        : "=a"(rax), "=d"(rdx)
        : "a"(nr), "D"(a), "S"(b), "d"(c), "r"(d)
        : "rcx", "r11", "memory");
    if (out) *out = rdx;
    return rax;
}
static long long sc(long long nr, long long a, long long b, long long c, long long *out) {
    return sc4(nr, a, b, c, 0, out);
}

static void yield(void) { sc(SYS_YIELD, 0, 0, 0, 0); }

void _start(void) {
    long long dtok = 0, bblk = 0, qtok = 0, rc, st;

    /* 2: block token on |dev|rd0, block 0 only, read right. The name is
     * "rd0": sys_request_blocks adds the |dev| prefix itself. */
    const char name[] = "rd0";
    st = sc4(SYS_REQUEST_BLOCKS, (long long)name, 0, 1, (1 | 32) /* rcx = READ | RELEASE */, &bblk);
    if (st != 0 || bblk == 0) sc(SYS_EXIT, 2, 0, 0, 0);

    /* 5: data frame, reserved but NOT mapped yet. */
    rc = sc(SYS_ALLOC, 1, RIGHTS_RW, 0, &dtok);
    if (rc != 0 || dtok == 0) sc(SYS_EXIT, 5, 0, 0, 0);

    /* 7: the kernel's ring; 4: mapped at RING_VA so the program can write SQEs. */
    rc = sc(SYS_QUEUE_CREATE, RING_N, 0, 0, &qtok);
    if (rc != 0 || qtok == 0) sc(SYS_EXIT, 7, 0, 0, 0);
    if (sc(SYS_MAP, qtok, RING_VA, PERM_RW, 0) != 0) sc(SYS_EXIT, 4, 0, 0, 0);

    /* One read SQE at slot 0, published by moving sq_tail. */
    XqSqe *sqe = (XqSqe *)(RING_VA + xq_sq_off());
    XqHeader *h = (XqHeader *)RING_VA;
    for (unsigned i = 0; i < XQ_SQE_BYTES; i++) ((volatile unsigned char *)sqe)[i] = 0;
    sqe->cookie    = COOKIE;
    sqe->blk_token = (uint64_t)bblk;
    sqe->ram_token = (uint64_t)dtok;
    sqe->lba       = 0;
    sqe->count     = 1;
    sqe->op        = XQ_OP_READ;
    h->sq_tail = 1;

    /* Doorbell as a hint, then yield until the completion is posted. */
    sc(SYS_QUEUE_RING, qtok, 0, 0, 0);
    for (int spins = 0; spins < 1000 && h->cq_tail == 0; spins++) yield();
    if (h->cq_tail == 0) sc(SYS_EXIT, 8, 0, 0, 0);

    XqCqe *cqe = (XqCqe *)(RING_VA + xq_cq_off(RING_N));
    if (cqe->cookie != COOKIE) sc(SYS_EXIT, 9, 0, 0, 0);
    /* 200 + (-status) for a device or validation error. */
    if (cqe->status != 0) sc(SYS_EXIT, 200 + (-cqe->status), 0, 0, 0);
    h->cq_head = 1;

    /* 6: only now map the data frame, and report its first byte. */
    if (sc(SYS_MAP, dtok, DATA_VA, PERM_RW, 0) != 0) sc(SYS_EXIT, 6, 0, 0, 0);
    sc(SYS_LOG, *(volatile unsigned char *)DATA_VA, 0, 0, 0);

    /* Teardown, one exit code per step so a failure is visible: 31 unmap of the
     * data frame, 32 free of the data token, 33 unmap of the ring, 34 close of
     * the ring, 35 free of the block token. */
    if (sc(SYS_UNMAP, DATA_VA, 0, 0, 0) != 0) sc(SYS_EXIT, 31, 0, 0, 0);
    if (sc(SYS_FREE, dtok, 0, 0, 0) != 0)     sc(SYS_EXIT, 32, 0, 0, 0);
    if (sc(SYS_UNMAP, RING_VA, 0, 0, 0) != 0) sc(SYS_EXIT, 33, 0, 0, 0);
    if (sc(SYS_QUEUE_CLOSE, qtok, 0, 0, 0) != 0) sc(SYS_EXIT, 34, 0, 0, 0);
    if (sc(SYS_FREE, bblk, 0, 0, 0) != 0)     sc(SYS_EXIT, 35, 0, 0, 0);
    sc(SYS_EXIT, 0, 0, 0, 0);
}
