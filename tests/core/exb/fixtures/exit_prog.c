/* exit.exb: the first program. The boot launches it with its owner (init_owner)
 * and the two disk roots in rdi and rsi. It attenuates each root, returns the
 * probes, and reports. It names no device and decides no recipient. */
#define SYS_EXIT   1
#define SYS_FREE   7
#define SYS_DERIVE 13
#define RIGHTS_RR  (1ull | 32ull)   /* READ | RELEASE */

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

/* Entry: rdi and rsi carry the roots. The stub only aligns the stack and calls
 * the C code, so both registers reach exit_main unchanged. */
__asm__(".globl _start\n_start:\n\tandq $-16, %rsp\n\tcall exit_main\n\tud2\n");

static int derive_child(long long parent, long long *child) {
    long long st = sc4(SYS_DERIVE, parent, 0, 8, RIGHTS_RR, child);
    return st == 0 && *child != 0;
}

void exit_main(unsigned long long rd0, unsigned long long nd0) {
    long long c_rd0 = 0, c_nd0 = 0;

    if (!rd0 || !derive_child((long long)rd0, &c_rd0))
        sc4(SYS_EXIT, 2, 0, 0, 0, 0);                    /* rd0 root not usable */

    if (!nd0 || !derive_child((long long)nd0, &c_nd0)) {
        sc4(SYS_FREE, c_rd0, 0, 0, 0, 0);                /* return what we took */
        sc4(SYS_EXIT, 3, 0, 0, 0, 0);                    /* nd0 root not usable */
    }

    if (sc4(SYS_FREE, c_nd0, 0, 0, 0, 0) != 0) sc4(SYS_EXIT, 4, 0, 0, 0, 0);
    if (sc4(SYS_FREE, c_rd0, 0, 0, 0, 0) != 0) sc4(SYS_EXIT, 5, 0, 0, 0, 0);

    sc4(SYS_EXIT, 0, 0, 0, 0, 0);
}
