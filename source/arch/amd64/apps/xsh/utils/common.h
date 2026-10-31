/* xsh utilities -- shared output helpers (kernel console side).
 * Path: source/arch/amd64/apps/xsh/utils/common.h */
#ifndef XSH_UTILS_COMMON_H
#define XSH_UTILS_COMMON_H

#include <xerlibc/fmt.h>

extern void xk_print(const char *s);
extern void xk_println(const char *s);

static inline void put_dec(unsigned long long v) {
    char buf[24];
    size_t n = xl_u64_dec(buf, v);
    buf[n] = '\0';
    xk_print(buf);
}

static inline void put_hex(unsigned long long v, unsigned width) {
    char buf[24];
    size_t n = xl_u64_hex(buf, v, width);
    buf[n] = '\0';
    xk_print(buf);
}

#endif
