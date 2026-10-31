/* xerlibc -- string primitives. Same build flags as mem.c. */
#include <xerlibc/string.h>

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n] != '\0') n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    while (*p && *p == *q) { p++; q++; }
    return (int)*p - (int)*q;
}

int strncmp(const char *a, const char *b, size_t n) {
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != q[i]) return (int)p[i] - (int)q[i];
        if (p[i] == '\0') return 0;
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++) != '\0') ;
    return dst;
}

/* Standard semantics: copies at most n bytes and pads with NULs up to n. */
char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

char *strchr(const char *s, int c) {
    char v = (char)c;
    for (;; s++) {
        if (*s == v) return (char *)s;
        if (*s == '\0') return 0;
    }
}
