#ifndef XASMOS_PROG_ABI_H
#define XASMOS_PROG_ABI_H

static inline long long sc4(long long nr, long long a, long long b, long long c,
                            long long d, long long *out) {
    long long rax, rdx;
    __asm__ volatile ("mov %6, %%rcx\n\tint $0x80"
                      : "=a"(rax), "=d"(rdx)
                      : "a"(nr), "D"(a), "S"(b), "d"(c), "r"(d)
                      : "rcx", "r11", "memory");
    if (out) *out = rdx;
    return rax;
}

static inline long long sc(long long nr, long long a, long long b, long long c,
                           long long *out) {
    return sc4(nr, a, b, c, 0, out);
}

#define PROG_ENTRY(fn) \
    __asm__(".globl _start\n" \
            "_start:\n\t" \
            "andq $-16, %rsp\n\t" \
            "call " #fn "\n\t" \
            "ud2\n")

#endif
