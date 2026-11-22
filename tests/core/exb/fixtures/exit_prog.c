/* exit.exb: the first program. The boot launches it with its owner (init_owner)
 * and four arguments: rdi = root of rd0, rsi = root of nd0, rdx = the boot
 * budget, rcx = the shell's SEND. It checks that both roots attenuate, returns
 * the probes, derives the shell's share of the budget and offers it through the
 * shell's SEND. It names no device and keeps no authority it was not given. */
#define SYS_EXIT          1
#define SYS_FREE          7
#define SYS_DERIVE        13
#define SYS_BUDGET_DERIVE 15
#define SYS_OFFER         16
#define RIGHTS_RR         (1ull | 32ull)
#define SHELL_FRAMES      8192ull

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

/* The stub only aligns the stack. rdi, rsi, rdx and rcx survive the call, so
 * exit_main receives the four arguments in the SysV order. */
__asm__(".globl _start\n_start:\n\tandq $-16, %rsp\n\tcall exit_main\n\tud2\n");

static int derive_child(long long parent, long long *child) {
    long long st = sc4(SYS_DERIVE, parent, 0, 8, RIGHTS_RR, child);
    return st == 0 && *child != 0;
}

void exit_main(unsigned long long rd0, unsigned long long nd0,
               unsigned long long bud, unsigned long long send) {
    long long c_rd0 = 0, c_nd0 = 0, share = 0;

    if (!rd0 || !derive_child((long long)rd0, &c_rd0))
        sc4(SYS_EXIT, 2, 0, 0, 0, 0);                    /* rd0 root not usable */

    if (!nd0 || !derive_child((long long)nd0, &c_nd0)) {
        sc4(SYS_FREE, c_rd0, 0, 0, 0, 0);                /* return what we took */
        sc4(SYS_EXIT, 3, 0, 0, 0, 0);                    /* nd0 root not usable */
    }

    if (sc4(SYS_FREE, c_nd0, 0, 0, 0, 0) != 0) sc4(SYS_EXIT, 4, 0, 0, 0, 0);
    if (sc4(SYS_FREE, c_rd0, 0, 0, 0, 0) != 0) sc4(SYS_EXIT, 5, 0, 0, 0, 0);

    /* The budget: no budget or no SEND means exit has nothing to give. */
    if (!bud || !send) sc4(SYS_EXIT, 6, 0, 0, 0, 0);

    long long st = sc4(SYS_BUDGET_DERIVE, (long long)bud, SHELL_FRAMES, 0, 0, &share);
    /* 70 + (-rax): 71 NOSYS, 72 INVAL, 73 NOMEM, 70 = syscall ok but no child. */
    if (st != 0) sc4(SYS_EXIT, 70 + (-st), 0, 0, 0, 0);
    if (share == 0) sc4(SYS_EXIT, 70, 0, 0, 0, 0);

    if (sc4(SYS_OFFER, share, (long long)send, 0, 0, 0) != 0)
        sc4(SYS_EXIT, 8, 0, 0, 0, 0);

    sc4(SYS_EXIT, 0, 0, 0, 0, 0);
}
