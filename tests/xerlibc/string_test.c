/* Host test for xerlibc string primitives.
 * Checks each function against an independent reference, with guard bytes
 * around every destination so an out-of-range write is caught. */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <xerlibc/string.h>

static uint64_t rs = 88172645463325252ULL;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)rs; }

static int fails, passes;
#define CHECK(cond, ...) do { if (cond) passes++; else { fails++; printf("FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

/* ---- independent references (no libc, no builtins) ---- */
static void ref_copy(unsigned char *d, const unsigned char *s, size_t n) {
    unsigned char tmp[4096];
    for (size_t i = 0; i < n; i++) tmp[i] = s[i];
    for (size_t i = 0; i < n; i++) d[i] = tmp[i];
}
static int ref_cmp(const unsigned char *a, const unsigned char *b, size_t n) {
    for (size_t i = 0; i < n; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

#define GUARD 16
#define BUF   1024

/* A buffer with guard bytes on both sides; the payload is at buf + GUARD. */
typedef struct { unsigned char raw[BUF + 2 * GUARD]; } Buf;
static unsigned char *payload(Buf *b) { return b->raw + GUARD; }
static void arm(Buf *b) {
    for (size_t i = 0; i < sizeof b->raw; i++) b->raw[i] = (unsigned char)(0xA5 ^ (i * 31));
}
static int guards_ok(Buf *b, size_t n) {
    for (size_t i = 0; i < GUARD; i++) {
        if (b->raw[i] != (unsigned char)(0xA5 ^ (i * 31))) return 0;
        size_t j = GUARD + n + i;
        if (b->raw[j] != (unsigned char)(0xA5 ^ (j * 31))) return 0;
    }
    return 1;
}

static void test_memcpy(void) {
    for (int t = 0; t < 4000; t++) {
        size_t n = rnd() % 300;
        Buf src, dst, ref;
        for (size_t i = 0; i < BUF; i++) src.raw[i] = (unsigned char)rnd();
        arm(&dst); arm(&ref);
        size_t so = rnd() % 8, dofs = rnd() % 8;
        memcpy(payload(&dst) + dofs, payload(&src) + so, n);
        ref_copy(payload(&ref) + dofs, payload(&src) + so, n);
        CHECK(memcmp(payload(&dst), payload(&ref), BUF) == 0, "memcpy mismatch n=%zu", n);
        CHECK(guards_ok(&dst, dofs + n), "memcpy wrote outside its range n=%zu", n);
    }
}

static void test_memmove(void) {
    for (int t = 0; t < 4000; t++) {
        size_t n = rnd() % 300;
        Buf a, ref;
        for (size_t i = 0; i < BUF; i++) a.raw[i] = (unsigned char)rnd();
        ref = a;
        size_t off = rnd() % 64;
        unsigned char *base = payload(&a);
        /* overlapping in both directions: dst before and after src */
        int forward = rnd() & 1;
        unsigned char *d = forward ? base + off : base;
        unsigned char *s = forward ? base : base + off;
        unsigned char *rd = payload(&ref) + (d - base);
        unsigned char *rs_ = payload(&ref) + (s - base);
        memmove(d, s, n);
        ref_copy(rd, rs_, n);
        CHECK(memcmp(payload(&a), payload(&ref), BUF) == 0, "memmove overlap mismatch n=%zu off=%zu", n, off);
    }
}

static void test_memset(void) {
    for (int t = 0; t < 2000; t++) {
        size_t n = rnd() % 300, o = rnd() % 8;
        Buf b; arm(&b);
        int c = (int)(rnd() % 256) - (int)(rnd() % 2) * 256; /* also negative ints */
        memset(payload(&b) + o, c, n);
        int ok = 1;
        for (size_t i = 0; i < n; i++) if (payload(&b)[o + i] != (unsigned char)c) ok = 0;
        CHECK(ok, "memset value wrong n=%zu c=%d", n, c);
        CHECK(guards_ok(&b, o + n), "memset wrote outside its range n=%zu", n);
    }
}

static void test_memcmp_memchr(void) {
    for (int t = 0; t < 3000; t++) {
        size_t n = rnd() % 64;
        unsigned char a[64], b[64];
        for (size_t i = 0; i < n; i++) a[i] = b[i] = (unsigned char)rnd();
        if (n && (rnd() & 1)) b[rnd() % n] ^= (unsigned char)(1 + rnd() % 255);
        int got = memcmp(a, b, n), want = ref_cmp(a, b, n);
        CHECK((got < 0) == (want < 0) && (got > 0) == (want > 0), "memcmp sign n=%zu", n);
        if (n) {
            unsigned char c = a[rnd() % n];
            size_t idx = 0; while (a[idx] != c) idx++;
            CHECK(memchr(a, c, n) == a + idx, "memchr finds first occurrence");
        }
        CHECK(memchr(a, 0x7F, 0) == NULL, "memchr with n=0 finds nothing");
    }
}

static void test_str(void) {
    char a[64], b[64], d[32];
    strcpy(a, "hello"); strcpy(b, "hello");
    CHECK(strlen(a) == 5 && strlen("") == 0, "strlen basics");
    CHECK(strcmp(a, b) == 0, "strcmp equal");
    CHECK(strcmp("abc", "abd") < 0 && strcmp("abd", "abc") > 0, "strcmp order");
    CHECK(strcmp("ab", "abc") < 0, "strcmp prefix");
    CHECK(strcmp("\x80", "\x01") > 0, "strcmp compares as unsigned char");
    CHECK(strncmp("abcdef", "abcxyz", 3) == 0, "strncmp stops at n");
    CHECK(strncmp("abc", "abd", 3) < 0, "strncmp detects difference");
    CHECK(strncmp("a", "b", 0) == 0, "strncmp n=0");
    CHECK(strnlen("abcdef", 3) == 3 && strnlen("ab", 10) == 2, "strnlen");
    CHECK(strchr("hello", 'z') == NULL, "strchr not found");
    CHECK(strchr(a, 'o') == a + 4, "strchr returns the position inside the string");
    CHECK(*strchr("hello", '\0') == '\0', "strchr finds terminator");
    memset(d, 'X', sizeof d);
    strncpy(d, "ab", 5);
    CHECK(d[0] == 'a' && d[1] == 'b' && d[2] == 0 && d[3] == 0 && d[4] == 0 && d[5] == 'X', "strncpy pads with NULs");
    memset(d, 'X', sizeof d);
    strncpy(d, "abcdef", 3);
    CHECK(d[0] == 'a' && d[2] == 'c' && d[3] == 'X', "strncpy truncates without terminator");
    CHECK(strcpy(d, "ok") == d && strcmp(d, "ok") == 0, "strcpy");
}

int main(void) {
    test_memcpy();
    test_memmove();
    test_memset();
    test_memcmp_memchr();
    test_str();
    printf("xerlibc string: %d passed, %d failed\n", passes, fails);
    return fails != 0;
}
