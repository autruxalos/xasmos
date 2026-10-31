/* xerlibc -- memory primitives.
 *
 * Built with -fno-builtin and -fno-tree-loop-distribute-patterns: otherwise
 * the compiler turns the loops below into calls to memcpy/memset, and these
 * functions would call themselves forever. */
#include <xerlibc/string.h>

void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s || d >= s + n) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    unsigned char v = (unsigned char)c;
    while (n--) *d++ = v;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = a;
    const unsigned char *q = b;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != q[i]) return (int)p[i] - (int)q[i];
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    unsigned char v = (unsigned char)c;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == v) return (void *)(p + i);
    }
    return 0;
}
