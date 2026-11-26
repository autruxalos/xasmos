#ifndef XASM_ARCH_SYSCALL_ABI_H
#define XASM_ARCH_SYSCALL_ABI_H
#include <stdint.h>

#define XASM_USER_IPC_VA 0x0000008000040000ULL

static inline int64_t xarch_sys3(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2) {
    int64_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "D"(a0), "S"(a1), "d"(a2) : "rcx", "r11", "memory");
    return r;
}

static inline int64_t xarch_sys3_out1(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t *out1) {
    int64_t r;
    uint64_t d = 0;
    __asm__ volatile("int $0x80" : "=a"(r), "=d"(d) : "a"(nr), "D"(a0), "S"(a1), "d"(a2) : "rcx", "r11", "memory");
    if (out1) *out1 = d;
    return r;
}
#endif
