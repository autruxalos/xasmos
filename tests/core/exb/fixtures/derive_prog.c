/* derive_prog: attenuates the lane it was lent (blocks 8..15 of the lane), reads
 * its first block through its own ring, and returns the lane. Exit = 500 + byte. */
#include "xqueue.h"
#include "prog_abi.h"

#define SYS_EXIT         1
#define SYS_YIELD        3
#define SYS_ALLOC        4
#define SYS_MAP          5
#define SYS_UNMAP        6
#define SYS_FREE         7
#define SYS_DERIVE       13
#define SYS_QUEUE_CREATE 10
#define SYS_QUEUE_RING   11
#define SYS_QUEUE_CLOSE  12
#define RIGHTS_RW (1ull | 2ull | 8ull | 32ull)
#define RIGHTS_RR (1ull | 32ull)
#define PERM_RW   (1ull | 2ull)
#define RING_VA   0x8000100000ull
#define DATA_VA   0x8000101000ull
#define RING_N    16ull
#define COOKIE    0x44524956ull

void prog_main(unsigned long long rdi, unsigned long long rsi,
               unsigned long long budget, unsigned long long lane) {
    long long child = 0, dtok = 0, qtok = 0, rc, st;
    (void)rdi; (void)rsi; (void)budget;

    if (lane == 0) sc(SYS_EXIT, 2, 0, 0, 0);

    st = sc4(SYS_DERIVE, (long long)lane, 8, 8, RIGHTS_RR, &child);
    if (st != 0 || child == 0) sc(SYS_EXIT, 3, 0, 0, 0);

    rc = sc(SYS_ALLOC, 1, RIGHTS_RW, 0, &dtok);
    if (rc != 0 || dtok == 0) sc(SYS_EXIT, 5, 0, 0, 0);

    rc = sc(SYS_QUEUE_CREATE, RING_N, (long long)budget, 0, &qtok);
    if (rc != 0 || qtok == 0) sc(SYS_EXIT, 7, 0, 0, 0);
    if (sc(SYS_MAP, qtok, RING_VA, PERM_RW, 0) != 0) sc(SYS_EXIT, 4, 0, 0, 0);

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
    for (int spins = 0; spins < 1000 && h->cq_tail == 0; spins++) sc(SYS_YIELD, 0, 0, 0, 0);
    if (h->cq_tail == 0) sc(SYS_EXIT, 8, 0, 0, 0);

    XqCqe *cqe = (XqCqe *)(RING_VA + xq_cq_off(RING_N));
    if (cqe->cookie != COOKIE) sc(SYS_EXIT, 9, 0, 0, 0);
    if (cqe->status != 0) sc(SYS_EXIT, 200 + (-cqe->status), 0, 0, 0);
    h->cq_head = 1;

    if (sc(SYS_MAP, dtok, DATA_VA, PERM_RW, 0) != 0) sc(SYS_EXIT, 6, 0, 0, 0);
    unsigned char first = *(volatile unsigned char *)DATA_VA;

    if (sc(SYS_UNMAP, DATA_VA, 0, 0, 0) != 0) sc(SYS_EXIT, 31, 0, 0, 0);
    if (sc(SYS_FREE, dtok, 0, 0, 0) != 0) sc(SYS_EXIT, 32, 0, 0, 0);
    if (sc(SYS_UNMAP, RING_VA, 0, 0, 0) != 0) sc(SYS_EXIT, 33, 0, 0, 0);
    if (sc(SYS_QUEUE_CLOSE, qtok, 0, 0, 0) != 0) sc(SYS_EXIT, 34, 0, 0, 0);
    if (sc(SYS_FREE, child, 0, 0, 0) != 0) sc(SYS_EXIT, 36, 0, 0, 0);
    if (sc(SYS_FREE, lane, 0, 0, 0) != 0) sc(SYS_EXIT, 35, 0, 0, 0);
    sc(SYS_EXIT, 500 + first, 0, 0, 0);
}
PROG_ENTRY(prog_main);
