/* End-to-end test of XSYS_DERIVE. Asks for a block token on rd0, derives a
 * subrange of 8 blocks starting at block 8 (relative to the token), and reads
 * its first block through its own ring. The child carries only READ and
 * RELEASE: attenuation, never widening. Exit code is 500 + the byte read. */
#include "xqueue.h"

#define SYS_EXIT           1
#define SYS_YIELD          3
#define SYS_ALLOC          4
#define SYS_MAP            5
#define SYS_UNMAP          6
#define SYS_FREE           7
#define SYS_REQUEST_BLOCKS 9
#define SYS_QUEUE_CREATE  10
#define SYS_QUEUE_RING    11
#define SYS_QUEUE_CLOSE   12
#define SYS_DERIVE        13

#define RIGHTS_RW  (1ull | 2ull | 8ull | 32ull)  /* READ | WRITE | MAP | RELEASE */
#define RIGHTS_RR  (1ull | 32ull)                /* READ | RELEASE */
#define PERM_RW    (1ull | 2ull)
#define RING_VA    0x8000100000ull
#define DATA_VA    0x8000101000ull
#define RING_N     16ull
#define COOKIE     0x44524956ull                 /* "DRIV" */

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
    long long parent = 0, child = 0, dtok = 0, qtok = 0, rc, st;

    /* 2: block token on rd0, whole device (count 64 is inside the grant). */
    const char name[] = "rd0";
    st = sc4(SYS_REQUEST_BLOCKS, (long long)name, 0, 64, (1 | 32), &parent);
    if (st != 0 || parent == 0) sc(SYS_EXIT, 2, 0, 0, 0);

    /* 3: derive blocks 8..15 of the parent, read and release only. */
    st = sc4(SYS_DERIVE, parent, 8, 8, (1 | 32), &child);
    if (st != 0 || child == 0) sc(SYS_EXIT, 3, 0, 0, 0);

    /* 5: data frame, reserved, not mapped yet. */
    rc = sc(SYS_ALLOC, 1, RIGHTS_RW, 0, &dtok);
    if (rc != 0 || dtok == 0) sc(SYS_EXIT, 5, 0, 0, 0);

    /* 7: ring, mapped at RING_VA. */
    rc = sc(SYS_QUEUE_CREATE, RING_N, 0, 0, &qtok);
    if (rc != 0 || qtok == 0) sc(SYS_EXIT, 7, 0, 0, 0);
    if (sc(SYS_MAP, qtok, RING_VA, PERM_RW, 0) != 0) sc(SYS_EXIT, 4, 0, 0, 0);

    /* One read SQE: lba 0 is the child's first block, device block 8. */
    XqSqe *sqe = (XqSqe *)(RING_VA + xq_sq_off());
    XqHeader *h = (XqHeader *)RING_VA;
    for (unsigned i = 0; i < XQ_SQE_BYTES; i++) ((volatile unsigned char *)sqe)[i] = 0;
    sqe->cookie    = COOKIE;
    sqe->blk_token = (uint64_t)child;
    sqe->ram_token = (uint64_t)dtok;
    sqe->lba       = 0;
    sqe->count     = 1;
    sqe->op        = XQ_OP_READ;
    h->sq_tail = 1;

    sc(SYS_QUEUE_RING, qtok, 0, 0, 0);
    for (int spins = 0; spins < 1000 && h->cq_tail == 0; spins++) yield();
    if (h->cq_tail == 0) sc(SYS_EXIT, 8, 0, 0, 0);

    XqCqe *cqe = (XqCqe *)(RING_VA + xq_cq_off(RING_N));
    if (cqe->cookie != COOKIE) sc(SYS_EXIT, 9, 0, 0, 0);
    if (cqe->status != 0) sc(SYS_EXIT, 200 + (-cqe->status), 0, 0, 0);
    h->cq_head = 1;

    /* 6: map the data frame only now, and read its first byte. */
    if (sc(SYS_MAP, dtok, DATA_VA, PERM_RW, 0) != 0) sc(SYS_EXIT, 6, 0, 0, 0);
    unsigned char first = *(volatile unsigned char *)DATA_VA;

    /* Teardown, one code per step, child before parent. */
    if (sc(SYS_UNMAP, DATA_VA, 0, 0, 0) != 0)     sc(SYS_EXIT, 31, 0, 0, 0);
    if (sc(SYS_FREE, dtok, 0, 0, 0) != 0)         sc(SYS_EXIT, 32, 0, 0, 0);
    if (sc(SYS_UNMAP, RING_VA, 0, 0, 0) != 0)     sc(SYS_EXIT, 33, 0, 0, 0);
    if (sc(SYS_QUEUE_CLOSE, qtok, 0, 0, 0) != 0)  sc(SYS_EXIT, 34, 0, 0, 0);
    if (sc(SYS_FREE, child, 0, 0, 0) != 0)        sc(SYS_EXIT, 36, 0, 0, 0);
    if (sc(SYS_FREE, parent, 0, 0, 0) != 0)       sc(SYS_EXIT, 35, 0, 0, 0);

    /* Report the byte as 500 + value: run shows exit codes. */
    sc(SYS_EXIT, 500 + first, 0, 0, 0);
}
